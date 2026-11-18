# The author's shader_embed produces the shared HLSL and per-program DXC rules.
# Run from RendererShaders' OutDir, where --dxil-sources writes dxil/.
DXIL=dxil

all: shared native

!include "$(DXIL)\dxil.mak"

!IFNDEF TOOLCHAIN_STAMP
TOOLCHAIN_STAMP=$(DXC)
!ENDIF
$(DXIL_PROGRAMS): "$(TOOLCHAIN_STAMP)"
$(DXIL)\diag_cs.dxil $(DXIL)\diag_vs.dxil $(DXIL)\diag_ps.dxil $(DXIL)\imgui_vs.dxil $(DXIL)\imgui_ps.dxil: "$(TOOLCHAIN_STAMP)"

shared: $(DXIL_PROGRAMS)

native: $(DXIL)\diag_cs.dxil $(DXIL)\diag_vs.dxil $(DXIL)\diag_ps.dxil $(DXIL)\imgui_vs.dxil $(DXIL)\imgui_ps.dxil "$(HOST_D3D12)\eid3d11_d3d12_pipelines.shipped.list"

$(DXIL)\diag_cs.dxil: "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
    @"$(DXC)" -nologo -HV 2018 -T cs_6_6 -E barsCS -O3 -Qstrip_debug -Qstrip_reflect -Fo $@ "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
$(DXIL)\diag_vs.dxil: "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
    @"$(DXC)" -nologo -HV 2018 -T vs_6_6 -E overlayVS -O3 -Qstrip_debug -Qstrip_reflect -Fo $@ "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
$(DXIL)\diag_ps.dxil: "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
    @"$(DXC)" -nologo -HV 2018 -T ps_6_6 -E overlayPS -O3 -Qstrip_debug -Qstrip_reflect -Fo $@ "$(HOST_D3D12)\d3d12_diagnostic.hlsl"
$(DXIL)\imgui_vs.dxil: "$(HOST_D3D12)\d3d12_overlay.hlsl"
    @"$(DXC)" -nologo -HV 2018 -T vs_6_6 -E imguiVS -O3 -Qstrip_debug -Qstrip_reflect -Fo $@ "$(HOST_D3D12)\d3d12_overlay.hlsl"
$(DXIL)\imgui_ps.dxil: "$(HOST_D3D12)\d3d12_overlay.hlsl"
    @"$(DXC)" -nologo -HV 2018 -T ps_6_6 -E imguiPS -O3 -Qstrip_debug -Qstrip_reflect -Fo $@ "$(HOST_D3D12)\d3d12_overlay.hlsl"
