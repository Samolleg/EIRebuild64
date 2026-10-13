#include "E1RendererFacadeCheck.h"
#include <cstdio>

int main() {
    std::puts("E1RendererFacadeWarpCheck: entry");
    std::fflush(stdout);
    const int result = RunE1RendererFacadeWarpCheck();
    std::printf("E1RendererFacadeWarpCheck: result=%d\n", result);
    return result;
}
