#pragma once
#include <array>
#include <cmath>

namespace owned {
inline std::array<float,4> fractional_visual_sun(double hour) noexcept {
 // Independently evaluated from the verified retail direction curve. The
 // shipped elevation is 60 degrees at noon, 30 at 04/20 and 75 at midnight.
 const double degrees = hour < 4.0 ? 75.0-11.25*hour :
  hour > 20.0 ? 30.0+11.25*(hour-20.0) : 60.0-3.75*std::abs(hour-12.0);
 constexpr double pi = 3.14159265358979323846;
 const double elevation = degrees*(pi/180.0),azimuth = hour*(pi/12.0);
 const double horizontal = std::cos(elevation);
 // The bridge's lighting vector points toward the sun; the game's stored
 // vector travels away from it. This analytic direction is unit length.
 return {float(-std::cos(azimuth)*horizontal),float(-std::sin(azimuth)*horizontal),
  float(std::sin(elevation)),0.f};
}

} // namespace owned
