#include "stdafx.h"
#include "ConstantBuffer.h"
#include "../Light.h"

bool CConstantBuffers::Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device,
                                 Microsoft::WRL::ComPtr<ID3D11DeviceContext> &deviceContext)
{
    IfFailedThrowHR(cb_vs_vertexshader.Initialize(device.Get(), deviceContext.Get()));
    IfFailedThrowHR(cb_vs_vertexlights.Initialize(device.Get(), deviceContext.Get()));
    IfFailedThrowHR(cb_ps_pixelshader.Initialize(device.Get(), deviceContext.Get()));
    return true;
}

CLight11Packed::CLight11Packed(CLight &src)
{
    rd = src.GetRespectDirection();
    rcpr_range2 = src.GetRcprRange2();
    dcMaterialDiffuse = src.GetMaterialDiffuse();
    respectCP = src.GetRespectCP();
    junk = 999777;
    position = src.GetCP();
    D3DVECTOR dir = src.GetDirection();
    direction = { dir.x, dir.y, dir.z };
}
