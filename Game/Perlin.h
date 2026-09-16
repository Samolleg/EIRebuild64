#pragma once

extern const int CLOUDS_SIZE;

class CPerlinGenerator
{
protected:
    int mnDensity;
    int mnSharpness;
    float mfAnimSpeed;

    LPWORD mpValTable;

    int *mpNoise;  // not smoothed noise picture
    LPBYTE mpMap4; // four arrays of NxN bytes for different freqs

    LPBYTE mpPic1;
    LPBYTE mpPic2; // interpolation pPic1 -> pPic2
    LPBYTE mpPic3; // generating pPic3

    float mfCurTime; // current interp step
    int mnCurStep;   // 0 - 15 (16 steps)

    int mnResultSize;

protected:
    virtual void GenerateNoiseLevel(LPBYTE pMap) = 0;
    virtual void GenerateNoisePicture(LPBYTE pPic, int nStep) = 0;

public:
    CPerlinGenerator(int nSize);
    virtual ~CPerlinGenerator();

    void Init();
    void GeneratePerlin(LPWORD pDst, float fFrameTime);
};
/////////////////////////////////////////////////////////////////////////////////////
class CCloudsGenerator : public CPerlinGenerator
{
protected:
    virtual void GenerateNoiseLevel(LPBYTE pMap);
    virtual void GenerateNoisePicture(LPBYTE pPic, int nStep);

public:
    CCloudsGenerator()
        : CPerlinGenerator(CLOUDS_SIZE)
    {
    }

    void SetParams(int nDensity, int nSharpness, float fSpeed);
};
/////////////////////////////////////////////////////////////////////////////////////
class CItemMagicGenerator : public CPerlinGenerator
{
protected:
    virtual void GenerateNoiseLevel(LPBYTE pMap);
    virtual void GenerateNoisePicture(LPBYTE pPic, int nStep);

public:
    CItemMagicGenerator()
        : CPerlinGenerator(128)
    {
    }

    void SetParams(int nDensity, int nSharpness, float fSpeed);
};
/////////////////////////////////////////////////////////////////////////////////////
class CFOWGenerator : public CCloudsGenerator
{
public:
    CFOWGenerator()
        : CCloudsGenerator()
    {
    }

    void SetParams(int nDensity, int nSharpness, float fSpeed, DWORD dwColor1, DWORD dwColor2);
};