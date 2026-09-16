#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
const DWORD B2D_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, -1);
/////////////////////////////////////////////////////////////////////////////////////
class CBase2D : public CGameBase
{
protected:
    CPlot2D cp;
    CRect rect;

public:
    CBase2D();
    CBase2D(const CBase2D &base);
    virtual ~CBase2D();

    virtual void operator=(const CBase2D &base);

    void SetCP(const CPlot2D &cp) { this->cp = cp; }
    CPlot2D GetCP() const { return cp; }

    virtual void Save(CFile &file);
    virtual void Load(CFile &file);
};
/////////////////////////////////////////////////////////////////////////////////////
inline CBase2D::CBase2D()
    : CGameBase()
{
    cp = CPlot2D(0, 0);
    rect.SetRectEmpty();
}
inline void CBase2D::operator=(const CBase2D &base)
{
    cp = base.cp;
    rect = base.rect;
}
inline CBase2D::CBase2D(const CBase2D &base)
    : CGameBase(base)
{
    (*this) = base;
}
inline CBase2D::~CBase2D()
{
}
/////////////////////////////////////////////////////////////////////////////////////
