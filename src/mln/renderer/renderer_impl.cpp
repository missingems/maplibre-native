#include <mln/renderer/renderer_impl.hpp>

#include <mln/geometry/line_atlas.hpp>
#include <mln/gfx/backend_scope.hpp>
#include <mln/gfx/context.hpp>
#include <mln/gfx/cull_face_mode.hpp>
#include <mln/gfx/render_pass.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/upload_pass.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/pattern_atlas.hpp>
#include <mln/renderer/renderer_observer.hpp>
#include <mln/renderer/render_static_data.hpp>
#include <mln/renderer/render_tree.hpp>
#include <mln/renderer/update_parameters.hpp>
#include <mln/shaders/program_parameters.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/string.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/instrumentation.hpp>

#include <limits>
#include <optional>

#include <mln/gfx/drawable_tweaker.hpp>
#include <mln/renderer/layer_tweaker.hpp>
#include <mln/renderer/render_target.hpp>

#if MLN_RENDER_BACKEND_METAL
#include <mln/mtl/renderer_backend.hpp>
#include <mln/mtl/render_pass.hpp>
#include <mln/mtl/renderable_resource.hpp>
#include <Metal/MTLCaptureManager.hpp>
#include <Metal/MTLCaptureScope.hpp>
/// Enable programmatic Metal frame captures for specific frame numbers.
/// Requires iOS 13
constexpr auto EnableMetalCapture = 0;
constexpr auto CaptureFrameStart = 0; // frames are 0-based
constexpr auto CaptureFrameCount = 1;
#elif MLN_RENDER_BACKEND_OPENGL
#include <mln/gl/defines.hpp>
#include <mln/gl/drawable_gl.hpp>
#endif // !MLN_RENDER_BACKEND_METAL

namespace mln {

using namespace style;

namespace {

RendererObserver& nullObserver() {
    static RendererObserver observer;
    return observer;
}

} // namespace

Renderer::Impl::Impl(gfx::RendererBackend& backend_,
                     float pixelRatio_,
                     const std::optional<std::string>& localFontFamily_)
    : orchestrator(!backend_.contextIsShared(), backend_.getThreadPool(), localFontFamily_),
      backend(backend_),
      observer(&nullObserver()),
      pixelRatio(pixelRatio_) {}

Renderer::Impl::~Impl() {
    assert(gfx::BackendScope::exists());
};

void Renderer::Impl::onPreCompileShader(shaders::BuiltIn shaderID,
                                        gfx::Backend::Type type,
                                        const std::string& additionalDefines) {
    observer->onPreCompileShader(shaderID, type, additionalDefines);
}

void Renderer::Impl::onPostCompileShader(shaders::BuiltIn shaderID,
                                         gfx::Backend::Type type,
                                         const std::string& additionalDefines) {
    observer->onPostCompileShader(shaderID, type, additionalDefines);
}

void Renderer::Impl::onShaderCompileFailed(shaders::BuiltIn shaderID,
                                           gfx::Backend::Type type,
                                           const std::string& additionalDefines) {
    observer->onShaderCompileFailed(shaderID, type, additionalDefines);
}

void Renderer::Impl::onRenderError(std::exception_ptr error) {
    observer->onRenderError(error);
}

void Renderer::Impl::setObserver(RendererObserver* observer_) {
    observer = observer_ ? observer_ : &nullObserver();
}

void Renderer::Impl::render(const RenderTree& renderTree, const std::shared_ptr<UpdateParameters>& updateParameters) {
    MLN_TRACE_FUNC();
    auto& context = backend.getContext();
    context.setObserver(this);

    assert(updateParameters);

#if MLN_RENDER_BACKEND_METAL
#if MLN_CREATE_AUTORELEASEPOOL
    NS::SharedPtr pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
#endif

    if constexpr (EnableMetalCapture) {
        const auto& mtlBackend = static_cast<mtl::RendererBackend&>(backend);

        const auto& mtlDevice = mtlBackend.getDevice();

        if (!commandCaptureScope) {
            if (const auto& cmdQueue = mtlBackend.getCommandQueue()) {
                if (const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager())) {
                    // NOLINTNEXTLINE(bugprone-assignment-in-if-condition)
                    if ((commandCaptureScope = NS::TransferPtr(captureManager->newCaptureScope(cmdQueue.get())))) {
                        const auto label = "Renderer::Impl frame=" + util::toString(frameCount);
                        commandCaptureScope->setLabel(NS::String::string(label.c_str(), NS::UTF8StringEncoding));
                        captureManager->setDefaultCaptureScope(commandCaptureScope.get());
                    }
                }
            }
        }

        // "When you capture a frame programmatically, you can capture Metal commands that span multiple
        //  frames by using a custom capture scope. For example, by calling begin() at the start of frame
        //  1 and end() after frame 3, the trace will contain command data from all the buffers that were
        //  committed in the three frames."
        // https://developer.apple.com/documentation/metal/debugging_tools/capturing_gpu_command_data_programmatically
        if constexpr (0 < CaptureFrameStart && 0 < CaptureFrameCount) {
            if (commandCaptureScope) {
                const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
                if (frameCount == CaptureFrameStart) {
                    constexpr auto captureDest = MTL::CaptureDestination::CaptureDestinationDeveloperTools;
                    if (captureManager && !captureManager->isCapturing() &&
                        captureManager->supportsDestination(captureDest)) {
                        if (auto captureDesc = NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init())) {
                            captureDesc->setCaptureObject(mtlDevice.get());
                            captureDesc->setDestination(captureDest);
                            NS::Error* errorPtr = nullptr;
                            if (captureManager->startCapture(captureDesc.get(), &errorPtr)) {
                                Log::Warning(Event::Render, "Capture Started");
                            } else {
                                std::string errStr = "<none>";
                                if (auto error = NS::TransferPtr(errorPtr)) {
                                    if (auto str = error->localizedDescription()) {
                                        if (auto cstr = str->utf8String()) {
                                            errStr = cstr;
                                        }
                                    }
                                }
                                Log::Warning(Event::Render, "Capture Failed: " + errStr);
                            }
                        }
                    }
                }
            }
        }
        if (commandCaptureScope) {
            commandCaptureScope->beginScope();

            const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
            if (captureManager->isCapturing()) {
                Log::Info(Event::Render, "Capturing frame " + util::toString(frameCount));
            }
        }
    }
