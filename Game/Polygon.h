#pragma once

#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
class CFace
{
public:
    // index information
    DWORD geoms[3]; // geometry vertex indices
    DWORD texes[3]; // texture vertex indices
    DWORD norms[3]; // normale indices
    // this face plane equation
    CPlot3D n; // normale for this face
    float d;   // 'd' coeff for plane equation: n*X - d = 0
public:
    CFace();
    CFace(const CFace &face);
    ~CFace();
    //
    void operator=(const CFace &face);
    // plane coeffs calculation: 'n' and 'd'
    void CalcPlane(const CXYZW4Array &plots);
    void CalcPlane(const CPlot3DArray &plots);
#if !defined(__NOMFC__)
    // service : save/load
    void Save(CFile &file) const;
    void Load(CFile &file);
#endif // !defined(__NOMFC__)
};
/////////////////////////////////////////////////////////////////////////////////////
inline CFace::CFace()
{
}
inline CFace::~CFace()
{
}
inline void CFace::operator=(const CFace &face)
{
    memcpy(geoms, face.geoms, 3 * sizeof(DWORD));
    memcpy(texes, face.texes, 3 * sizeof(DWORD));
    memcpy(norms, face.norms, 3 * sizeof(DWORD));
    n = face.n;
    d = face.d;
}
inline CFace::CFace(const CFace &face)
{
    (*this) = face;
}
/////////////////////////////////////////////////////////////////////////////////////
typedef CSimplyArray<CFace, CFace &> CFaceArray;
