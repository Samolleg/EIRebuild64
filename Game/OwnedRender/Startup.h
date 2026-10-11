#pragma once

namespace OwnedRender {
// Called on the startup thread after settings and ModRuntime selection, before
// any renderer, scene or loader worker is created.
void ConfigureStartup();
}