#endif // MLN_RENDER_BACKEND_METAL

    // Blocks execution until the renderable is available.
    backend.getDefaultRenderable().wait();
    context.beginFrame();

    if (!staticData) {
        staticData = std::make_unique<RenderStaticData>(std::make_unique<gfx::ShaderRegistry>());

        // Initialize shaders for drawables
        const auto programParameters = ProgramParameters{pixelRatio, false};
        backend.initShaders(*staticData->shaders, programParameters);

        // Notify post-shader registration
        observer->onRegisterShaders(*staticData->shaders);
    }

    const auto& renderTreeParameters = renderTree.getParameters();
    staticData->has3D = renderTreeParameters.has3D;
    const Size renderableSize = backend.getDefaultRenderable().getSize();

    if (renderState == RenderState::Never) {
        observer->onWillStartRenderingMap();
    }

    observer->onWillStartRenderingFrame();

    const TransformState& state = renderTreeParameters.transformParams.state;
    const EdgeInsets& frustumOffset = state.getFrustumOffset();
    const gfx::ScissorRect scissorRect = {
        .x = static_cast<int32_t>(frustumOffset.left() * pixelRatio),
#if MLN_RENDER_BACKEND_OPENGL
        .y = static_cast<int32_t>(frustumOffset.bottom() * pixelRatio),
#else
        .y = static_cast<int32_t>(frustumOffset.top() * pixelRatio),
#endif
        .width = renderableSize.width -
                 static_cast<uint32_t>((frustumOffset.left() + frustumOffset.right()) * pixelRatio),
        .height = renderableSize.height -
                  static_cast<uint32_t>((frustumOffset.top() + frustumOffset.bottom()) * pixelRatio),
    };

    PaintParameters parameters{
        context,
        pixelRatio,
        backend,
        renderTreeParameters.light,
        renderTreeParameters.mapMode,
        renderTreeParameters.debugOptions,
        renderTreeParameters.timePoint,
        renderTreeParameters.transformParams,
        *staticData,
        renderTree.getLineAtlas(),
        renderTree.getPatternAtlas(),
        frameCount,
        updateParameters->tileLodMinRadius,
        updateParameters->tileLodScale,
        updateParameters->tileLodPitchThreshold,
        updateParameters->tileLodMode,
        renderableSize,
        scissorRect,
    };

    parameters.symbolFadeChange = renderTreeParameters.symbolFadeChange;
    parameters.opaquePassCutoff = renderTreeParameters.opaquePassCutOff;
    const auto& sourceRenderItems = renderTree.getSourceRenderItems();

    const auto& layerRenderItems = renderTree.getLayerRenderItemMap();

    // - UPLOAD PASS -------------------------------------------------------------------------------
    // Uploads all required buffers and images before we do any actual rendering.
    {
        const auto uploadPass = parameters.encoder->createUploadPass("upload",
                                                                     parameters.backend.getDefaultRenderable());
#if !defined(NDEBUG)
        const auto debugGroup = uploadPass->createDebugGroup("upload");
#endif

        // Update all clipping IDs + upload buckets.
        for (const RenderItem& item : sourceRenderItems) {
            item.upload(*uploadPass);
        }
        for (const RenderItem& item : layerRenderItems) {
            item.upload(*uploadPass);
        }
        staticData->upload(*uploadPass);
        renderTree.getLineAtlas().upload(*uploadPass);
        renderTree.getPatternAtlas().upload(*uploadPass);
    }

    // - LAYER GROUP UPDATE ------------------------------------------------------------------------
    // Updates all layer groups and process changes
    if (staticData && staticData->shaders) {
        orchestrator.updateLayers(*staticData->shaders,
                                  context,
                                  renderTreeParameters.transformParams.state,
                                  updateParameters,
                                  parameters,
                                  renderTree);
    }

    orchestrator.processChanges();

    // Decide whether this frame renders the map into the layer cache, with the overlay layers on top of it.
    std::optional<int32_t> overlayIndex;
    bool cacheFrame = false;
