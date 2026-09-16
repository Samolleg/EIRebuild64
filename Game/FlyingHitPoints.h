#pragma once

#include <functional>
#include <list>
/////////////////////////////////////////////////////////////////////////////////////
const int FHP_FLYTIME = 3000;
const DWORD BRIEFING_TO_SAY = 0x80000000;
/////////////////////////////////////////////////////////////////////////////////////
struct SFlyingHPDesc
{
public:
    int nUnitID; // unit which received damage
    int nAmount; // amount of damage received
    DWORD dwDamageFlags;
    DWORD dwTime; // time of receiving
};
/////////////////////////////////////////////////////////////////////////////////////
class CNeedDeleteFlyingHPFunctional
{
    DWORD dwCurrTime;

public:
    explicit CNeedDeleteFlyingHPFunctional(const DWORD dwCurrentTime)
        : dwCurrTime(dwCurrentTime)
    {
    }
    bool operator()(const SFlyingHPDesc &desc) const
    {
        return (int)dwCurrTime - (int)desc.dwTime > FHP_FLYTIME;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CNeedDeleteFlyingHPFunctional2
{
public:
    CNeedDeleteFlyingHPFunctional2() {}
    bool operator()(const SFlyingHPDesc &desc) const
    {
        return (desc.dwDamageFlags & BRIEFING_TO_SAY) ? true : false;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CTexture;
class CFlyingHitPoints
{
private:
    CTexture *pTexture;
    std::list<SFlyingHPDesc> damages;

public:
    CFlyingHitPoints();
    ~CFlyingHitPoints();

    void SetTexture(CTexture *pTex) { pTexture = pTex; }
    void ReceiveDamage(int nUnitID, int nAmount, DWORD dwDamageFlags = 0);
    BOOL Draw(CRenderer *pRend);
};
