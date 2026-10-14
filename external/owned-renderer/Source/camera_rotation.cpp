#include "camera_rotation.h"

#include <cmath>

namespace owned {

void apply_camera_mouse_yaw(CameraQuaternion& orientation,
                            float yaw_delta) noexcept {
    const float half_angle =
        yaw_delta * kCameraMouseRotationMultiplier * 0.5f;
    const float sine = std::sin(half_angle);
    const float cosine = std::cos(half_angle);

    const CameraQuaternion current = orientation;
    orientation.w = current.w * cosine - current.z * sine;
    orientation.x = current.x * cosine + current.y * sine;
    orientation.y = current.y * cosine - current.x * sine;
    orientation.z = current.z * cosine + current.w * sine;
}

void apply_camera_mouse_pitch(float& pitch_angle, float pitch_delta,
                              float minimum_pitch,
                              float maximum_pitch) noexcept {
    const float next =
        pitch_angle + pitch_delta * kCameraMouseRotationMultiplier;
    if (next < minimum_pitch) pitch_angle = minimum_pitch;
    else if (next > maximum_pitch) pitch_angle = maximum_pitch;
    else pitch_angle = next;
}

void normalize_camera_mouse_delta(int& x, int& y, int width, int height,
                                  CameraMouseCarry& carry) noexcept {
    if (width <= 0 || height <= 0) {
        x = y = 0;
        carry = {};
        return;
    }
    const long long scaled_x = static_cast<long long>(x) * 800 + carry.x;
    const long long scaled_y = static_cast<long long>(y) * 600 + carry.y;
    const long long out_x = scaled_x / width;
    const long long out_y = scaled_y / height;
    carry.x = scaled_x - out_x * width;
    carry.y = scaled_y - out_y * height;
    x = static_cast<int>(out_x);
    y = static_cast<int>(out_y);
}

} // namespace owned
