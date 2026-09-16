#ifndef __MMPFORMAT_H__
#define __MMPFORMAT_H__
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
/////////////////////////////////////////////////////////////////////////////////////
// signature
const DWORD MMP_SIGNATURE = 0x00504D4D;
// formats
const DWORD MMP_FORMAT_5650 = 0x5650;
const DWORD MMP_FORMAT_5550 = 0x5550;
const DWORD MMP_FORMAT_5551 = 0x5551;
const DWORD MMP_FORMAT_4444 = 0x4444;
const DWORD MMP_FORMAT_8888 = 0x8888;
const DWORD MMP_FORMAT_DXT1 = 0x31545844;
const DWORD MMP_FORMAT_DXT2 = 0x32545844;
const DWORD MMP_FORMAT_DXT3 = 0x33545844;
const DWORD MMP_FORMAT_DXT4 = 0x34545844;
const DWORD MMP_FORMAT_DXT5 = 0x35545844;
const DWORD MMP_FORMAT_DXTN = 0x00545844;
const DWORD MMP_FORMAT_PAINT = 0x00544E50;
const DWORD MMP_FORMAT_PAINT32 = 0x32544E50;
const DWORD MMP_FORMAT_PNT3 = 0x33544E50;
// bit depthes for appropriate formats
const DWORD MMP_BIT_DEPTH_5650 = 16;
const DWORD MMP_BIT_DEPTH_5551 = 16;
const DWORD MMP_BIT_DEPTH_4444 = 16;
const DWORD MMP_BIT_DEPTH_8888 = 32;
const DWORD MMP_BIT_DEPTH_DXT1 = 4;
const DWORD MMP_BIT_DEPTH_DXT2 = 8;
const DWORD MMP_BIT_DEPTH_DXT3 = 8;
const DWORD MMP_BIT_DEPTH_DXT4 = 8;
const DWORD MMP_BIT_DEPTH_DXT5 = 8;
const DWORD MMP_BIT_DEPTH_PAINT = 24;
const DWORD MMP_BIT_DEPTH_PAINT32 = 32;
const DWORD MMP_BIT_DEPTH_PNT3 = 32;

