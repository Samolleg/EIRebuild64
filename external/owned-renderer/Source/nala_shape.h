#pragma once
#include <memory>

namespace owned {
struct RetainedVisualSample;
namespace skn { struct RuntimeAsset; }

// Returns the unit-local bind asset, rebuilding it when shape inputs change.
std::shared_ptr<const skn::RuntimeAsset> unit_skn_asset(
    const RetainedVisualSample& sample, bool& present);
}
