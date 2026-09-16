#pragma once

#include <assert.h>
/*==========================================================================================================

        Random generator class header
        Uses ISAAC random generator, (c) Bob Jenkins with
        with using random file for initial random seeds

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

const int RANDSIZL = 8;
const int RANDSIZ = 1 << RANDSIZL;

class CRandomGenerator
{
private:
    BOOL bIsReady;
    unsigned _int32 randcnt;
    unsigned _int32 randrsl[RANDSIZ];
    unsigned _int32 randmem[RANDSIZ];
    unsigned _int32 randa;
    unsigned _int32 randb;
    unsigned _int32 randc;

public:
    CRandomGenerator() { bIsReady = FALSE; }
    void Init(); // That is very slow operation
    unsigned _int32 Get();
    unsigned _int32 Get(unsigned _int32 nMax) { return Get() % (nMax + 1); }
    unsigned _int32 Get(unsigned _int32 nMin, unsigned _int32 nMax)
    {
        return Get(nMax - nMin) + nMin;
    }
    float GetFloat(float fpMin, float fpMax);
    unsigned _int32 Dice(unsigned _int32 nNum, unsigned _int32 nDice);

private:
    void Isaac();
    void FillRandRsl();
    BOOL RecFindFile(LPSTR pszFindedName, LPCSTR pszBaseDir, int nToFind, int *pnTotFinded);
};

inline unsigned _int32 CRandomGenerator::Get()
{
    assert(bIsReady);
    if (randcnt-- == 0)
    {
        Isaac();
        randcnt = RANDSIZ - 1;
    }
    return randrsl[randcnt];
}

inline unsigned _int32 CRandomGenerator::Dice(unsigned _int32 nNum, unsigned _int32 nDice)
{
    unsigned _int32 nRes = 0;
    for (unsigned _int32 i = 0; i < nNum; i++)
        nRes += Get() % nDice + 1;
    return nRes;
}

inline float CRandomGenerator::GetFloat(float fpMin, float fpMax)
{
    assert(fpMin <= fpMax);
    return fpMin + float(double(Get()) * double(fpMax - fpMin) / double(0xFFFFFFFF));
}

// ---------------------------- Random generator instance
// --------------------------------------------------
extern CRandomGenerator random;
