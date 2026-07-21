#pragma once

#include "PluginConfig.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

// ── All default values in one place ──────────────────────────────────────────

// Per-theme hardcoded fallbacks for settings that naturally differ between
// themes. Used as the final tier in the resolution chain.
struct SThemeDefaults {
    float brightness;
    float contrast;
    float saturation;
    float vibrancy;
    float vibrancyDarkness;
    float adaptiveDim;
    float adaptiveBoost;
};

inline constexpr SThemeDefaults DARK_THEME_DEFAULTS  = {0.82f, 0.90f, 0.80f, 0.15f, 0.0f, 0.4f, 0.0f};
inline constexpr SThemeDefaults LIGHT_THEME_DEFAULTS = {1.12f, 0.92f, 0.85f, 0.12f, 0.0f, 0.0f, 0.4f};

// Global config defaults — values registered with Hyprlang for effect settings.
// Theme-sensitive settings (brightness, contrast, etc.) use SENTINEL at global
// level so they fall through to the per-theme hardcoded defaults above.
namespace GlobalDefaults {
    // Blur kernel radius = strength * 12px, applied blur_iterations times
    // (half-res above the downscale threshold). Walked down from the
    // original 2.0 -> 1.3 -> 0.8 by user preference: 0.8 is a light frost
    // that keeps shapes readable through the glass; ~0.55 is nearly clear
    // if even less is ever wanted.
    inline constexpr float   BLUR_STRENGTH        = 0.8f;
    inline constexpr int64_t BLUR_ITERATIONS      = 3;
    // Tuned live (hyprctl keyword iteration, 2026-07-17): a thin, strong
    // bend band per user preference - 1.2 -> 60px max pull (the full sample
    // padding; the falloff makes the actual reach shorter), 0.05 keeps the
    // band tight, which also steepens the displacement gradient so the bend
    // reads stronger. Aberration walked down further on 2026-07-18 (0.6 ->
    // 0.5 -> 0.4) by live user preference: 0.4 keeps a visible rim fringe
    // without it reading as blotchy.
    inline constexpr float   REFRACTION_STRENGTH  = 1.2f;
    inline constexpr float   CHROMATIC_ABERRATION = 0.4f;
    inline constexpr float   FRESNEL_STRENGTH     = 0.6f;
    inline constexpr float   SPECULAR_STRENGTH    = 0.8f;
    inline constexpr float   GLASS_OPACITY        = 1.0f;
    inline constexpr float   EDGE_THICKNESS       = 0.05f;
    inline constexpr int64_t TINT_COLOR           = 0x8899aa22;
    inline constexpr float   LENS_DISTORTION      = 0.5f;

    // Layers only. Tuned live (2026-07-19) chasing a hard crease where two
    // separate silhouette lobes (e.g. adjacent bar pill modules) touch -
    // see refractionDirField's comment in Shaders.hpp.
    // Gradient finite-difference step, in JFA field texels (roughly real
    // pixels - see DISTANCE_FIELD_RESOLUTION below). Wider blends the seam
    // between separate lobes more smoothly at the cost of a little
    // directional sharpness at real concave corners.
    inline constexpr float   LAYERS_REFRACTION_BLEND           = 14.0f;
    // Cap on the JFA distance field's longer side, in texels. Higher gives
    // crisper edges/refraction on large layers (walked 256 -> 512 -> 1024
    // chasing choppy/staircased edges) at more GPU cost per frame.
    inline constexpr int64_t LAYERS_DISTANCE_FIELD_RESOLUTION  = 1024;

    // Windows only. Scales edge_thickness/refraction_strength on an
    // unfocused window (1.0 = no change from the focused look). Animated
    // (see FOCUS_ANIM_TIME_CONSTANT in WindowGlassState.cpp) so focus
    // changes ease rather than pop. Global-only (not preset/theme
    // resolved) - a secondary accent on top of whatever base look a
    // preset/theme already resolved to, not itself a themeable dimension.
    inline constexpr float   UNFOCUSED_EDGE_THICKNESS_SCALE = 0.6f;
    inline constexpr float   UNFOCUSED_REFRACTION_SCALE     = 0.65f;
} // namespace GlobalDefaults

// ── Built-in presets ─────────────────────────────────────────────────────────
// To add a new built-in preset: define a make*() function and register it
// in getAll().

namespace BuiltInPresets {

inline SCustomPreset makeHighContrast() {
    SCustomPreset p;
    p.name = "high_contrast";

    p.shared.blurStrength        = 1.2f;
    p.shared.blurIterations      = 2;
    p.shared.lensDistortion      = 0.5f;
    p.shared.refractionStrength  = 1.2f;
    p.shared.chromaticAberration = 0.25f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.8f;
    p.shared.glassOpacity        = 1.0f;
    p.shared.edgeThickness       = 0.06f;

    p.dark.brightness         = 0.82f;
    p.dark.contrast           = 1.14f;
    p.dark.saturation         = 0.92f;
    p.dark.vibrancy           = 0.5f;
    p.dark.vibrancyDarkness   = 0.2f;
    p.dark.adaptiveDim        = 0.25f;
    p.dark.tintColor          = 0x02142aa9;

    p.light.brightness         = 1.0f;
    p.light.contrast           = 0.92f;
    p.light.saturation         = 0.8f;
    p.light.vibrancy           = 0.12f;
    p.light.vibrancyDarkness   = 5.0f;
    p.light.adaptiveBoost      = 0.15f;
    p.light.tintColor          = 0xc2cddb33;

    return p;
}

inline SCustomPreset makeSubtle() {
    SCustomPreset p;
    p.name = "subtle";

    p.shared.blurStrength        = 1.0f;
    p.shared.refractionStrength  = 0.3f;
    p.shared.chromaticAberration = 0.2f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.4f;

    return p;
}

inline SCustomPreset makeClear() {
    SCustomPreset p;
    p.name = "clear";

    p.shared.blurStrength        = 0.0f;
    p.shared.refractionStrength  = 0.3f;
    p.shared.chromaticAberration = 0.2f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.4f;

    return p;
}

inline SCustomPreset makeGlass() {
    SCustomPreset p;
    p.name = "glass";

    p.shared.blurStrength        = 1.0f;
    p.shared.blurIterations      = 2;
    p.shared.lensDistortion      = 0.3f;
    p.shared.refractionStrength  = 8.0f;
    p.shared.chromaticAberration = 0.5f;
    p.shared.fresnelStrength     = 0.4f;
    p.shared.specularStrength    = 0.8f;
    p.shared.glassOpacity        = 1.0f;
    p.shared.edgeThickness       = 0.06f;
    p.shared.tintColor           = 0xffffff00;

    p.dark.adaptiveDim           = 0.3f;
    p.light.adaptiveBoost        = 0.3f;

    return p;
}

inline std::unordered_map<std::string, SCustomPreset> getAll() {
    std::unordered_map<std::string, SCustomPreset> presets;

    auto add = [&](SCustomPreset p) { presets[p.name] = std::move(p); };

    add(makeHighContrast());
    add(makeSubtle());
    add(makeClear());
    add(makeGlass());

    return presets;
}

} // namespace BuiltInPresets
