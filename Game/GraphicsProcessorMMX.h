#pragma once

#include "GraphicsProcessor.h"

// Historical class/file name; the sole implementation uses C++ and SSE2.
class CGraphicsProcessorMMX : public CGraphicsProcessor
{
public:
    CGraphicsProcessorMMX() = default;
    ~CGraphicsProcessorMMX() override = default;

    void SetupTextureFormat(ETextureFormats format, DWORD dwAMask, DWORD dwRMask, DWORD dwGMask,
                            DWORD dwBMask) override;
    void Convert32To16Bit(LPWORD pDst, LPDWORD pSrc, ETextureFormats format, int nPix) override;
    void PutOneLayer(LPDWORD pDst, LPDWORD pSrc, int nPix, bool bRLE = false) override;
    void PrecalculateAsLayer(LPDWORD pSrc, int nPix) override;
    void GenerateMipmapLevel(LPDWORD pDst, LPDWORD pSrc, int iDstSize) override;
    void RasterizeTriangle(SShadowTextureRenderParam &strp) override;
    void RasterizeTriangleWithAlpha(SShadowTextureRenderParam &strp) override;
    void DecompressDXT(LPWORD pDst, LPBYTE pSrc, EDXTDecodeTypes decodeType, int nSizeX,
                       int nSizeY) override;
    void BlurVertical(LPWORD pSrc, int nSizeX, int nSizeY) override;
    void BlurHorizontal(LPWORD pSrc, int nSizeX, int nSizeY) override;
};
