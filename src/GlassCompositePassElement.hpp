#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <memory>

class CWindowGlassState;

class CGlassCompositePassElement : public IPassElement {
  public:
    struct SGlassCompositeData {
        std::shared_ptr<CWindowGlassState> windowState;
        float                              alpha = 1.0f;
    };

    explicit CGlassCompositePassElement(const SGlassCompositeData& data);
    ~CGlassCompositePassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;

    [[nodiscard]] const char* passName() override { return "CGlassCompositePassElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    SGlassCompositeData m_data;
};
