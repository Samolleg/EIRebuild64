#pragma once

#include "stdafx.h"

enum ETextureFormats
{
    TEX_FORMAT_565,
    TEX_FORMAT_555,
    TEX_FORMAT_1555,
    TEX_FORMAT_4444
};

enum EDXTDecodeTypes
{
    DXT1_TO_565,
    DXT1_TO_555,
    DXT1_TO_1555,
    DXT2_TO_4444,
    DXT3_TO_4444
};

// --------------------------------------------------------------------------------------------
// Contain data pack to rasterize 'shadow' triangle with texture (take in account alpha only)
// --------------------------------------------------------------------------------------------
struct SShadowTextureRenderParam
{
    // complete information set for 1st vertex: geometry (x1, y1) and texture map (tu1, tv1)
    int x1, y1, tu1, tv1;

    // complete information set for 2nd vertex: geometry (x2, y2) and texture map (tu2, tv2)
    int x2, y2, tu2, tv2;

    // complete information set for 3rd vertex: geometry (x3, y3) and texture map (tu3, tv3)
    int x3, y3, tu3, tv3;

    // source texture. NOTE: tu/tv already in texture size coords (not in 0..1)
    LPWORD lpSrcTexture;

    // destination texture (to render shadow in).
    LPWORD lpDstTexture;

    // pitch in BYTES (!) for source texture
    DWORD dwSrcPitch;

    // pitch in BYTES (!) for destination texture
    DWORD dwDstPitch;
};

//--------------------------------------------------------------------------------------------------
// CGraphicsProcessor
//--------------------------------------------------------------------------------------------------
class CGraphicsProcessor
{
public:
    // setup corresponding masks
    virtual void SetupTextureFormat(ETextureFormats format, DWORD dwAMask, DWORD dwRMask,
                                    DWORD dwGMask, DWORD dwBMask) = 0;
    // convert 32-bit to 16-bit in format needed
    // nPix - total number of pixels
    virtual void Convert32To16Bit(LPWORD pDst, LPDWORD pSrc, ETextureFormats format, int nPix) = 0;

    // put one layer on texture w/o painting using alpha only
    // input format: 8888 where RGB are premultiplied and alpha is not
    // input: 32-bit bitmap (with alpha), total number of pixels; output: 32-bit
    virtual void PutOneLayer(LPDWORD pDst, LPDWORD pSrc, int nPix, bool bRLE = false) = 0;

    // convert 32-bit bitmap to format used in previous function
    // input: 32-bit bitmap, total number of pixels
    virtual void PrecalculateAsLayer(LPDWORD pSrc, int nPix) = 0;

    // generate mipmap level
    // iDstSize : width of destination mipmap in pixels
    // input and output: 32-bit
    virtual void GenerateMipmapLevel(LPDWORD pDst, LPDWORD pSrc, int iDstSize) = 0;

    // ---------------------------------------------------------------------------------------------
    // Rasterizing
    // ---------------------------------------------------------------------------------------------

    // rasterize triangle in texture ( for shadows )
    // output: 16-bit
    virtual void RasterizeTriangle(SShadowTextureRenderParam &strp) = 0;
    // rasterize triangle in texture ( for shadows )
    // source texture is 1-bit alpha
    // output: 16-bit
    virtual void RasterizeTriangleWithAlpha(SShadowTextureRenderParam &strp) = 0;

    // ---------------------------------------------------------------------------------------------
    // DXT
    // ---------------------------------------------------------------------------------------------

    // decompress DXT texture ( DXT1, DXT2, DXT3 to 565, 555, 1555, 4444 )
    // input and output: 16-bit
    virtual void DecompressDXT(LPWORD pDst, LPBYTE pSrc, EDXTDecodeTypes decodeType, int nSizeX,
                               int nSizeY) = 0;

    // ---------------------------------------------------------------------------------------------
    // Bluring
    // ---------------------------------------------------------------------------------------------

    // blur shadow texture vertically
    // input and output: 16 bit
    virtual void BlurVertical(LPWORD pSrc, int nSizeX, int nSizeY) = 0;

    // blur shadow texture horizontally
    // input and output: 16 bit
    virtual void BlurHorizontal(LPWORD pSrc, int nSizeX, int nSizeY) = 0;

    // ---------------------------------------------------------------------------------------------
    // Constructors and destructor
    // ---------------------------------------------------------------------------------------------

    CGraphicsProcessor() = default;
    virtual ~CGraphicsProcessor() = default;
    CGraphicsProcessor(CGraphicsProcessor &) = delete;
    CGraphicsProcessor(CGraphicsProcessor &&) = delete;
    CGraphicsProcessor &operator=(CGraphicsProcessor &) = delete;
    CGraphicsProcessor &operator=(CGraphicsProcessor &&) = delete;

    // ---------------------------------------------------------------------------------------------
    // Data
    // ---------------------------------------------------------------------------------------------

protected:
    DWORD dwRMask565 = 0;
    DWORD dwRShift565 = 0;
    DWORD dwGMask565 = 0;
    DWORD dwGShift565 = 0;
    DWORD dwBMask565 = 0;
    DWORD dwBShift565 = 0;
    BOOL bIs565Default = TRUE;

    DWORD dwRMask555 = 0;
    DWORD dwRShift555 = 0;
    DWORD dwGMask555 = 0;
    DWORD dwGShift555 = 0;
    DWORD dwBMask555 = 0;
    DWORD dwBShift555 = 0;
    BOOL bIs555Default = TRUE;

    DWORD dwAMask1555 = 0;
    DWORD dwAShift1555 = 0;
    DWORD dwRMask1555 = 0;
    DWORD dwRShift1555 = 0;
    DWORD dwGMask1555 = 0;
    DWORD dwGShift1555 = 0;
    DWORD dwBMask1555 = 0;
    DWORD dwBShift1555 = 0;
    BOOL bIs1555Default = TRUE;
};

using LPGRAPHICSPROCESSOR = CGraphicsProcessor *;
