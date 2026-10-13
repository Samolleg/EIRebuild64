#include "stdafx.h"
#include "ConstantBuffer.h"
#include "../Light.h"

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
