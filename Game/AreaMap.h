#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "sArray.h"
#include "Plot.h"
#include "SmartPtr.h"
#include <list>
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
class CAreaMap
{
    typedef std::list<CSmartPtr<VAL>> CValList;

private:
    CArray2D<CValList> map;
    int nSizeX, nSizeY;                   // area map dimentions
    DWORD dwMinX, dwMinY, dwMaxX, dwMaxY; // scan area
    DWORD dwPosX, dwPosY;                 // current iteration list area position
private:
    DWORD BuildCoordIndex(const int nCoord) const { return DWORD(nCoord) >> 4; }
    DWORD BuildCoordIndex(const float fCoord) const { return BuildCoordIndex(MINT(fCoord)); }
    DWORD BuildHashKey(const int x, const int y) const
    {
        return ((static_cast<DWORD>(x) >> 4) | ((static_cast<DWORD>(y) & 0x0000FFF0) << 12));
    }
    DWORD BuildHashKey(const float x, const float y) const
    {
        return BuildHashKey(MINT(x), MINT(y));
    }
    DWORD BuildHashKey(const CPlot3D &plot) const
    {
        return BuildHashKey(MINT(plot.x), MINT(plot.y));
    }
    DWORD BuildHashKey(const CPlot2D &plot) const
    {
        return BuildHashKey(MINT(plot.x), MINT(plot.y));
    }
    //
    CValList *GetList(const DWORD dwX, const DWORD dwY) { return &(map[dwY][dwX]); }
    // range check
    bool CanPlace(DWORD dwX, DWORD dwY);

private:
    // disabled operations
    CAreaMap(const CAreaMap &m);
    const CAreaMap &operator=(const CAreaMap &m);

public:
    CAreaMap() = default;
    ~CAreaMap();
    // size changing functions
    void SetDimentions(int nSizeX, int nSizeY);
    // add/remove
    void Add(VAL *val);
    void Remove(VAL *val);
    void RemoveAt(const float x, const float y);
    void RemoveAt(const CPlot3D &plot) { RemoveAt(plot.x, plot.y); }
    void RemoveAll();
    void ClearAll();
    // lookup
    bool Lookup(const float x, const float y, VAL *&rpValue) const;
    bool Lookup(const CPlot3D &plot, VAL *&rpValue) const
    {
        return Lookup(plot.x, plot.y, rpValue);
    }
    bool Lookup(const CPlot2D &plot, VAL *&rpValue) const
    {
        return Lookup(plot.x, plot.y, rpValue);
    }
    //
    void MoveTo(VAL *val, const CPlot3D &position);
    //
    bool CanPlace(float x, float y);
    bool CanPlace(const CPlot3D &position);
    //
    CValList *GetFirstList(int minx, int miny, int maxx, int maxy);
    CValList *GetNextList();
};
/////////////////////////////////////////////////////////////////////////////////////
#include "AreaMap.inl"