#if MLN_RENDER_BACKEND_METAL
    {
        using CacheStage = mtl::RenderableResource::LayerCacheStage;
        auto& resource = backend.getDefaultRenderable().getResource<mtl::RenderableResource>();
        if (parameters.debugOptions == MapDebugOptions::NoDebug) {
            overlayIndex = orchestrator.cachedMapOverlayIndex();
        }
        cacheFrame = overlayIndex && resource.setLayerCacheStage(CacheStage::Base);
        if (!cacheFrame) {
            resource.setLayerCacheStage(CacheStage::None);
        }
    }
    overlayReady = false;
#endif // MLN_RENDER_BACKEND_METAL
    const int32_t overlayFirstIndex = cacheFrame ? *overlayIndex : std::numeric_limits<int32_t>::max();

    // Upload layer groups
    {
        const auto uploadPass = parameters.encoder->createUploadPass("layerGroup-upload",
                                                                     parameters.backend.getDefaultRenderable());
#if !defined(NDEBUG)
        const auto debugGroup = uploadPass->createDebugGroup("layerGroup-upload");
#endif

        // Update the debug layer groups
        orchestrator.updateDebugLayerGroups(renderTree, parameters);

        // Tweakers are run in the upload pass so they can set up uniforms.
        parameters.currentLayer = 0;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            layerGroup.runTweakers(renderTree, parameters);
            parameters.currentLayer++;
        });
        parameters.currentLayer = 0;
        orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) {
            layerGroup.runTweakers(renderTree, parameters);
            parameters.currentLayer++;
        });

        // Give the layers a chance to upload
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.upload(*uploadPass); });

        // Give the render targets a chance to upload
        orchestrator.visitRenderTargets([&](RenderTarget& renderTarget) { renderTarget.upload(*uploadPass); });

        // Upload the Debug layer group
        orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.upload(*uploadPass); });
    }

    const Size atlasSize = parameters.patternAtlas.getPixelSize();
    const auto& worldSize = parameters.renderableSize;
    const shaders::GlobalPaintParamsUBO globalPaintParamsUBO = {
        .pattern_atlas_texsize = {static_cast<float>(atlasSize.width), static_cast<float>(atlasSize.height)},
        .units_to_pixels = {1.0f / parameters.pixelsToGLUnits[0], 1.0f / parameters.pixelsToGLUnits[1]},
        .world_size = {static_cast<float>(worldSize.width), static_cast<float>(worldSize.height)},
        .camera_to_center_distance = parameters.state.getCameraToCenterDistance(),
        .symbol_fade_change = parameters.symbolFadeChange,
        .aspect_ratio = parameters.state.getSize().aspectRatio(),
        .pixel_ratio = parameters.pixelRatio,
        .map_zoom = static_cast<float>(parameters.state.getZoom()),
        .pad1 = 0,
    };
    auto& globalUniforms = context.mutableGlobalUniformBuffers();
    globalUniforms.createOrUpdate(shaders::idGlobalPaintParamsUBO, &globalPaintParamsUBO, context);

    // - 3D PASS
    // -------------------------------------------------------------------------------------
    // Renders any 3D layers bottom-to-top to unique FBOs with texture
    // attachments, but share the same depth rbo between them.
    const auto common3DPass = [&] {
        if (parameters.staticData.has3D) {
            const auto debugGroup(parameters.encoder->createDebugGroup("common-3d"));
            parameters.pass = RenderPass::Pass3D;
#if MLN_RENDER_BACKEND_OPENGL
            parameters.updateStencilBufferAvailability();
#endif

            // TODO is this needed?
            // if (!parameters.staticData.depthRenderbuffer ||
            //    parameters.staticData.depthRenderbuffer->getSize() != parameters.renderableSize) {
            //    parameters.staticData.depthRenderbuffer =
            //        parameters.context.createRenderbuffer<gfx::RenderbufferPixelType::Depth>(
            //            parameters.renderableSize);
            //}
            // parameters.staticData.depthRenderbuffer->setShouldClear(true);
        }
    };

    const auto drawable3DPass = [&] {
        const auto debugGroup(parameters.encoder->createDebugGroup("drawables-3d"));
        assert(parameters.pass == RenderPass::Pass3D);

        // draw layer groups, 3D pass
        parameters.currentLayer = static_cast<uint32_t>(orchestrator.numLayerGroups()) - 1;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            layerGroup.render(orchestrator, parameters);
            if (parameters.currentLayer > 0) {
                parameters.currentLayer--;
            }
        });
    };

    const auto drawableTargetsPass = [&] {
        // draw render targets
        orchestrator.visitRenderTargets(
            [&](RenderTarget& renderTarget) { renderTarget.render(orchestrator, renderTree, parameters); });
    };

    const auto commonClearPass = [&] {
        // - CLEAR
        // -------------------------------------------------------------------------------------
        // Renders the backdrop of the OpenGL view. This also paints in areas where
        // we don't have any tiles whatsoever.
        {
            std::optional<Color> color;
            if (parameters.debugOptions & MapDebugOptions::Overdraw) {
                color = Color::black();
            } else if (!backend.contextIsShared()) {
                color = renderTreeParameters.backgroundColor;
            }
            parameters.renderPass = parameters.encoder->createRenderPass(
                "main buffer",
                {.renderable = parameters.backend.getDefaultRenderable(),
                 .clearColor = color,
                 .clearDepth = 1.0f,
                 .clearStencil = 0});
#if MLN_RENDER_BACKEND_OPENGL
            parameters.updateStencilBufferAvailability();
#endif
        }
    };

    // Actually render the layers
    // Drawables
    const auto drawableOpaquePass = [&] {
        const auto debugGroup(parameters.renderPass->createDebugGroup("drawables-opaque"));
        parameters.pass = RenderPass::Opaque;
        parameters.depthRangeSize = 1 - (orchestrator.numLayerGroups() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;

        // draw layer groups, opaque pass
        parameters.currentLayer = 0;
        orchestrator.visitLayerGroupsReversed([&](LayerGroupBase& layerGroup) {
            layerGroup.render(orchestrator, parameters);
            parameters.currentLayer++;
        });
    };

    // Draws the translucent pass of the layer groups below (`overlay == false`) or from (`true`) the overlay run.
    const auto drawableTranslucentLayerGroups = [&](const bool overlay) {
        const auto debugGroup(
            parameters.renderPass->createDebugGroup(overlay ? "drawables-overlay" : "drawables-translucent"));
        parameters.pass = RenderPass::Translucent;
        parameters.depthRangeSize = 1 - (orchestrator.numLayerGroups() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;

        // draw layer groups, translucent pass
        parameters.currentLayer = static_cast<uint32_t>(orchestrator.numLayerGroups()) - 1;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            if ((layerGroup.getLayerIndex() >= overlayFirstIndex) == overlay) {
                layerGroup.render(orchestrator, parameters);
            }
            if (parameters.currentLayer > 0) {
                parameters.currentLayer--;
            }
        });
    };

    const auto drawableLegacyPass = [&] {
        const auto debugGroup(parameters.renderPass->createDebugGroup("drawables-legacy"));
        parameters.pass = RenderPass::Translucent;

        // Finally, render any legacy layers which have not been converted to drawables.
        // Note that they may be out of order, this is just a temporary fix for `RenderLocationIndicatorLayer` (#2216)
        parameters.depthRangeSize = 1 - (layerRenderItems.size() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;
        int32_t i = static_cast<int32_t>(layerRenderItems.size()) - 1;
        for (auto it = layerRenderItems.begin(); it != layerRenderItems.end() && i >= 0; ++it, --i) {
            parameters.currentLayer = i;
            const RenderItem& item = *it;
            if (item.hasRenderPass(parameters.pass)) {
                item.render(parameters);
            }
        }
    };

    const auto drawableDebugOverlays = [&] {
        // Renders debug overlays.
        {
            const auto debugGroup(parameters.renderPass->createDebugGroup("debug"));
            parameters.currentLayer = 0;
            orchestrator.visitDebugLayerGroups([&](LayerGroupBase& layerGroup) {
                layerGroup.render(orchestrator, parameters);
                parameters.currentLayer++;
            });
        }
    };

    if (parameters.staticData.has3D) {
        common3DPass();
        drawable3DPass();
    }
    drawableTargetsPass();
    commonClearPass();
    context.bindGlobalUniformBuffers(*parameters.renderPass);
    drawableOpaquePass();
    drawableTranslucentLayerGroups(false);
#if MLN_RENDER_BACKEND_METAL
    if (cacheFrame) {
        // The map below the overlay run is now in the layer cache. Continue on the drawable.
        beginOverlayPass(parameters);
        drawableTranslucentLayerGroups(true);
    }
#endif // MLN_RENDER_BACKEND_METAL
    drawableLegacyPass();
    drawableDebugOverlays();

    // Give the layers a chance to do cleanup
    orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) { layerGroup.postRender(orchestrator, parameters); });
    context.unbindGlobalUniformBuffers(*parameters.renderPass);

    // Ends the RenderPass
    parameters.renderPass.reset();

    const auto startRendering = util::MonotonicTimer::now().count();
    // present submits render commands
    parameters.encoder->present(parameters.backend.getDefaultRenderable());
    context.renderingStats().renderingTime = util::MonotonicTimer::now().count() - startRendering;

    parameters.encoder.reset();
    context.endFrame();

