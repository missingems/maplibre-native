#import "MLNDisplayUtils.h"
#import "MLNFoundation_Private.h"
#import "MLNLoggingConfiguration_Private.h"
#import "MLNMapView+Metal.h"

#import <mln/mtl/renderable_resource.hpp>

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#import <Metal/Metal.hpp>

@interface MLNMapViewImplDelegate : NSObject <MTKViewDelegate>
@end

@implementation MLNMapViewImplDelegate {
  MLNMapViewMetalImpl* _impl;
}

- (instancetype)initWithImpl:(MLNMapViewMetalImpl*)impl {
  if (self = [super init]) {
    _impl = impl;
  }
  return self;
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}

- (void)drawInMTKView:(MTKView*)view {
  _impl->render();
}

@end

class MLNMapViewMetalRenderableResource final : public mln::mtl::RenderableResource {
public:
  MLNMapViewMetalRenderableResource(MLNMapViewMetalImpl& backend_)
      : backend(backend_), delegate([[MLNMapViewImplDelegate alloc] initWithImpl:&backend]) {}

  void bind() override {
    if (!commandQueue) {
      // The renderer backend's queue, not a queue of the view's own: offscreen passes (drape
      // targets, hillshade prepare) commit to the backend's queue, and only command buffers on
      // one queue are guaranteed to run in commit order with the hazard tracking that orders a
      // target's writes before the frame's reads. On a separate queue the frame could sample a
      // drape target mid-render (white flashes over terrain) unless every offscreen pass waited
      // for the GPU to finish.
      commandQueue = (__bridge id<MTLCommandQueue>)backend.getCommandQueue().get();
    }

    if (!commandBuffer) {
      commandBuffer = [commandQueue commandBuffer];
      commandBufferPtr = NS::RetainPtr((__bridge MTL::CommandBuffer*)commandBuffer);
    }
  }

  const mln::mtl::RendererBackend& getBackend() const override { return backend; }

  const mln::mtl::MTLCommandBufferPtr& getCommandBuffer() const override {
    return commandBufferPtr;
  }

  virtual mln::mtl::MTLBlitPassDescriptorPtr getUploadPassDescriptor() const override {
    // Create from render pass descriptor?
    return NS::TransferPtr(MTL::BlitPassDescriptor::alloc()->init());
  }

  const mln::mtl::MTLRenderPassDescriptorPtr& getRenderPassDescriptor() const override {
    if (!cachedRenderPassDescriptor) {
      MTLRenderPassDescriptor* mtlDesc = mtlView.currentRenderPassDescriptor;
      if (mtlDesc && cacheStage != LayerCacheStage::None) {
        mtlDesc = [mtlDesc copy];
        if (cacheStage == LayerCacheStage::Base) {
          // The map below the overlay layers renders into the cache, colour and depth both kept.
          mtlDesc.colorAttachments[0].texture = cacheColor;
          mtlDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
          mtlDesc.depthAttachment.loadAction = MTLLoadActionClear;
          mtlDesc.depthAttachment.clearDepth = 1.0;
          mtlDesc.depthAttachment.storeAction = MTLStoreActionStore;
          mtlDesc.stencilAttachment.loadAction = MTLLoadActionClear;
          mtlDesc.stencilAttachment.clearStencil = 0;
          mtlDesc.stencilAttachment.storeAction = MTLStoreActionStore;
        } else {
          // The overlay layers render to the drawable, which `drawLayerCache` fills first, against
          // the cached depth. Their own depth writes are discarded with the pass.
          mtlDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
          mtlDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
          mtlDesc.depthAttachment.loadAction = MTLLoadActionLoad;
          mtlDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
          mtlDesc.stencilAttachment.loadAction = MTLLoadActionLoad;
          mtlDesc.stencilAttachment.storeAction = MTLStoreActionDontCare;
        }
        mtlDesc.depthAttachment.texture = cacheDepth;
        mtlDesc.stencilAttachment.texture = cacheDepth;
      }
      cachedRenderPassDescriptor = NS::RetainPtr((__bridge MTL::RenderPassDescriptor*)mtlDesc);
    }
    return cachedRenderPassDescriptor;
  }

