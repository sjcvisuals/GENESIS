#pragma once

#include <cstdint>
#include <string>

namespace genesis {

enum class VisionEffect : std::uint8_t {
    PersonMatteRvm = 0,
    DepthMap = 1,
    BiRefNetMatte = 2,
    Saliency = 3,
    HumanParse = 4,
    ColourImage = 5,   // style/enhance image-out
    OpticalFlow = 6,   // needs decoder — unsupported yet
    FaceLandmarks = 7, // needs decoder — unsupported yet
    ObjectDetect = 8,  // needs decoder — unsupported yet
    CustomOnnx = 9,    // user model path; map or colour auto
};

enum class VisionResultAs : std::uint8_t { Alpha = 0, Greyscale = 1, Both = 2 };

enum class VisionActivation : std::uint8_t { Auto = 0, None = 1, Sigmoid = 2, MinMax = 3 };

enum class VisionNorm : std::uint8_t { ZeroToOne = 0, ImageNet = 1, MinusOneToOne = 2 };

const char* visionEffectLabel(VisionEffect e);
const char* visionResultAsLabel(VisionResultAs r);
bool visionEffectNeedsDecoder(VisionEffect e); // true for flow/face/detect
bool visionEffectIsColourOut(VisionEffect e);
// Default asset relative path under assets/vision/ or assets/matting/
const char* visionEffectDefaultModel(VisionEffect e);
VisionActivation visionEffectDefaultActivation(VisionEffect e);
VisionNorm visionEffectDefaultNorm(VisionEffect e);
int visionEffectDefaultSize(VisionEffect e); // 0 = use model fixed size

} // namespace genesis