namespace mmpf {

typedef enum MMP_FORMATS
{
    MMP_SIGNATURE = 0x00504D4D,
    MMP_FORMAT_5650 = 0x5650,
    MMP_FORMAT_5550 = 0x5550,
    MMP_FORMAT_5551 = 0x5551,
    MMP_FORMAT_4444 = 0x4444,
    MMP_FORMAT_8888 = 0x8888,
    MMP_FORMAT_DXT1 = 0x31545844,
    MMP_FORMAT_DXT2 = 0x32545844,
    MMP_FORMAT_DXT3 = 0x33545844,
    MMP_FORMAT_DXT4 = 0x34545844,
    MMP_FORMAT_DXT5 = 0x35545844,
    MMP_FORMAT_DXTN = 0x00545844,
    MMP_FORMAT_PAINT = 0x00544E50,
    MMP_FORMAT_PAINT32 = 0x32544E50,
    MMP_FORMAT_PNT3 = 0x33544E50,
    MMP_BIT_DEPTH_5650 = 16,
    MMP_BIT_DEPTH_5551 = 16,
    MMP_BIT_DEPTH_4444 = 16,
    MMP_BIT_DEPTH_8888 = 32,
    MMP_BIT_DEPTH_DXT1 = 4,
    MMP_BIT_DEPTH_DXT2 = 8,
    MMP_BIT_DEPTH_DXT3 = 8,
    MMP_BIT_DEPTH_DXT4 = 8,
    MMP_BIT_DEPTH_DXT5 = 8,
    MMP_BIT_DEPTH_PAINT = 24,
    MMP_BIT_DEPTH_PAINT32 = 32,
    MMP_BIT_DEPTH_PNT3 = 32,
} MMP_FORMATS;

};
// mmpf
/////////////////////////////////////////////////////////////////////////////////////
struct SMMPFileHeader
{
public:
    // general header information
    DWORD dwSignature;  // see signature constant above
    DWORD dwDimentionX; // x-dimention of the top-level mip object
    DWORD dwDimentionY; // y-dimention of the top-level mip object
    union
    {
        DWORD dwMipMapLevel; // number of mip-map levels
        DWORD dwLinearSize;  // linear size of compresed PNT3 format
    };
    // format info
    // general format info
    DWORD dwFormat;   // format type (see format constants above)
    DWORD dwBitDepth; // bit depth (see bit depth constants above)
    // masks (for RGBA formats it can be applied to each pixel, for FOURCC formats if can be applied
    // for color_0...color_1) alpha mask info
    DWORD dwAMask;  // mask
    DWORD dwAShift; // shift
    DWORD dwABits;  // bits
    // red mask info
    DWORD dwRMask;  // mask
    DWORD dwRShift; // shift
    DWORD dwRBits;  // bits
    // green mask info
    DWORD dwGMask;  // mask
    DWORD dwGShift; // shift
    DWORD dwGBits;  // bits
    // blue mask info
    DWORD dwBMask;  // mask
    DWORD dwBShift; // shift
    DWORD dwBBits;  // bits
    //
    DWORD dwUserDataShift; // number of bytes to shift from the header to get to mmp data
public:
    SMMPFileHeader() { memset(this, 0, sizeof(*this)); }
    //
    int ShiftToMipLevel(int nMipLevel) const;
    LPVOID GetUsedData() const
    {
        return reinterpret_cast<LPVOID>(reinterpret_cast<int>(this) + sizeof(*this));
    }
    LPVOID GetMipLevelData(int nMipLevel) const
    {
        return reinterpret_cast<LPVOID>(reinterpret_cast<int>(GetUsedData()) +
                                        ShiftToMipLevel(nMipLevel));
    }
    // default pixel format initialization
    void InitializeDefaultFormat0000();
    void InitializeDefaultFormat5650();
    void InitializeDefaultFormat5551();
    void InitializeDefaultFormat4444();
    void InitializeDefaultFormat8888();
    void InitializeDefaultFormatPNT2();
    void InitializeDefaultFormatPNT3();
    void InitializeDefaultFormat(DWORD dwFormat);
};
typedef SMMPFileHeader *LPMMPFILEHEADER;
/////////////////////////////////////////////////////////////////////////////////////
inline int SMMPFileHeader::ShiftToMipLevel(int nMipLevel) const
{
    if (nMipLevel == 0)
        return dwUserDataShift;

    nMipLevel--;
    int b = 1 << (2 * (nMipLevel + 1));
    return dwDimentionX * dwDimentionY * dwBitDepth / 8 * 4 * (b - 1) / (3 * b) + dwUserDataShift;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void SMMPFileHeader::InitializeDefaultFormat0000()
{
    dwAMask = 0x00000000;
    dwABits = 0x00000000;
    dwAShift = 0x00000000;
    dwRMask = 0x00000000;
    dwRBits = 0x00000000;
    dwRShift = 0x00000000;
    dwGMask = 0x00000000;
    dwGBits = 0x00000000;
    dwGShift = 0x00000000;
    dwBMask = 0x00000000;
    dwBBits = 0x00000000;
    dwBShift = 0x00000000;
    dwBitDepth = 0;
    dwFormat = 0;
}
inline void SMMPFileHeader::InitializeDefaultFormat5650()
{
    dwAMask = 0x00000000;
    dwABits = 0x00000000;
    dwAShift = 0x00000000;
    dwRMask = 0x0000F800;
    dwRBits = 0x00000005;
    dwRShift = 0x0000000B;
    dwGMask = 0x000007E0;
    dwGBits = 0x00000006;
    dwGShift = 0x00000005;
    dwBMask = 0x0000001F;
    dwBBits = 0x00000005;
    dwBShift = 0x00000000;
    dwBitDepth = MMP_BIT_DEPTH_5650;
    dwFormat = MMP_FORMAT_5650;
}
inline void SMMPFileHeader::InitializeDefaultFormat5551()
{
    dwAMask = 0x00008000;
    dwABits = 0x00000001;
    dwAShift = 0x0000000F;
    dwRMask = 0x00007C00;
    dwRBits = 0x00000005;
    dwRShift = 0x0000000A;
    dwGMask = 0x000003E0;
    dwGBits = 0x00000005;
    dwGShift = 0x00000005;
    dwBMask = 0x0000001F;
    dwBBits = 0x00000005;
    dwBShift = 0x00000000;
    dwBitDepth = MMP_BIT_DEPTH_5551;
    dwFormat = MMP_FORMAT_5551;
}
inline void SMMPFileHeader::InitializeDefaultFormat4444()
{
    dwAMask = 0x0000F000;
    dwABits = 0x00000004;
    dwAShift = 0x0000000C;
    dwRMask = 0x00000F00;
    dwRBits = 0x00000004;
    dwRShift = 0x00000008;
    dwGMask = 0x000000F0;
    dwGBits = 0x00000004;
    dwGShift = 0x00000004;
    dwBMask = 0x0000000F;
    dwBBits = 0x00000004;
    dwBShift = 0x00000000;
    dwBitDepth = MMP_BIT_DEPTH_4444;
    dwFormat = MMP_FORMAT_4444;
}
inline void SMMPFileHeader::InitializeDefaultFormat8888()
{
    dwAMask = 0xFF000000;
    dwABits = 0x00000008;
    dwAShift = 0x00000018;
    dwRMask = 0x00FF0000;
    dwRBits = 0x00000008;
    dwRShift = 0x00000010;
    dwGMask = 0x0000FF00;
    dwGBits = 0x00000008;
    dwGShift = 0x00000008;
    dwBMask = 0x000000FF;
    dwBBits = 0x00000008;
    dwBShift = 0x00000000;
    dwBitDepth = MMP_BIT_DEPTH_8888;
    dwFormat = MMP_FORMAT_8888;
}
inline void SMMPFileHeader::InitializeDefaultFormatPNT2()
{
    InitializeDefaultFormat0000();
    dwBitDepth = MMP_BIT_DEPTH_PAINT32;
    dwFormat = MMP_FORMAT_PAINT32;
}
inline void SMMPFileHeader::InitializeDefaultFormatPNT3()
{
    InitializeDefaultFormat0000();
    dwBitDepth = MMP_BIT_DEPTH_PNT3;
    dwFormat = MMP_FORMAT_PNT3;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void SMMPFileHeader::InitializeDefaultFormat(DWORD dwInitFormat)
{
    switch (dwInitFormat)
    {
    case MMP_FORMAT_5650:
        InitializeDefaultFormat5650();
        break;
    case MMP_FORMAT_5551:
        InitializeDefaultFormat5551();
        break;
    case MMP_FORMAT_4444:
        InitializeDefaultFormat4444();
        break;
    case MMP_FORMAT_8888:
        InitializeDefaultFormat8888();
        break;
    case MMP_FORMAT_PAINT32:
        InitializeDefaultFormatPNT2();
        break;
    case MMP_FORMAT_PNT3:
        InitializeDefaultFormatPNT3();
        break;
    default:
        InitializeDefaultFormat0000();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// convert RGBA bits to MMP format
inline DWORD MMPBits2MMPFormat(const SMMPFileHeader *pHdr)
{
    if ((pHdr->dwRBits == 5) && (pHdr->dwGBits == 6) && (pHdr->dwBBits == 5) &&
        (pHdr->dwABits == 0))
        return MMP_FORMAT_5650;
    else if ((pHdr->dwRBits == 5) && (pHdr->dwGBits == 5) && (pHdr->dwBBits == 5) &&
             (pHdr->dwABits == 0))
        return MMP_FORMAT_5550;
    else if ((pHdr->dwRBits == 5) && (pHdr->dwGBits == 5) && (pHdr->dwBBits == 5) &&
             (pHdr->dwABits == 1))
        return MMP_FORMAT_5551;
    else if ((pHdr->dwRBits == 4) && (pHdr->dwGBits == 4) && (pHdr->dwBBits == 4) &&
             (pHdr->dwABits == 4))
        return MMP_FORMAT_4444;
    else if ((pHdr->dwRBits == 8) && (pHdr->dwGBits == 8) && (pHdr->dwBBits == 8) &&
             (pHdr->dwABits == 8))
        return MMP_FORMAT_8888;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
#endif // __MMPFORMAT_H__
