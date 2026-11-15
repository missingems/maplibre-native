#pragma once

#include <mln/style/layers/custom_layer_impl.hpp>
#include <mln/renderer/render_layer.hpp>

namespace mln {

class RenderCustomLayer final : public RenderLayer {
public:
    explicit RenderCustomLayer(Immutable<style::CustomLayer::Impl>);
    ~RenderCustomLayer() override;

    /// Generate any changes needed by the layer
    void update(gfx::ShaderRegistry &,
                gfx::Context &,
                const TransformState &,
                const std::shared_ptr<UpdateParameters> &,
                const PaintParameters &,
                const RenderTree &,
                UniqueChangeRequestVec &) override;

    /// Whether the host allows this layer to be redrawn over a cached image of the layers below.
    bool drawsOverCachedMap() const;

    /// Calls the host's pre-render outside of a full layer update.
    void preRender(gfx::Context &, const PaintParameters &);

private:
    void transition(const TransitionParameters &) override {}
    void evaluate(const PropertyEvaluationParameters &) override;
    bool hasTransition() const override;
    bool hasCrossfade() const override;
    void markContextDestroyed() override;
    void prepare(const LayerPrepareParameters &) override;

    bool contextDestroyed = false;
    bool needsInitialize = true;
    std::shared_ptr<style::CustomLayerHost> host;
};

} // namespace mln
