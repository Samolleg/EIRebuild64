#pragma once
#include <DirectXHelpers.h>

inline void SetDebugObjectNameEx(ID3D11DeviceChild *resource, const std::string &name)
{
    const int MAX_LENGTH = 32;
    char debugNameArray[MAX_LENGTH]{ 0 };
    strncpy_s(debugNameArray, MAX_LENGTH, name.c_str(), _TRUNCATE);
    DirectX::SetDebugObjectName<MAX_LENGTH>(resource, debugNameArray);
}

inline DirectX::XMFLOAT4 BGRAtoFloat4(DWORD value)
{
    int iA = (value >> 24) & 0xff;
    int iR = (value >> 16) & 0xff;
    int iG = (value >> 8) & 0xff;
    int iB = (value) & 0xff;
    return DirectX::XMFLOAT4(iR / 255., iG / 255., iB / 255., iA / 255.);
};