  bool setLayerCacheStage(LayerCacheStage stage) override {
    if (stage == cacheStage) {
      return true;
    }
    if (stage == LayerCacheStage::Base) {
      if (!prepareLayerCache()) {
        return false;
      }
      cacheFilled = true;
    } else if (stage == LayerCacheStage::Overlay && !hasLayerCache()) {
      return false;
    }
    cacheStage = stage;
    cachedRenderPassDescriptor.reset();
    return true;
  }

  bool hasLayerCache() const override {
    return cacheFilled && cacheColor && cacheColor.width == mtlView.drawableSize.width &&
           cacheColor.height == mtlView.drawableSize.height;
  }

  void drawLayerCache(const mln::mtl::MTLRenderCommandEncoderPtr& encoderPtr) override {
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr.get();
    if (!encoder || !cacheColor || !cachePipeline) {
      return;
    }
    [encoder pushDebugGroup:@"layer-cache"];
    [encoder setRenderPipelineState:cachePipeline];
    [encoder setFragmentTexture:cacheColor atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder popDebugGroup];
  }

  void releaseLayerCache() {
    cacheColor = nil;
    cacheDepth = nil;
    cacheFilled = false;
  }

  void swap() override {
    id<CAMetalDrawable> currentDrawable = [mtlView currentDrawable];
    if (currentDrawable) {
      if (presentsWithTransaction) {
        // Presenting inside the Core Animation transaction keeps the map in step with UIKit
        // annotation views. That needs the frame on the GPU's queue before present, not finished:
        // waitUntilCompleted serialised every frame - the CPU idle while the GPU drew, the GPU idle
        // while the CPU encoded the next - and with view annotations on (any at all, see
        // MLNPresentsWithTransactionAnnotationCount) a pitched terrain map ran at ~15 fps with the
        // GPU 27 % busy and the main thread ~30 % (iPhone 16 Pro). Apple's guidance for
        // presentsWithTransaction is waitUntilScheduled, then present.
        [commandBuffer commit];
        [commandBuffer waitUntilScheduled];
        [currentDrawable present];
      } else {
        [commandBuffer presentDrawable:currentDrawable];
        [commandBuffer commit];
      }
    }

    commandBuffer = nil;
    commandBufferPtr.reset();

    cachedRenderPassDescriptor.reset();
    cacheStage = LayerCacheStage::None;
  }

