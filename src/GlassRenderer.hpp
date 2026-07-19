#pragma once

#include "PluginConfig.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <optional>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Maximum downscale factor for blur sampling. Half-res (2) is 4x cheaper
// per blur pass. Only applied when blur is strong enough to hide the lower
// resolution — weak blur at half-res shows visible pixelation.
inline constexpr int   BLUR_DOWNSCALE_MAX       = 2;
inline constexpr float BLUR_DOWNSCALE_THRESHOLD = 0.35f; // min blur_strength for downscale

// Side length (px) of the offscreen buffer used by computeAlphaContentBox to
// detect a layer's real content bounds. Coarse on purpose: the searched
// region is downsampled GPU-side to this size before the one CPU readback, so
// cost is independent of how large the searched box is - a finer probe only
// tightens the detected box, it doesn't change the cost model.
inline constexpr int ALPHA_PROBE_SIZE = 128;

// Layers only: alpha mask from the temp FBO that captured the rendered surface.
// Constrains the glass effect to regions where the layer has visible content.
// Windows do not use masking, they pass mask=nullptr to applyGlassEffect.
struct SMaskInfo {
    GLuint   textureId;
    GLenum   target;
    Vector2D uvOffset; // mapping from glass box UV → full surface UV
    Vector2D uvScale;
    float    alphaThreshold = 0.001f;
};

// UV-space layout of a captured background sample. uvOffset/uvScale map a
// box's local UV [0,1] to the padded texture's UV (texUV = uv * uvScale +
// uvOffset); for the box a sample was captured for, this simply carves out
// the non-padded interior. narrowSampleLayout() remaps these to instead
// address a tighter sub-rect of that same captured texture. validMin/validMax
// bound the part of the texture that actually received content (the blit is
// clamped at screen edges and the remainder is cleared black - the shader
// clamps its sampling to these bounds so edge windows stretch instead of
// refracting black); they're already absolute texture-space bounds, so they
// don't change when narrowing to a sub-rect.
struct SSampleLayout {
    Vector2D uvOffset = {0.0, 0.0};
    Vector2D uvScale  = {1.0, 1.0};
    Vector2D validMin = {0.0, 0.0};
    Vector2D validMax = {1.0, 1.0};
};

// sharpFramebuffer, when non-null, receives a full-resolution unblurred copy
// of the same padded region (always full-res, even when the blur sample is
// downscaled) - the shader's refracted rim samples it, and it doubles as the
// authoritative background cache: pass partialDamage (buffer coords,
// pre-intersected with the padded region) to update only freshly-rendered
// rects instead of recapturing everything; nullptr = full capture (required
// whenever the sampled geometry changed).
void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, SSampleLayout& outLayout, int downscale = 1,
                       SP<Render::IFramebuffer>* sharpFramebuffer = nullptr,
                       const CRegion* partialDamage = nullptr);

void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    GLuint callerFramebufferID, int viewportWidth, int viewportHeight);

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
// refractOutward: true = rim pulls in content from beyond the boundary
// (floating windows, layers); false = inward compression (tiled windows,
// where outward sampling would show the neighboring windows in the rim).
void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const SSampleLayout& sampleLayout, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr,
                       SP<Render::IFramebuffer> sharpFramebuffer = nullptr,
                       bool refractOutward = true);

// Layers only: tight axis-aligned bounding box (in the same buffer/pixel
// space as searchBox) of alphaSource's non-transparent content within
// searchBox. Used so refraction geometry hugs a layer's actual visible
// content instead of its reported (possibly oversized) box - e.g. a launcher
// that reports fullscreen bounds for click-catching but only draws a small
// centered panel. Downsamples to a small probe FBO (ALPHA_PROBE_SIZE) before
// a single CPU readback, so this stays cheap regardless of searchBox's size.
// Returns std::nullopt when nothing exceeds alphaThreshold (not yet rendered,
// or fully hidden this frame) - callers should fall back to searchBox itself.
std::optional<CBox> computeAlphaContentBox(SP<Render::IFramebuffer>& probeFramebuffer,
                                            SP<Render::IFramebuffer> alphaSource,
                                            CBox searchBox, float alphaThreshold);

// Remaps a sample layout captured for capturedBox so its UVs instead address
// contentBox, a sub-rect of it in the same buffer/pixel space. validMin/Max
// are already absolute texture-space bounds and pass through unchanged.
SSampleLayout narrowSampleLayout(const SSampleLayout& base, const CBox& capturedBox, const CBox& contentBox);

} // namespace GlassRenderer
