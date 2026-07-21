#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <chrono>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

// Per-window glass state shared between the BOTTOM decoration (redirect) and
// the OVER decoration (composite), mirroring CGlassLayerSurface's role for
// layer surfaces but bracketing a window's own CSurfacePassElement instead
// of a hooked renderLayer call.
class CWindowGlassState {
  public:
    explicit CWindowGlassState(PHLWINDOW window);
    ~CWindowGlassState();

    // Pre-surface (BOTTOM layer): sample+blur background, redirect currentFB -> temp FBO
    void sampleAndRedirect(PHLMONITOR monitor, float alpha);

    // Post-surface (OVER layer): restore currentFB, composite glass masked by temp FBO
    void compositeAndRestore(PHLMONITOR monitor, float alpha);

    void damageIfMoved();

    // Advances the focus transition toward the window's current focus state
    // by one frame's worth of easing. Returns true while still transitioning
    // (caller should damage the window so the eased value actually reaches
    // the screen). Call exactly once per frame - see CGlassDecoration::draw().
    bool tickFocusAnim();
    // Current transition state: 1.0 = fully focused look, 0.0 = fully
    // unfocused look. Read-only; updated by tickFocusAnim().
    [[nodiscard]] float focusFactor() const { return m_focusAnim; }

    [[nodiscard]] PHLWINDOW getWindow() const;
    [[nodiscard]] bool      resolveEnabled() const;

  private:
    PHLWINDOWREF              m_window;
    SP<Render::IFramebuffer>  m_sampleFramebuffer;
    SP<Render::IFramebuffer>  m_sharpFramebuffer;
    SP<Render::IFramebuffer>  m_surfaceTempFramebuffer;
    GlassRenderer::SSampleLayout m_sampleLayout;
    bool                      m_hasCachedSample = false;

    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    CBox     m_lastSampleBox      = {};

    SP<Render::IFramebuffer> m_savedCurrentFB;

    // Focus-transition ease state. Initialized to the window's real focus
    // state at construction (see .cpp) so a freshly opened window doesn't
    // animate in from "unfocused".
    float                                 m_focusAnim;
    std::chrono::steady_clock::time_point m_lastFocusTick;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;
};