#if MLN_RENDER_BACKEND_METAL
    if constexpr (EnableMetalCapture) {
        if (commandCaptureScope) {
            commandCaptureScope->endScope();

            const auto captureManager = NS::RetainPtr(MTL::CaptureManager::sharedCaptureManager());
            if (frameCount == CaptureFrameStart + CaptureFrameCount - 1 && captureManager->isCapturing()) {
                captureManager->stopCapture();
            }
        }
    }
#endif // MLN_RENDER_BACKEND_METAL

    context.renderingStats().encodingTime = renderTree.getElapsedTime() - context.renderingStats().renderingTime;

    observer->onDidFinishRenderingFrame(
        renderTreeParameters.loaded ? RendererObserver::RenderMode::Full : RendererObserver::RenderMode::Partial,
        renderTreeParameters.needsRepaint,
        renderTreeParameters.placementChanged,
        context.threadSafeCopyRenderingStats());

    if (!renderTreeParameters.loaded) {
        renderState = RenderState::Partial;
    } else if (renderState != RenderState::Fully) {
        renderState = RenderState::Fully;
        observer->onDidFinishRenderingMap();
    }

#if MLN_RENDER_BACKEND_METAL
    overlayReady = cacheFrame && !renderTreeParameters.needsRepaint;
#endif // MLN_RENDER_BACKEND_METAL

    frameCount += 1;
    MLN_END_FRAME();
}

