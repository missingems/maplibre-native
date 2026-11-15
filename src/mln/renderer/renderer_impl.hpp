#pragma once

#include <mln/renderer/render_orchestrator.hpp>
#include <mln/gfx/context_observer.hpp>

#if MLN_RENDER_BACKEND_METAL
#include <mln/mtl/mtl_fwd.hpp>
#include <Foundation/Foundation.hpp>
#endif // MLN_RENDER_BACKEND_METAL

#include <memory>
#include <string>

namespace mln {

class RendererObserver;
class RenderStaticData;
class RenderTree;
class PaintParameters;
class UpdateParameters;

namespace gfx {
class RendererBackend;
class ShadeRegistry;
class DynamicTextureAtlas;
using DynamicTextureAtlasPtr = std::shared_ptr<gfx::DynamicTextureAtlas>;
} // namespace gfx

class Renderer::Impl : public gfx::ContextObserver {
public:
    Impl(gfx::RendererBackend&, float pixelRatio_, const std::optional<std::string>& localFontFamily_);
    virtual ~Impl();

    // ContextObserver
    void onPreCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) override;
    void onPostCompileShader(shaders::BuiltIn, gfx::Backend::Type, const std::string&) override;
    void onShaderCompileFailed(shaders::BuiltIn, gfx::Backend::Type, const std::string&) override;
    void onRenderError(std::exception_ptr) override;

private:
    friend class Renderer;

    void setObserver(RendererObserver*);

    void render(const RenderTree&, const std::shared_ptr<UpdateParameters>&);

    void reduceMemoryUse();

#if MLN_RENDER_BACKEND_METAL
    /// Whether the retained tree can be redrawn as cached map plus overlay layers.
    bool canRenderOverlay(const std::shared_ptr<UpdateParameters>&);
    /// Redraws only the overlay layers over the cached map.
    void renderOverlay(const RenderTree&, const std::shared_ptr<UpdateParameters>&);
    /// Ends the current pass and starts one on the drawable, seeded with the cached map.
    void beginOverlayPass(PaintParameters&);
    void dropRetainedTree();
#endif // MLN_RENDER_BACKEND_METAL

    // TODO: Move orchestrator to Map::Impl.
    RenderOrchestrator orchestrator;

    gfx::RendererBackend& backend;

    RendererObserver* observer;

    const float pixelRatio;
    std::unique_ptr<RenderStaticData> staticData;
    gfx::DynamicTextureAtlasPtr dynamicTextureAtlas;
    bool styleLoaded = false;

    enum class RenderState {
        Never,
        Partial,
        Fully,
    };

    RenderState renderState = RenderState::Never;

    uint64_t frameCount = 0;

#if MLN_RENDER_BACKEND_METAL
    mtl::MTLCaptureScopePtr commandCaptureScope;

    // The last fully rendered tree, kept while its map image is cached and only overlay layers change.
    // Declared after `orchestrator`, which it refers to, so it is destroyed first.
    std::unique_ptr<RenderTree> retainedTree;
    std::shared_ptr<UpdateParameters> retainedParameters;
    bool overlayReady = false;
#endif // MLN_RENDER_BACKEND_METAL
};

} // namespace mln
