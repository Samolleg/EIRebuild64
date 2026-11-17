#pragma once
// Camp item icons (camp_inventory.cpp): one icon's canvas decoded from the
// game's resource archives. Game-free: the x86 icon workers (standard mode)
// and the x64 host's icon worker (core mode, Host/core_camp.cpp) decode with
// the same code, so both produce the same pixels and alpha facts.
#include "engine_bridge.h"
#include "owned_resource_store.h"
#include "renderer.h"

#include <string>

namespace owned {

// The admitted addon allocator gives geometry-facing icon families different
// canvas sizes. Keep those sizes and bottom alignment; never resample an icon.
unsigned camp_icon_canvas(const std::string& name, unsigned variant) noexcept;

// Resolves `name` (its .x4/.x2 variants first when high_resolution), checks
// the admitted BGRA canvas contract and decodes it into `image` (canvas x
// canvas, 32-bit BGRA, bottom-aligned, quantised exactly as the native 1555
// surface and expanded as the normal upload expands it) and `description`
// (the 1555 surface the native binding has, with its alpha facts). Returns
// an empty string on success, else the reason. Throws on allocation failure.
// An icon no mounted archive holds under any of those names decodes as the
// game's own canvas for it, a 256 opaque white square (`placeholder` set).
// basis (display pixels the content's larger side must keep, with one
// level of margin) or factor (canvas texels per home texel, a power of two)
// selects the icon's own mip level instead of level 0: image is then the
// canvas reduced by 2^level (level_out), description stays the game's full
// canvas and carries no cut mask. Both 0: the full canvas, as before.
std::string decode_camp_icon(const ResourceStore& store, const std::string& name, bool high_resolution,
                             SurfaceImage& image, OwnedVisualSurface& description, bool* placeholder = nullptr,
                             unsigned basis = 0, unsigned factor = 0, unsigned* level_out = nullptr);

}  // namespace owned
