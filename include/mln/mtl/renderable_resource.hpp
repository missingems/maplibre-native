#pragma once

#include <mln/gfx/renderable.hpp>
#include <mln/mtl/mtl_fwd.hpp>

#include <cstdlib>

namespace mln {
namespace mtl {

class RendererBackend;

class RenderableResource : public gfx::RenderableResource {
protected:
    explicit RenderableResource() = default;

public:
    virtual void swap() {
        // Renderable resources that require a swap function to be called
        // explicitly can override this method.
    }

    virtual const mln::mtl::RendererBackend& getBackend() const = 0;
    virtual const mln::mtl::MTLCommandBufferPtr& getCommandBuffer() const = 0;
    virtual mln::mtl::MTLBlitPassDescriptorPtr getUploadPassDescriptor() const = 0;
    virtual const mln::mtl::MTLRenderPassDescriptorPtr& getRenderPassDescriptor() const = 0;

    /// Which target `getRenderPassDescriptor` describes. `Base` renders into a retained colour and
    /// depth cache, `Overlay` renders to the drawable over that cache's depth.
    enum class LayerCacheStage {
        None,
        Base,
        Overlay,
    };

    /// Returns false when the resource can't provide the stage (no cache support, or for `Overlay`
    /// no cache matching the current drawable); the stage is then `None`.
    virtual bool setLayerCacheStage(LayerCacheStage stage) { return stage == LayerCacheStage::None; }

    /// Whether a filled cache matching the current drawable exists.
    virtual bool hasLayerCache() const { return false; }

    /// Copies the cached colour into the current (`Overlay`) render pass.
    virtual void drawLayerCache(const mln::mtl::MTLRenderCommandEncoderPtr&) {}
};

} // namespace mtl
} // namespace mln
