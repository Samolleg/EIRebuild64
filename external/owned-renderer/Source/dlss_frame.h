#pragma once
// DLSS Super Resolution, renderer side (the evaluation runs in the x64 host,
// x64_remote_device.h). Small pure helpers shared by renderer.cpp and the
// settings menu.
#include <array>
#include <cmath>
#include <cstdio>

namespace owned {

// Player options (settings_version 3): upscaler, the method (0 off: native
// world size and the render scale; 1 NVIDIA DLSS; 2 AMD FSR 2,
// fsr2_upscaler.h), and upscaler_quality, its preset (0 Quality,
// 1 Balanced, 2 Performance, 3 Ultra Performance, 4 native AA: DLAA or
// FSR Native AA). upscale_sharpness is the one RCAS strength after either.
constexpr int kUpscalerOff = 0, kUpscalerDlss = 1, kUpscalerFsr = 2;
constexpr int kUpscalerMethodMaximum = 2;
constexpr int kUpscalerQualityMaximum = 4;

// (method, quality) -> NGX mode of the host API (0 perf, 1 balanced,
// 2 quality, 3 ultra perf, 4 DLAA); -1: off or not DLSS.
inline int dlss_ngx_mode(int method, int quality) noexcept {
    static const int modes[] = {2, 1, 0, 3, 4};
    return method == kUpscalerDlss && quality >= 0 && quality <= kUpscalerQualityMaximum ? modes[quality] : -1;
}

// (method, quality) -> FSR mode 0 Quality .. 4 Native AA; -1: not FSR.
inline int fsr_mode_of(int method, int quality) noexcept {
    return method == kUpscalerFsr && quality >= 0 && quality <= kUpscalerQualityMaximum ? quality : -1;
}

// The single list of builds before settings_version 3 (ini key dlss_mode):
// 0 off; DLSS 1 Quality .. 4 Ultra Performance, 5 DLAA; FSR 6 Quality ..
// 9 Ultra Performance, 10 Native AA. Read once by the settings migration.
inline void legacy_upscaling_choice(int legacy, int& method, int& quality) noexcept {
    method = kUpscalerOff; quality = 0;
    if (legacy >= 1 && legacy <= 5) { method = kUpscalerDlss; quality = legacy - 1; }
    else if (legacy >= 6 && legacy <= 10) { method = kUpscalerFsr; quality = legacy - 6; }
}
// The FSR 2 scaling ratio of each mode (output / render, per side).
inline float fsr_upscale_ratio(int mode) noexcept {
    static const float ratios[] = {1.5f, 1.7f, 2.f, 3.f, 1.f};
    return mode >= 0 && mode < 5 ? ratios[mode] : 1.f;
}
// The FSR render size of an output side (upstream truncation, at least 1).
inline unsigned fsr_render_extent(unsigned output, int mode) noexcept {
    const unsigned extent = unsigned(float(output) / fsr_upscale_ratio(mode));
    return extent ? extent : 1u;
}

// Near and far plane distances and tan(vertical fov / 2) of a perspective
// world_to_clip (row_major, mul(M, v), D3D depth 0..1): row 3 is the view
// depth axis (scaled by the view's uniform scale k), row 2 = A * row 3 +
// B * (0,0,0,1), row 1 = sy * k * view up (plus any off-centre share of
// row 3). False when the matrix is not such a projection.
inline bool upscaler_camera_from_clip(const float* m, float& near_plane, float& far_plane,
                                      float& tan_half_fov_y) noexcept {
    const float* r1 = m + 4; const float* r2 = m + 8; const float* r3 = m + 12;
    const float k2 = r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2];
    if (!(k2 > 1e-20f)) return false;
    const float a = (r2[0] * r3[0] + r2[1] * r3[1] + r2[2] * r3[2]) / k2;
    const float b = r2[3] - a * r3[3];
    if (!(a > 1.f) || !(b < 0.f)) return false;  // A = f / (f - n), B = -n f / (f - n)
    const float k = std::sqrt(k2);
    near_plane = -b / a / k;
    far_plane = b / (1.f - a) / k;
    const float along = (r1[0] * r3[0] + r1[1] * r3[1] + r1[2] * r3[2]) / k2;
    const float ux = r1[0] - along * r3[0], uy = r1[1] - along * r3[1], uz = r1[2] - along * r3[2];
    const float sy = std::sqrt(ux * ux + uy * uy + uz * uz) / k;
    if (!(sy > 1e-6f) || !(near_plane > 0.f) || !(far_plane > near_plane)) return false;
    tan_half_fov_y = 1.f / sy;
    return true;
}

// Radical inverse in base `base` of index (index >= 1): the Halton sequence.
inline float dlss_halton(unsigned index, unsigned base) noexcept {
    float fraction = 1.f, result = 0.f;
    while (index) {
        fraction /= float(base);
        result += fraction * float(index % base);
        index /= base;
    }
    return result;
}

// Jitter phases: about 8 x (output / render)^2 (NGX guidance), at least 8.
inline unsigned dlss_jitter_phases(unsigned output_width, unsigned render_width) noexcept {
    if (!render_width) return 8;
    const float ratio = float(output_width) / float(render_width);
    const unsigned phases = unsigned(8.f * ratio * ratio + .5f);
    return phases < 8 ? 8 : phases;
}

// The world colour is jittered by a sub-pixel viewport offset (jx, jy in
// world pixels, right/down positive). The same shift as a clip-space
// post-offset of a column-vector world_to_clip (row_major, mul(M, v)), for
// consumers that must project into a jittered image.
inline std::array<float, 16> dlss_jittered_clip(std::array<float, 16> m, float jx, float jy,
                                                unsigned width, unsigned height) noexcept {
    if (!width || !height) return m;
    const float ax = 2.f * jx / float(width), ay = -2.f * jy / float(height);
    for (int c = 0; c < 4; ++c) {
        m[c] += ax * m[12 + c];
        m[4 + c] += ay * m[12 + c];
    }
    return m;
}

// Why the Upscaling option is greyed out (null: DLSS available). Set once
// after device creation, and again if an evaluation fails.
inline char* dlss_unavailable_storage() noexcept {
    static char reason[160] = "needs the 64-bit render host";
    return reason;
}
inline const char* dlss_unavailable_reason() noexcept {
    const char* reason = dlss_unavailable_storage();
    return reason[0] ? reason : nullptr;
}
inline void set_dlss_unavailable_reason(const char* reason) noexcept {
    std::snprintf(dlss_unavailable_storage(), 160, "%s", reason ? reason : "");
}

// Why the FSR entries of the Upscaling list are unavailable (null: FSR can
// run). Set after device creation, and again if FSR fails.
inline char* fsr_unavailable_storage() noexcept {
    static char reason[160] = "no graphics device yet";
    return reason;
}
inline const char* fsr_unavailable_reason() noexcept {
    const char* reason = fsr_unavailable_storage();
    return reason[0] ? reason : nullptr;
}
inline void set_fsr_unavailable_reason(const char* reason) noexcept {
    std::snprintf(fsr_unavailable_storage(), 160, "%s", reason ? reason : "");
}

} // namespace owned
