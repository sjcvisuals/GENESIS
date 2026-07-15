#pragma once

#include <cstring>

namespace genesis {

struct GridDrawPushConstants {
    float model[16];
    float color[4];
};

inline GridDrawPushConstants makeGridDrawPushConstants(const float* model_col_major, float r, float g, float b,
                                                       float a = 1.0f) {
    GridDrawPushConstants pc{};
    if (model_col_major) {
        std::memcpy(pc.model, model_col_major, sizeof(pc.model));
    } else {
        const float identity[16] = {
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        };
        std::memcpy(pc.model, identity, sizeof(pc.model));
    }
    pc.color[0] = r;
    pc.color[1] = g;
    pc.color[2] = b;
    pc.color[3] = a;
    return pc;
}

} // namespace genesis
