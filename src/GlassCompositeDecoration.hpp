#pragma once

#include "WindowGlassState.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <memory>

// OVER-layer half of window glass: queues the composite+restore step after
// the window's own surface has rendered. Paired with CGlassDecoration
// (BOTTOM), sharing the same CWindowGlassState.
class CGlassCompositeDecoration : public IHyprWindowDecoration {
  public:
    CGlassCompositeDecoration(PHLWINDOW window, std::shared_ptr<CWindowGlassState> state);
    ~CGlassCompositeDecoration() override = default;

    [[nodiscard]] SDecorationPositioningInfo getPositioningInfo() override;
    void                                     onPositioningReply(const SDecorationPositioningReply& reply) override;
    void                                     draw(PHLMONITOR monitor, float const& alpha) override;
    [[nodiscard]] eDecorationType            getDecorationType() override;
    void                                     updateWindow(PHLWINDOW window) override;
    void                                     damageEntire() override;
    [[nodiscard]] eDecorationLayer           getDecorationLayer() override;
    [[nodiscard]] uint64_t                   getDecorationFlags() override;
    [[nodiscard]] std::string                getDisplayName() override;

    WP<CGlassCompositeDecoration> m_self;

  private:
    PHLWINDOWREF                       m_window;
    std::shared_ptr<CWindowGlassState> m_state;
};
