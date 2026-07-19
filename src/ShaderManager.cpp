#include "ShaderManager.hpp"
#include "Globals.hpp"
#include "Shaders.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>

std::string CShaderManager::loadShaderSource(const char* fileName) {
    if (SHADERS.contains(fileName))
        return SHADERS.at(fileName);

    const std::string message = std::format("[{}] Failed to load shader: {}", PLUGIN_NAME, fileName);
    HyprlandAPI::addNotification(PHANDLE, message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error(message);
}

bool CShaderManager::compileGlassShader() {
    if (!glassShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("liquidglass.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile glass shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = glassShader->program();

    glassUniforms.refractionStrength  = glGetUniformLocation(program, "refractionStrength");
    glassUniforms.chromaticAberration = glGetUniformLocation(program, "chromaticAberration");
    glassUniforms.fresnelStrength     = glGetUniformLocation(program, "fresnelStrength");
    glassUniforms.specularStrength    = glGetUniformLocation(program, "specularStrength");
    glassUniforms.glassOpacity        = glGetUniformLocation(program, "glassOpacity");
    glassUniforms.edgeThickness       = glGetUniformLocation(program, "edgeThickness");
    glassUniforms.uvOffset            = glGetUniformLocation(program, "uvOffset");
    glassUniforms.uvScale             = glGetUniformLocation(program, "uvScale");
    glassUniforms.tintColor           = glGetUniformLocation(program, "tintColor");
    glassUniforms.tintAlpha           = glGetUniformLocation(program, "tintAlpha");
    glassUniforms.lensDistortion      = glGetUniformLocation(program, "lensDistortion");
    glassUniforms.saturation          = glGetUniformLocation(program, "saturation");
    glassUniforms.vibrancyDarkness    = glGetUniformLocation(program, "vibrancyDarkness");
    glassUniforms.adaptiveDim         = glGetUniformLocation(program, "adaptiveDim");
    glassUniforms.adaptiveBoost       = glGetUniformLocation(program, "adaptiveBoost");
    glassUniforms.sharpTex            = glGetUniformLocation(program, "sharpTex");
    glassUniforms.validBounds         = glGetUniformLocation(program, "validBounds");
    glassUniforms.refractionDirSign   = glGetUniformLocation(program, "refractionDirSign");
    glassUniforms.maskTex             = glGetUniformLocation(program, "maskTex");
    glassUniforms.useMask             = glGetUniformLocation(program, "useMask");
    glassUniforms.maskUVOffset        = glGetUniformLocation(program, "maskUVOffset");
    glassUniforms.maskUVScale         = glGetUniformLocation(program, "maskUVScale");
    glassUniforms.maskAlphaThreshold  = glGetUniformLocation(program, "maskAlphaThreshold");
    glassUniforms.distField           = glGetUniformLocation(program, "distField");
    glassUniforms.distFieldSize       = glGetUniformLocation(program, "distFieldSize");
    glassUniforms.useDistanceField    = glGetUniformLocation(program, "useDistanceField");

    return true;
}

bool CShaderManager::compileBlurShader() {
    if (!blurShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("gaussianblur.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile blur shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = blurShader->program();

    blurUniforms.direction = glGetUniformLocation(program, "direction");
    blurUniforms.radius    = glGetUniformLocation(program, "blurRadius");

    return true;
}

bool CShaderManager::compileJfaSeedShader() {
    if (!jfaSeedShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("jfaseed.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile JFA seed shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = jfaSeedShader->program();

    jfaSeedUniforms.maskUVOffset       = glGetUniformLocation(program, "maskUVOffset");
    jfaSeedUniforms.maskUVScale        = glGetUniformLocation(program, "maskUVScale");
    jfaSeedUniforms.maskAlphaThreshold = glGetUniformLocation(program, "maskAlphaThreshold");
    jfaSeedUniforms.fieldSize          = glGetUniformLocation(program, "fieldSize");

    return true;
}

bool CShaderManager::compileJfaStepShader() {
    if (!jfaStepShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("jfastep.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile JFA step shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = jfaStepShader->program();

    jfaStepUniforms.prevBuf   = glGetUniformLocation(program, "prevBuf");
    jfaStepUniforms.stepPx    = glGetUniformLocation(program, "stepPx");
    jfaStepUniforms.fieldSize = glGetUniformLocation(program, "fieldSize");

    return true;
}

bool CShaderManager::compileJfaFinalizeShader() {
    if (!jfaFinalizeShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("jfafinalize.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile JFA finalize shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = jfaFinalizeShader->program();

    jfaFinalizeUniforms.prevBuf        = glGetUniformLocation(program, "prevBuf");
    jfaFinalizeUniforms.fieldSize      = glGetUniformLocation(program, "fieldSize");
    jfaFinalizeUniforms.pixelsPerTexel = glGetUniformLocation(program, "pixelsPerTexel");

    return true;
}

void CShaderManager::initializeIfNeeded() {
    if (m_initialized)
        return;

    if (!compileGlassShader())
        return;

    if (!compileBlurShader())
        return;

    if (!compileJfaSeedShader())
        return;

    if (!compileJfaStepShader())
        return;

    if (!compileJfaFinalizeShader())
        return;

    m_initialized = true;
}

void CShaderManager::destroy() noexcept {
    glassShader->destroy();
    blurShader->destroy();
    jfaSeedShader->destroy();
    jfaStepShader->destroy();
    jfaFinalizeShader->destroy();
    m_initialized = false;
}
