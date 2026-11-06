#pragma once
#ifdef EID3D11_EMBEDDED
namespace owned {
// Supplied by the source host; never persisted in renderer configuration.
bool embedded_agent_background() noexcept;
}
#endif