#if MLN_RENDER_BACKEND_METAL
void Renderer::Impl::dropRetainedTree() {
    retainedTree.reset();
    retainedParameters.reset();
    overlayReady = false;
}

bool Renderer::Impl::canRenderOverlay(const std::shared_ptr<UpdateParameters>& updateParameters) {
    return retainedTree && staticData && retainedParameters == updateParameters &&
           orchestrator.cachedMapOverlayIndex() &&
           backend.getDefaultRenderable().getResource<mtl::RenderableResource>().hasLayerCache();
}

void Renderer::Impl::beginOverlayPass(PaintParameters& parameters) {
    using CacheStage = mtl::RenderableResource::LayerCacheStage;
    auto& context = backend.getContext();
    auto& resource = backend.getDefaultRenderable().getResource<mtl::RenderableResource>();

    if (parameters.renderPass) {
        context.unbindGlobalUniformBuffers(*parameters.renderPass);
        parameters.renderPass.reset();
    }

    resource.setLayerCacheStage(CacheStage::Overlay);
    parameters.renderPass = parameters.encoder->createRenderPass(
        "overlay",
        {.renderable = backend.getDefaultRenderable(), .clearColor = {}, .clearDepth = {}, .clearStencil = {}});

    auto& mtlRenderPass = static_cast<mtl::RenderPass&>(*parameters.renderPass);
    resource.drawLayerCache(mtlRenderPass.getMetalEncoder());
    mtlRenderPass.resetState();
    context.setDirtyState();
    context.bindGlobalUniformBuffers(*parameters.renderPass);
}

