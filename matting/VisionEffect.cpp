#include "matting/VisionEffect.hpp"

namespace genesis {

const char* visionEffectLabel(VisionEffect effect) {
    switch (effect) {
    case VisionEffect::PersonMatteRvm:
        return "Person Matte (RVM)";
    case VisionEffect::DepthMap:
        return "Depth Map";
    case VisionEffect::BiRefNetMatte:
        return "BiRefNet Matte";
    case VisionEffect::Saliency:
        return "Saliency";
    case VisionEffect::HumanParse:
        return "Human Parse";
    case VisionEffect::ColourImage:
        return "Colour / Style";
    case VisionEffect::OpticalFlow:
        return "Optical Flow (soon)";
    case VisionEffect::FaceLandmarks:
        return "Face Landmarks (soon)";
    case VisionEffect::ObjectDetect:
        return "Object Detect (soon)";
    case VisionEffect::CustomOnnx:
        return "Custom ONNX";
    default:
        return "Unknown";
    }
}

const char* visionResultAsLabel(VisionResultAs result) {
    switch (result) {
    case VisionResultAs::Alpha:
        return "Alpha (key)";
    case VisionResultAs::Greyscale:
        return "Greyscale (view)";
    case VisionResultAs::Both:
        return "Both";
    default:
        return "Unknown";
    }
}

bool visionEffectNeedsDecoder(VisionEffect effect) {
    return effect == VisionEffect::OpticalFlow || effect == VisionEffect::FaceLandmarks ||
           effect == VisionEffect::ObjectDetect;
}

bool visionEffectIsColourOut(VisionEffect effect) {
    return effect == VisionEffect::ColourImage;
}

const char* visionEffectDefaultModel(VisionEffect effect) {
    switch (effect) {
    case VisionEffect::PersonMatteRvm:
        return "assets/matting/rvm_mobilenetv3_fp16.onnx";
    case VisionEffect::DepthMap:
        return "assets/vision/depth_anything_v2_vits.onnx";
    case VisionEffect::BiRefNetMatte:
        return "assets/vision/birefnet_512_fp16.onnx";
    case VisionEffect::Saliency:
        return "assets/vision/u2net_saliency.onnx";
    case VisionEffect::HumanParse:
        return "assets/vision/human_parse.onnx";
    case VisionEffect::ColourImage:
        return "assets/vision/style_mosaic.onnx";
    case VisionEffect::CustomOnnx:
        return "";
    default:
        return "";
    }
}

VisionActivation visionEffectDefaultActivation(VisionEffect effect) {
    switch (effect) {
    case VisionEffect::BiRefNetMatte:
        return VisionActivation::Sigmoid;
    case VisionEffect::DepthMap:
    case VisionEffect::Saliency:
    case VisionEffect::HumanParse:
        return VisionActivation::MinMax;
    case VisionEffect::PersonMatteRvm:
        return VisionActivation::None;
    default:
        return VisionActivation::Auto;
    }
}

VisionNorm visionEffectDefaultNorm(VisionEffect effect) {
    switch (effect) {
    case VisionEffect::PersonMatteRvm:
        return VisionNorm::ZeroToOne;
    case VisionEffect::DepthMap:
    case VisionEffect::BiRefNetMatte:
    case VisionEffect::Saliency:
    case VisionEffect::HumanParse:
    case VisionEffect::ColourImage:
        return VisionNorm::ImageNet;
    default:
        return VisionNorm::ImageNet;
    }
}

int visionEffectDefaultSize(VisionEffect effect) {
    switch (effect) {
    case VisionEffect::BiRefNetMatte:
        return 512;
    case VisionEffect::DepthMap:
        return 518;
    case VisionEffect::Saliency:
        return 320;
    case VisionEffect::HumanParse:
        return 473;
    case VisionEffect::ColourImage:
        return 224;
    default:
        return 0;
    }
}

} // namespace genesis
