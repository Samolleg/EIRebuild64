template <class VAL>
class CXYPositionMatchFunctional
{
private:
    float x, y;

public:
    explicit CXYPositionMatchFunctional(const float fX, const float fY)
        : x(fX)
        , y(fY)
    {
    }
    bool operator()(const VAL *pVal) { return ((pVal->GetCPX() == x) && (pVal->GetCPY() == y)); }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline CAreaMap<VAL>::~CAreaMap()
{
    ClearAll();
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::SetDimentions(int nNewSizeX, int nNewSizeY)
{
    ClearAll();
    nSizeX = BuildCoordIndex(nNewSizeX);
    nSizeY = BuildCoordIndex(nNewSizeY);
    map.SetSizes(nSizeX, nSizeY);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::Add(VAL *val)
{
    DWORD dwX = BuildCoordIndex(val->GetCPX());
    DWORD dwY = BuildCoordIndex(val->GetCPY());
    //
    if (!CanPlace(dwX, dwY))
    {
        ASSERT(0);
        return; // can't place to this position - out of range
    }
    // add to list
    GetList(dwX, dwY)->push_back(val);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::Remove(VAL *val)
{
    DWORD dwX = BuildCoordIndex(val->GetCPX());
    DWORD dwY = BuildCoordIndex(val->GetCPY());
    //
    if (!CanPlace(dwX, dwY))
    {
        ASSERT(0);
        return;
    }
    // remove from list
    GetList(dwX, dwY)->remove(val);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::RemoveAt(const float x, const float y)
{
    DWORD dwX = BuildCoordIndex(x);
    DWORD dwY = BuildCoordIndex(y);
    //
    if (!CanPlace(dwX, dwY))
    {
        ASSERT(0);
        return;
    }
    // remove from list
    GetList(dwX, dwY)->remove_if(CXYPositionMatchFunctional<VAL>(x, y));
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::RemoveAll()
{
    for (int i = 0; i < nSizeX; ++i)
        for (int j = 0; j < nSizeY; ++j)
            GetList(i, j)->clear();
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::ClearAll()
{
    map.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline bool CAreaMap<VAL>::Lookup(const float x, const float y, VAL *&rpValue) const
{
    DWORD dwX = BuildCoordIndex(x);
    DWORD dwY = BuildCoordIndex(y);
    CValList *lst = GetList(dwX, dwY);
    //
    auto it = std::find_if(lst->begin(), lst->end(), CXYPositionMatchFunctional<VAL>(x, y));
    if (it == lst->end())
        return false;

    rpValue = *it;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline void CAreaMap<VAL>::MoveTo(VAL *val, const CPlot3D &position)
{
    DWORD dwOldX = BuildCoordIndex(val->GetCPX());
    DWORD dwOldY = BuildCoordIndex(val->GetCPY());
    DWORD dwNewX = BuildCoordIndex(position.x);
    DWORD dwNewY = BuildCoordIndex(position.y);
    //
    if ((dwOldX != dwNewX) || (dwOldY != dwNewY))
    {
        // remove old value
        if (CanPlace(dwOldX, dwOldY))
            GetList(dwOldX, dwOldY)->remove(val);
        else
        {
            ASSERT(0);
        }
        // set new CP coords
        val->SetCP(position);
        // set new value
        if (CanPlace(dwNewX, dwNewY))
            GetList(dwNewX, dwNewY)->push_back(val);
        else
        {
            ASSERT(0);
        }
    }
    else
        val->SetCP(position);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline std::list<CSmartPtr<VAL>> *CAreaMap<VAL>::GetFirstList(int minx, int miny, int maxx,
                                                              int maxy)
{
    dwPosX = dwMinX = BuildCoordIndex(minx);
    dwPosY = dwMinY = BuildCoordIndex(miny);
    dwMaxX = BuildCoordIndex(maxx);
    dwMaxY = BuildCoordIndex(maxy);

    return GetList(dwPosX, dwPosY);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline std::list<CSmartPtr<VAL>> *CAreaMap<VAL>::GetNextList()
{
    if (++dwPosX > dwMaxX)
    {
        dwPosX = dwMinX;
        if (++dwPosY > dwMaxY)
            return 0;
    }

    return GetList(dwPosX, dwPosY);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline bool CAreaMap<VAL>::CanPlace(DWORD dwX, DWORD dwY)
{
    return ((dwX < nSizeX) && (dwY < nSizeY));
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline bool CAreaMap<VAL>::CanPlace(float x, float y)
{
    DWORD dwX = BuildCoordIndex(x);
    DWORD dwY = BuildCoordIndex(y);
    return CanPlace(dwX, dwY);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class VAL>
inline bool CAreaMap<VAL>::CanPlace(const CPlot3D &position)
{
    return CanPlace(position.x, position.y);
}
/////////////////////////////////////////////////////////////////////////////////////
