#include "build_version.h"

// build-local-x86.mak defines EID3D11_BUILD_COMMIT for this file of the DLL.
#ifndef EID3D11_BUILD_COMMIT
#define EID3D11_BUILD_COMMIT "unknown"
#endif

namespace owned {
extern const char kBuildCommit[] = EID3D11_BUILD_COMMIT;
} // namespace owned