void Renderer::Impl::renderOverlay(const RenderTree& renderTree,
                                   const std::shared_ptr<UpdateParameters>& updateParameters) {
    MLN_TRACE_FUNC();
    auto& context = backend.getContext();
    context.setObserver(this);

#if MLN_CREATE_AUTORELEASEPOOL
    NS::SharedPtr pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
#endif

    const auto overlayIndex = orchestrator.cachedMapOverlayIndex();
    assert(overlayIndex && staticData);

    backend.getDefaultRenderable().wait();
    context.beginFrame();

    const auto& renderTreeParameters = renderTree.getParameters();
    const Size renderableSize = backend.getDefaultRenderable().getSize();
    const TransformState& state = renderTreeParameters.transformParams.state;
    const EdgeInsets& frustumOffset = state.getFrustumOffset();
    const gfx::ScissorRect scissorRect = {
        .x = static_cast<int32_t>(frustumOffset.left() * pixelRatio),
        .y = static_cast<int32_t>(frustumOffset.top() * pixelRatio),
        .width = renderableSize.width -
                 static_cast<uint32_t>((frustumOffset.left() + frustumOffset.right()) * pixelRatio),
        .height = renderableSize.height -
                  static_cast<uint32_t>((frustumOffset.top() + frustumOffset.bottom()) * pixelRatio),
    };

    PaintParameters parameters{
        context,
        pixelRatio,
        backend,
        renderTreeParameters.light,
        renderTreeParameters.mapMode,
        renderTreeParameters.debugOptions,
        renderTreeParameters.timePoint,
        renderTreeParameters.transformParams,
        *staticData,
        renderTree.getLineAtlas(),
        renderTree.getPatternAtlas(),
        frameCount,
        updateParameters->tileLodMinRadius,
        updateParameters->tileLodScale,
        updateParameters->tileLodPitchThreshold,
        updateParameters->tileLodMode,
        renderableSize,
        scissorRect,
    };
    parameters.symbolFadeChange = renderTreeParameters.symbolFadeChange;
    parameters.opaquePassCutoff = renderTreeParameters.opaquePassCutOff;

    orchestrator.preRenderOverlayLayers(*overlayIndex, context, parameters);

    beginOverlayPass(parameters);
    {
        const auto debugGroup(parameters.renderPass->createDebugGroup("drawables-overlay"));
        parameters.pass = RenderPass::Translucent;
        parameters.depthRangeSize = 1 - (orchestrator.numLayerGroups() + 2) * PaintParameters::numSublayers *
                                            PaintParameters::depthEpsilon;
        parameters.currentLayer = static_cast<uint32_t>(orchestrator.numLayerGroups()) - 1;
        orchestrator.visitLayerGroups([&](LayerGroupBase& layerGroup) {
            if (layerGroup.getLayerIndex() >= *overlayIndex) {
                layerGroup.render(orchestrator, parameters);
            }
            if (parameters.currentLayer > 0) {
                parameters.currentLayer--;
            }
        });
    }
    context.unbindGlobalUniformBuffers(*parameters.renderPass);
    parameters.renderPass.reset();

    parameters.encoder->present(backend.getDefaultRenderable());
    parameters.encoder.reset();
    context.endFrame();

    frameCount += 1;
    MLN_END_FRAME();
}
#endif // MLN_RENDER_BACKEND_METAL

void Renderer::Impl::reduceMemoryUse() {
    assert(gfx::BackendScope::exists());
    backend.getContext().reduceMemoryUsage();
}

} // namespace mln
