#pragma once

namespace owned {

struct CameraQuaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
static_assert(sizeof(CameraQuaternion) == sizeof(float) * 4,
              "camera quaternion layout must remain four packed floats");

constexpr float kCameraMouseRotationMultiplier = 2.0f;
// Approved response: the accepted Legends input gain (0.0015 * 2). The
// verified caller contributes 500/300 and rotation contributes 2, yielding
// 0.01 radians per normalized vertical mouse unit on every supported profile.
constexpr float kCameraMousePitchInputGain = 0.003f;

// Applies one consumed horizontal mouse displacement. The input is already
// scaled by the supported executable's camera settings and must not be
// integrated again by frame or simulation time.
void apply_camera_mouse_yaw(CameraQuaternion& orientation,
                            float yaw_delta) noexcept;

// Applies one consumed vertical mouse displacement to the engine-owned pitch
// angle. Its legacy settings gain has been replaced with the owned pitch gain.
// Bounds are the active camera mode's already-converted radian limits.
// Persistent keyboard pitch velocity is deliberately outside this contract.
void apply_camera_mouse_pitch(float& pitch_angle, float pitch_delta,
                              float minimum_pitch,
                              float maximum_pitch) noexcept;

// Sub-unit remainder of the RMB displacement normalization, in
// virtual-unit * screen-dimension units. Always smaller than one virtual unit.
struct CameraMouseCarry {
    long long x = 0;
    long long y = 0;
};

// Replaces the supported executable's integer normalization of the raw mouse
// displacement (x * 800 / width, y * 600 / height, truncated toward zero). The
// stock handler truncates each consumed sample and then discards the raw
// accumulator, so small per-frame displacements vanish and the loss grows with
// frame rate. The remainder is carried into the next sample instead, so the
// summed output no longer depends on how the input was grouped.
void normalize_camera_mouse_delta(int& x, int& y, int width, int height,
                                  CameraMouseCarry& carry) noexcept;

} // namespace owned