  mln::Size framebufferSize() {
    assert(mtlView);
    return {static_cast<uint32_t>(mtlView.drawableSize.width),
            static_cast<uint32_t>(mtlView.drawableSize.height)};
  }

private:
  /// Makes the cache textures match the drawable and the copy pipeline exist.
  bool prepareLayerCache() {
    id<MTLDevice> device = mtlView.device;
    const auto width = static_cast<NSUInteger>(mtlView.drawableSize.width);
    const auto height = static_cast<NSUInteger>(mtlView.drawableSize.height);
    if (!device || !width || !height || mtlView.sampleCount != 1) {
      return false;
    }

    if (!cachePipeline && !cachePipelineFailed) {
      NSString* source = @"#include <metal_stdlib>\n"
                          "using namespace metal;\n"
                          "struct Out { float4 position [[position]]; };\n"
                          "vertex Out layerCacheVertex(uint vid [[vertex_id]]) {\n"
                          "    float2 p = float2((vid << 1) & 2, vid & 2);\n"
                          "    Out out;\n"
                          "    out.position = float4(p * 2.0 - 1.0, 1.0, 1.0);\n"
                          "    return out;\n"
                          "}\n"
                          "fragment float4 layerCacheFragment(Out in [[stage_in]], texture2d<float> t [[texture(0)]]) {\n"
                          "    return t.read(uint2(in.position.xy));\n"
                          "}\n";
      NSError* error = nil;
      id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
      MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
      desc.label = @"layer-cache";
      desc.vertexFunction = [library newFunctionWithName:@"layerCacheVertex"];
      desc.fragmentFunction = [library newFunctionWithName:@"layerCacheFragment"];
      desc.colorAttachments[0].pixelFormat = mtlView.colorPixelFormat;
      desc.depthAttachmentPixelFormat = mtlView.depthStencilPixelFormat;
      desc.stencilAttachmentPixelFormat = mtlView.depthStencilPixelFormat;
      if (library) {
        cachePipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
      }
      cachePipelineFailed = !cachePipeline;
    }
    if (!cachePipeline) {
      return false;
    }

    if (!cacheColor || cacheColor.width != width || cacheColor.height != height) {
      cacheFilled = false;
      MTLTextureDescriptor* colorDesc =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlView.colorPixelFormat
                                                             width:width
                                                            height:height
                                                         mipmapped:NO];
      colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      colorDesc.storageMode = MTLStorageModePrivate;
      cacheColor = [device newTextureWithDescriptor:colorDesc];
      cacheColor.label = @"layer-cache-color";

      MTLTextureDescriptor* depthDesc =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlView.depthStencilPixelFormat
                                                             width:width
                                                            height:height
                                                         mipmapped:NO];
      depthDesc.usage = MTLTextureUsageRenderTarget;
      depthDesc.storageMode = MTLStorageModePrivate;
      cacheDepth = [device newTextureWithDescriptor:depthDesc];
      cacheDepth.label = @"layer-cache-depth";
    }
    return cacheColor && cacheDepth;
  }

  MLNMapViewMetalImpl& backend;
  mln::mtl::MTLCommandBufferPtr commandBufferPtr;
  mutable mln::mtl::MTLRenderPassDescriptorPtr cachedRenderPassDescriptor;

  // The map below the overlay layers, kept so those layers can be redrawn alone.
  LayerCacheStage cacheStage = LayerCacheStage::None;
  id<MTLTexture> cacheColor = nil;
  id<MTLTexture> cacheDepth = nil;
  id<MTLRenderPipelineState> cachePipeline = nil;
  bool cachePipelineFailed = false;
  bool cacheFilled = false;

public:
  MLNMapViewImplDelegate* delegate = nil;
  MTKView* mtlView = nil;
  id<MTLCommandBuffer> commandBuffer;
  id<MTLCommandQueue> commandQueue;
  bool presentsWithTransaction = false;

  // Cached last-applied values comparing against MTKView's reflected state round-trips
  // through UIKit and can defeat the no-op guard under non-integer scale factors.
  CGFloat lastAppliedScaleFactor = 0;
  CGSize lastAppliedDrawableSize = CGSizeZero;

  // We count how often the context was activated/deactivated so that we can truly deactivate it
  // after the activation count drops to 0.
  NSUInteger activationCount = 0;
};

MLNMapViewMetalImpl::MLNMapViewMetalImpl(MLNMapView* nativeView_)
    : MLNMapViewImpl(nativeView_),
      mln::mtl::RendererBackend(mln::gfx::ContextMode::Unique),
      mln::gfx::Renderable({0, 0}, std::make_unique<MLNMapViewMetalRenderableResource>(*this)) {}

MLNMapViewMetalImpl::~MLNMapViewMetalImpl() = default;

void MLNMapViewMetalImpl::setOpaque(const bool opaque) {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  resource.mtlView.opaque = opaque;
  resource.mtlView.layer.opaque = opaque;
}

void MLNMapViewMetalImpl::setPresentsWithTransaction(const bool value) {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  resource.presentsWithTransaction = value;

  if (@available(iOS 13.0, *)) {
    if (CAMetalLayer* metalLayer = MLN_OBJC_DYNAMIC_CAST(resource.mtlView.layer, CAMetalLayer)) {
      metalLayer.presentsWithTransaction = value;
    }
  }
}

