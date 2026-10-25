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
std::string decode_camp_icon(const ResourceStore& store, const std::string& name, bool high_resolution,
                             SurfaceImage& image, OwnedVisualSurface& description);

}  // namespace owned
