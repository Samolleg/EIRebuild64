#pragma once
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
class CTerrainPrints
{
private:
    bool bProjected; // is mesh created from terrain projection ?
    bool bVisible;   // visible, если мы этот след уже видели...
    // geometry-relative data
    float fX, fY; // prints placement
    float fSizeX; // prints size
    float fSizeY;
    float fSizeInc; // size increment per life cycle
    // life control data
    short nLifeTimeCounter; // lifetime backcounter of this prints
    // texture-relative data
    CTexture *pTexture;       // texture with prints
    float fdx, fdy, fsx, fsy; // texture coords shift and scale to fit in the desired area
    float fMapRotate;
    float fGeomRotate;
    // color relative data
    BYTE cFogValue;
    float fAlpha;  // current alpha (start from 1.0f)
    float fPower;  // alpha power [0..1]
    DWORD dwColor; // diffuse color of this print
    int nFadeoutTime;
    // result mesh
    CXYZW4Array vertices; // result mesh vertices
    CPlot2DArray maps;    // result mapping vertices
    CWORDArray indices;   // result mesh indices
protected:
    void SetGeometryPlacement(float _fX, float _fY, float _fSizeX, float _fSizeY);
    void SetTexturePlacement(float _fdx, float _fdy, float _fsx, float _fsy)
    {
        fdx = _fdx;
        fdy = _fdy;
        fsx = _fsx;
        fsy = _fsy;
    }
    void SetTexture(const char *pszName);
    void SetSizeInc(float fNewSizeInc) { fSizeInc = fNewSizeInc; }
    void SetupSizeIncrement(float fFinalSize, short nTime)
    {
        SetSizeInc((fFinalSize - fSizeX) / float(nTime));
    }
    void SetProjected(bool bVal) { bProjected = bVal; }
    void SetMapRotate(float fVal) { fMapRotate = fVal; }
    void SetGeomRotate(float fVal) { fGeomRotate = fVal; }
    float GetGeomRotate() const { return fGeomRotate; }
    BYTE GetFogValue() const { return cFogValue; }

public:
    CTerrainPrints(DWORD _dwColor, float _fPower, int _nLifeTime, int _nFadeoutTime, bool _bVisible)
        : dwColor(_dwColor)
        , fAlpha(1)
        , bProjected(false)
        , bVisible(_bVisible)
        , fSizeInc(0)
        , fMapRotate(0)
        , fGeomRotate(0)
        , cFogValue(255)
        , fPower(_fPower)
        , nLifeTimeCounter(_nLifeTime)
        , nFadeoutTime(_nFadeoutTime)
    {
    }
    //
    int Draw();
    //
    bool IsInvisible() const { return !bVisible; }
    void SetVisible(bool bVal) { bVisible = bVal; }
    //
    void SetColor(DWORD dwNewColor) { dwColor = dwNewColor; }
    //
    float GetCenterX() const { return fX; }
    float GetCenterY() const { return fY; }
    float GetSizeX() const { return fSizeX; }
    float GetSizeY() const { return fSizeY; }
    float GetAverageSize() const { return (fSizeX + fSizeY) * 0.5f; }
    int GetRemainingSegments() const { return nLifeTimeCounter; }
    int GetFadeoutSegments() const { return nFadeoutTime; }
    //
    virtual int Segment();
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CBloodPrints : public CTerrainPrints
{
private:
    float fGeomRotateStep;
    short nIncrementingTime; // during this time bloodprints will increase their size
    int nBloodType;
    bool bSmall; // true, если это маленькая капля
public:
    CBloodPrints(float fX, float fY, float fSize, float fFinalSize, float fPower, int nLifetime,
                 int nFadeoutTime, DWORD dwType, DWORD dwColor, bool bSmall);
    //
    virtual int Segment();
    //
    int GetBloodType() const { return nBloodType; }
    bool IsSmall() const { return bSmall; }
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CFootPrints : public CTerrainPrints
{
private:
    float fDetection;
    int nBloodType;

public:
    CFootPrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate, float fPower,
                int nLifetime, int nFadeoutTime, DWORD dwType, float fDetection, bool bVisible,
                int nBloodType);
    float GetDetection() const { return fDetection; }
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CFirePrints : public CTerrainPrints
{
public:
    CFirePrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate, float fPower,
                int nLifetime, int nFadeoutTime);
};
/////////////////////////////////////////////////////////////////////////////////////
