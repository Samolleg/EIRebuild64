#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
#include "x64_remote_device.h"
#include "x64_remote_surfaces.h"
namespace x64remote {
bool remote_requested() noexcept { return false; }
bool remote_wanted() noexcept { return false; }
bool remote_active() noexcept { return false; }
bool core_requested() noexcept { return false; }
bool core_active() noexcept { return false; }
const char* host_unavailable_reason() noexcept { return "not supported by the embedded renderer"; }
std::uint64_t host_private_bytes() noexcept { return 0; }
const char* remote_notice() noexcept { return nullptr; }
const char* remote_lost_reason() noexcept { return nullptr; }
HRESULT create_remote_device(HWND, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**) noexcept { return E_NOTIMPL; }
void declare_map_range(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT) noexcept {}
#if !defined(EID3D11_EMBEDDED)
bool dlss_caps(DlssCaps& out) noexcept { out={}; return false; }
bool dlss_render_size(UINT,UINT,int,UINT&,UINT&) noexcept { return false; }
void dlss_release() noexcept {}
#endif // embedded lifecycle/optimal settings use owned::DlssBackend
// Existing frame entry remains unconnected until X64DLSS02; no NGX Evaluate here.
HRESULT dlss_evaluate(ID3D11DeviceContext*,const DlssEvaluate&) noexcept { return E_NOTIMPL; }
bool host_presented() noexcept { return false; }
void surface_write(bool,const SurfaceWrite&) noexcept {}
void surface_copy(bool,ID3D11Texture2D*,ID3D11ShaderResourceView*,ID3D11Texture2D*,bool,bool) noexcept {}
std::uint64_t surface_close_batch() noexcept { return 0; }
void surface_commit(std::uint64_t) noexcept {}
}
#endif