void MLNMapViewMetalImpl::display() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();

  // Calling `display` here directly causes the stuttering bug (if
  // `presentsWithTransaction` is `YES` - see above)
  // as reported in https://github.com/mapbox/mapbox-gl-native-ios/issues/350
  //
  // Since we use `presentsWithTransaction` to synchronize with UIView
  // annotations, we now let the system handle when the view is rendered. This
  // has the potential to increase latency
  [resource.mtlView setNeedsDisplay];
}

void MLNMapViewMetalImpl::createView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (resource.mtlView) {
    return;
  }

  id<MTLDevice> device = (__bridge id<MTLDevice>)resource.getBackend().getDevice().get();
  const auto scaleFactor = MLNEffectiveScaleFactorForView(mapView);

  resource.mtlView = [[MTKView alloc] initWithFrame:mapView.bounds device:device];
  resource.mtlView.delegate = resource.delegate;
  resource.mtlView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  // We own drawable sizing in layoutChanged(); disable MTKView's automatic recompute so
  // setting contentScaleFactor cannot race with explicit drawableSize assignment.
  resource.mtlView.autoResizeDrawable = NO;
  resource.mtlView.contentScaleFactor = scaleFactor;
  resource.mtlView.contentMode = UIViewContentModeCenter;
  resource.mtlView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  resource.mtlView.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  resource.mtlView.opaque = mapView.opaque;
  resource.mtlView.layer.opaque = mapView.opaque;
  resource.mtlView.enableSetNeedsDisplay = YES;
  if (@available(iOS 13.0, *)) {
    CAMetalLayer* metalLayer = MLN_OBJC_DYNAMIC_CAST(resource.mtlView.layer, CAMetalLayer);
    metalLayer.presentsWithTransaction = resource.presentsWithTransaction;
  }

  [mapView insertSubview:resource.mtlView atIndex:0];
}

UIView* MLNMapViewMetalImpl::getView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  return resource.mtlView;
}

void MLNMapViewMetalImpl::deleteView() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  [resource.mtlView releaseDrawables];
  resource.releaseLayerCache();
}

void MLNMapViewMetalImpl::activate() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (resource.activationCount++) {
    return;
  }
}

void MLNMapViewMetalImpl::deactivate() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  if (--resource.activationCount) {
    return;
  }
}

UIImage* MLNMapViewMetalImpl::snapshot() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  return nil;  // TODO: resource.mtlView.snapshot;
}

void MLNMapViewMetalImpl::layoutChanged() {
  // Fix: unconditionally rewriting contentScaleFactor/drawableSize every
  // layout pass caused a feedback loop under iOS 26 Smart Display Zoom (non-integer scale).
  // Also skip pre-layout/detached passes — MLNEffectiveScaleFactorForView falls back to
  // mainScreen without a window, wrong for CarPlay.
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  const auto viewSize = mapView.bounds.size;
  if (!resource.mtlView || viewSize.width <= 0 || viewSize.height <= 0 || !mapView.window) {
    return;
  }

  const auto scaleFactor = MLNEffectiveScaleFactorForView(mapView);
  const CGSize target = CGSizeMake(std::round(viewSize.width * scaleFactor),
                                   std::round(viewSize.height * scaleFactor));

  if (scaleFactor != resource.lastAppliedScaleFactor) {
    resource.mtlView.contentScaleFactor = resource.lastAppliedScaleFactor = scaleFactor;
  }
  if (!CGSizeEqualToSize(target, resource.lastAppliedDrawableSize)) {
    resource.mtlView.drawableSize = resource.lastAppliedDrawableSize = target;
  }

  setRenderableSize({static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)});
}

MLNBackendResource* MLNMapViewMetalImpl::getObject() {
  auto& resource = getResource<MLNMapViewMetalRenderableResource>();
  auto renderPassDescriptor = resource.getRenderPassDescriptor().get();

  return [[MLNBackendResource alloc] initWithMTKView:resource.mtlView
                                              device:resource.mtlView.device
                                renderPassDescriptor:[MTLRenderPassDescriptor renderPassDescriptor]
                                       commandBuffer:resource.commandBuffer];
}
