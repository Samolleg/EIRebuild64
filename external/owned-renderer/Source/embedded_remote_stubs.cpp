#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
#include "x64_remote_link.h"
namespace x64remote {
bool core_requested() noexcept { return false; }
bool core_active() noexcept { return false; }
const char* host_unavailable_reason() noexcept { return "not supported by the embedded renderer"; }
std::uint64_t host_private_bytes() noexcept { return 0; }
const char* remote_notice() noexcept { return nullptr; }
const char* remote_lost_reason() noexcept { return nullptr; }
#if !defined(EID3D11_EMBEDDED)
bool dlss_caps(DlssCaps& out) noexcept { out={}; return false; }
bool dlss_render_size(UINT,UINT,int,UINT&,UINT&) noexcept { return false; }
void dlss_release() noexcept {}
#endif // embedded lifecycle/optimal settings use owned::DlssBackend
#ifdef EID3D11_SHADER_EMBED
// The shader producer has no native NGX backend and never evaluates frames.
HRESULT dlss_evaluate(ID3D11DeviceContext*,const DlssEvaluate&) noexcept { return E_NOTIMPL; }
#endif
bool host_presented() noexcept { return false; }
}
#endif
