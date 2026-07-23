#pragma once

#include <GLES3/gl32.h>
#include <hyprland/src/render/Shader.hpp>
#include <string>

struct SGlassUniforms {
    GLint refractionStrength = -1;
    GLint chromaticAberration = -1;
    GLint fresnelStrength = -1;
    GLint specularStrength = -1;
    GLint glassOpacity = -1;
    GLint edgeThickness = -1;
    GLint uvOffset = -1;
    GLint uvScale = -1;
    GLint tintColor = -1;
    GLint tintAlpha = -1;
    GLint lensDistortion = -1;
    GLint saturation = -1;
    GLint vibrancyDarkness = -1;
    GLint adaptiveDim = -1;
    GLint adaptiveBoost = -1;
    GLint sharpTex = -1;
    GLint validBounds = -1;
    GLint refractionDirSign = -1;

    // Layers only: temp FBO surface mask for content-aware glass
    GLint maskTex = -1;
    GLint useMask = -1;
    GLint maskUVOffset = -1;
    GLint maskUVScale = -1;
    GLint maskAlphaThreshold = -1;

    // Layers only: JFA distance field replacing the box SDF for
    // non-rectangular content
    GLint distField = -1;
    GLint distFieldSize = -1;
    GLint useDistanceField = -1;
    GLint gradientStepTexels = -1;
    GLint debugView = -1; // 0=off, 1=direction color wheel, 2=signed-distance heatmap

    // Windows only: focus-reactive edge opacity fade
    GLint edgeOpacityFade = -1;
};

struct SBlurUniforms {
    GLint direction = -1;
    GLint radius    = -1;
};

// Shared by jfaSeedShader/jfaStepShader/jfaFinalizeShader - each only sets
// the fields its own shader source declares, the rest stay at -1 and are
// simply unused.
struct SJfaUniforms {
    GLint maskUVOffset       = -1; // seed, finalize
    GLint maskUVScale        = -1; // seed, finalize
    GLint maskAlphaThreshold = -1; // seed, finalize
    GLint maskTex            = -1; // finalize (signs the distance; seed
                                   // reads the mask via SHADER_TEX instead)
    GLint prevBuf            = -1; // step, finalize
    GLint stepPx             = -1; // step
    GLint pixelsPerTexel     = -1; // finalize
    GLint fieldSize          = -1; // seed + step + finalize
};

class CShaderManager {
  public:
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    void initializeIfNeeded();
    void destroy() noexcept;

    SP<CShader>    glassShader = makeShared<CShader>();
    SGlassUniforms glassUniforms;

    SP<CShader>    blurShader = makeShared<CShader>();
    SBlurUniforms  blurUniforms;

    SP<CShader>    jfaSeedShader = makeShared<CShader>();
    SJfaUniforms   jfaSeedUniforms;

    SP<CShader>    jfaStepShader = makeShared<CShader>();
    SJfaUniforms   jfaStepUniforms;

    SP<CShader>    jfaFinalizeShader = makeShared<CShader>();
    SJfaUniforms   jfaFinalizeUniforms;

  private:
    bool m_initialized = false;

    [[nodiscard]] static std::string loadShaderSource(const char* fileName);
    [[nodiscard]] bool compileGlassShader();
    [[nodiscard]] bool compileBlurShader();
    [[nodiscard]] bool compileJfaSeedShader();
    [[nodiscard]] bool compileJfaStepShader();
    [[nodiscard]] bool compileJfaFinalizeShader();
};
