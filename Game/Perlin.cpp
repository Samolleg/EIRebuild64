#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Perlin.h"
#include "tools.h"
#include "RandomGen.h"
const int CLOUDS_SIZE = 256;
/////////////////////////////////////////////////////////////////////////////////////
CPerlinGenerator::CPerlinGenerator(int nSize)
{
    mnResultSize = nSize;
    int nLayerSize = (nSize >> 3);

    mpNoise = dbgnew int[nLayerSize * nLayerSize];
    mpMap4 = dbgnew BYTE[4 * nLayerSize * nLayerSize];

    mpPic1 = dbgnew BYTE[nSize * nSize];
    mpPic2 = dbgnew BYTE[nSize * nSize];
    mpPic3 = dbgnew BYTE[nSize * nSize];

    mpValTable = dbgnew WORD[256];

    mfCurTime = 0;
    mnCurStep = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CPerlinGenerator::~CPerlinGenerator()
{
    delete[] mpNoise;
    delete[] mpMap4;

    delete[] mpPic1;
    delete[] mpPic2;
    delete[] mpPic3;

    delete[] mpValTable;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPerlinGenerator::Init()
{
    for (int i = 0; i < 16; i++)
        GenerateNoisePicture(mpPic1, i);
    for (int i = 0; i < 16; i++)
        GenerateNoisePicture(mpPic2, i);
}
/////////////////////////////////////////////////////////////////////////////////////
void CPerlinGenerator::GeneratePerlin(LPWORD pDst, float fFrameTime)
{
    mfCurTime += fFrameTime * mfAnimSpeed;
    while (mfCurTime >= 1)
    {
        mfCurTime = mfCurTime - 1;
        GenerateNoisePicture(mpPic3, mnCurStep);
        if (mnCurStep == 15)
        {
            LPBYTE pPicNew = mpPic1;
            mpPic1 = mpPic2;
            mpPic2 = mpPic3;
            mpPic3 = pPicNew;
            mnCurStep = 0;
        }
        else
            mnCurStep++;
    }

    float fInterpTime = (mnCurStep + mfCurTime) / 16.f;
    int a = (int)(fInterpTime * 254.f + 1.f);
    int b = 256 - a;

    LPBYTE pSrc1 = mpPic1;
    LPBYTE pSrc2 = mpPic2;

    int i = mnResultSize * mnResultSize;
    while (i--)
        *(pDst++) = mpValTable[(*(pSrc1++) * b + *(pSrc2++) * a) >> 8];
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
void CCloudsGenerator::SetParams(int nDensity, int nSharpness, float fAnimSpeed)
{
    mnDensity = nDensity;
    mnSharpness = nSharpness;
    mfAnimSpeed = fAnimSpeed;

    float fSharpness = nSharpness / 255.f;

    for (int x = 0; x < 256; x++)
    {
        int y = x - nDensity;
        if (y < 0)
            y = 0;
        BYTE d = (BYTE)(255 - pow(fSharpness, y) * 255);
        // d = x;
        d >>= 4;
        mpValTable[x] = (d << 12) | 0x0fff; // 4444 format
                                            // mpValTable[x] = 0x8000 | (d << 10) | (d << 5) | d;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCloudsGenerator::GenerateNoiseLevel(LPBYTE pMap)
{
    int *pNoise = mpNoise;
    int s1 = random.Get(), s2 = random.Get(), s3 = random.Get();
    //	int s1 = 15731;
    //	int s2 = 789221;
    //	int s3 = 1376312589;

    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 32; j++)
        {
            int n = i + j * 57;
            n = (n << 13) ^ n;
            *(pNoise++) = (n * (n * n * s1 + s2) + s3) & 0x1fffffff;
        }
    pNoise = mpNoise;
    for (int i = 0; i < 1024; i++, pMap++)
    {
        int corners = pNoise[(i + 33) & 0x3ff] + pNoise[(i + 31) & 0x3ff] +
                      pNoise[(i - 33) & 0x3ff] + pNoise[(i - 31) & 0x3ff];
        int sides = pNoise[(i + 32) & 0x3ff] + pNoise[(i + 1) & 0x3ff] + pNoise[(i - 32) & 0x3ff] +
                    pNoise[(i - 1) & 0x3ff];
        //*pMap = BYTE( (pNoise[ i ]) >> 21 ); // no smooth
        *pMap = BYTE(((pNoise[i] >> 2) + (sides >> 3) + (corners >> 4)) >> 21); // smooth
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCloudsGenerator::GenerateNoisePicture(LPBYTE pPic, int nStep)
{
    LPBYTE pMap1 = mpMap4;
    LPBYTE pMap2 = mpMap4 + 1024;
    LPBYTE pMap3 = mpMap4 + 2048;
    LPBYTE pMap4 = mpMap4 + 3072;

    if (nStep == 1)
        GenerateNoiseLevel(pMap1);
    if (nStep == 2)
        GenerateNoiseLevel(pMap2);
    if (nStep == 3)
        GenerateNoiseLevel(pMap3);
    if (nStep == 4)
        GenerateNoiseLevel(pMap4);

    if (nStep > 6 && nStep < 15)
    {
        nStep -= 7;
        pPic += nStep * 256 * 32;

        int pix, num, fm, fn, a1, a2, shift;
        for (int i = nStep * 32; i < (nStep + 1) * 32; i++)
            for (int j = 0; j < 256; j++)
            {
                pix = -7168; // move the mean value

                num = ((i << 2) & 0x3e0) + (j >> 3);
                fm = i & 0x7;
                fn = j & 0x7;
                if ((num + 1) & 0x1f)
                    shift = 1;
                else
                    shift = -31;
                a1 = pMap1[num] * (8 - fn) + pMap1[(num + shift) & 0x3ff] * fn;
                a2 = pMap1[(num + 32) & 0x3ff] * (8 - fn) + pMap1[(num + 32 + shift) & 0x3ff] * fn;
                pix += (a1 * (8 - fm) + a2 * fm);

                num = ((i << 3) & 0x3e0) + ((j >> 2) & 0x1f);
                fm = i & 0x3;
                fn = j & 0x3;
                if ((num + 1) & 0x1f)
                    shift = 1;
                else
                    shift = -31;
                a1 = pMap2[num] * (4 - fn) + pMap2[(num + shift) & 0x3ff] * fn;
                a2 = pMap2[(num + 32) & 0x3ff] * (4 - fn) + pMap2[(num + 32 + shift) & 0x3ff] * fn;
                pix += ((a1 * (4 - fm) + a2 * fm) << 1);

                num = ((i << 4) & 0x3e0) + ((j >> 1) & 0x1f);
                fm = i & 0x1;
                fn = j & 0x1;
                if ((num + 1) & 0x1f)
                    shift = 1;
                else
                    shift = -31;
                a1 = pMap3[num] * (2 - fn) + pMap3[(num + shift) & 0x3ff] * fn;
                a2 = pMap3[(num + 32) & 0x3ff] * (2 - fn) + pMap3[(num + 32 + shift) & 0x3ff] * fn;
                pix += ((a1 * (2 - fm) + a2 * fm) << 2);

                num = ((i << 5) & 0x3e0) + (j & 0x1f);
                pix += (pMap4[num] << 3);

                pix >>= 6;
                if (pix > 255)
                    pix = 255;

                *(pPic++) = (BYTE)(pix);
            }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
void CItemMagicGenerator::SetParams(int nDensity, int nSharpness, float fAnimSpeed)
{
    mnDensity = nDensity;
    mnSharpness = nSharpness;
    mfAnimSpeed = fAnimSpeed;

    float fSharpness = nSharpness / 255.f;

    for (int x = 0; x < 256; x++)
    {
        int y = x - nDensity;
        if (y < 0)
            y = 0;
        BYTE d = (BYTE)(255 - pow(fSharpness, y) * 255);
        // d = x;
        mpValTable[x] = ((d << 8) & 0xf800) | ((d << 3) & 0x07e0) | (d >> 3);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemMagicGenerator::GenerateNoiseLevel(LPBYTE pMap)
{
    int *pNoise = mpNoise;
    int s1 = random.Get(), s2 = random.Get(), s3 = random.Get();
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
        {
            int n = i + j * 57;
            n = (n << 13) ^ n;
            *(pNoise++) = (n * (n * n * s1 + s2) + s3) & 0x1fffffff;
        }
    pNoise = mpNoise;
    for (int i = 0; i < 256; i++, pMap++)
    {
        int corners = pNoise[(i + 17) & 0xff] + pNoise[(i + 15) & 0xff] + pNoise[(i - 17) & 0xff] +
                      pNoise[(i - 15) & 0xff];
        int sides = pNoise[(i + 16) & 0xff] + pNoise[(i + 1) & 0xff] + pNoise[(i - 16) & 0xff] +
                    pNoise[(i - 1) & 0xff];
        *pMap = BYTE(((pNoise[i] >> 2) + (sides >> 3) + (corners >> 4)) >> 21);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemMagicGenerator::GenerateNoisePicture(LPBYTE pPic, int nStep)
{
    LPBYTE pMap1 = mpMap4;
    LPBYTE pMap2 = mpMap4 + 256;
    LPBYTE pMap3 = mpMap4 + 512;
    LPBYTE pMap4 = mpMap4 + 768;

    if (nStep == 1)
        GenerateNoiseLevel(pMap1);
    if (nStep == 2)
        GenerateNoiseLevel(pMap2);
    if (nStep == 3)
        GenerateNoiseLevel(pMap3);
    if (nStep == 4)
        GenerateNoiseLevel(pMap4);

    if (nStep > 6 && nStep < 15)
    {
        nStep -= 7;
        pPic += nStep * 128 * 16;

        int pix, num, fm, fn, a1, a2, shift;
        for (int i = nStep * 16; i < (nStep + 1) * 16; i++)
            for (int j = 0; j < 128; j++)
            {
                pix = -7168; // move the mean value

                num = ((i << 1) & 0xf0) + (j >> 3);
                fm = i & 0x7;
                fn = j & 0x7;
                if ((num + 1) & 0xf)
                    shift = 1;
                else
                    shift = -15;
                a1 = pMap1[num] * (8 - fn) + pMap1[(num + shift) & 0xff] * fn;
                a2 = pMap1[(num + 16) & 0xff] * (8 - fn) + pMap1[(num + 16 + shift) & 0xff] * fn;
                pix += (a1 * (8 - fm) + a2 * fm);

                num = ((i << 2) & 0xf0) + ((j >> 2) & 0xf);
                fm = i & 0x3;
                fn = j & 0x3;
                if ((num + 1) & 0xf)
                    shift = 1;
                else
                    shift = -15;
                a1 = pMap2[num] * (4 - fn) + pMap2[(num + shift) & 0xff] * fn;
                a2 = pMap2[(num + 16) & 0xff] * (4 - fn) + pMap2[(num + 16 + shift) & 0xff] * fn;
                pix += ((a1 * (4 - fm) + a2 * fm) << 1);

                num = ((i << 3) & 0xf0) + ((j >> 1) & 0xf);
                fm = i & 0x1;
                fn = j & 0x1;
                if ((num + 1) & 0xf)
                    shift = 1;
                else
                    shift = -15;
                a1 = pMap3[num] * (2 - fn) + pMap3[(num + shift) & 0xff] * fn;
                a2 = pMap3[(num + 16) & 0xff] * (2 - fn) + pMap3[(num + 16 + shift) & 0xff] * fn;
                pix += ((a1 * (2 - fm) + a2 * fm) << 2);

                num = ((i << 4) & 0xf0) + (j & 0xf);
                pix += (pMap4[num] << 3);

                pix >>= 6;
                if (pix > 255)
                    pix = 255;

                *(pPic++) = (BYTE)(pix);
            }
    }
}
/////////////////////////////////////////////////////////////////////////////////////

void CFOWGenerator::SetParams(int nDensity, int nSharpness, float fAnimSpeed, DWORD dwColor1,
                              DWORD dwColor2)
{
    float r1 = float(RGBA_GETRED(dwColor1)) / 255.f;
    float g1 = float(RGBA_GETGREEN(dwColor1)) / 255.f;
    float b1 = float(RGBA_GETBLUE(dwColor1)) / 255.f;
    float r2 = float(RGBA_GETRED(dwColor2)) / 255.f;
    float g2 = float(RGBA_GETGREEN(dwColor2)) / 255.f;
    float b2 = float(RGBA_GETBLUE(dwColor2)) / 255.f;

    mnDensity = nDensity;
    mnSharpness = nSharpness;
    mfAnimSpeed = fAnimSpeed;

    float fSharpness = nSharpness / 255.f;

    for (int x = 0; x < 256; x++)
    {
        int y = x - nDensity;
        if (y < 0)
            y = 0;
        float d = pow(fSharpness, y);
        float d_inv = 1 - d;
        // Format 565
        BYTE r = MINT((r1 * d_inv + r2 * d) * 31.f);
        BYTE g = MINT((g1 * d_inv + g2 * d) * 63.f);
        BYTE b = MINT((b1 * d_inv + b2 * d) * 31.f);

        mpValTable[x] = ((r << 11) | (g << 5) | b);
    }
}
