#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "RandomGen.h"
/*==========================================================================================================

        Random generator class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

CRandomGenerator random;

#define ind(mm, x) (*(unsigned _int32 *)((unsigned _int8 *)(mm) + ((x) & ((RANDSIZ - 1) << 2))))

#define rngstep(mix, a, b, mm, m, m2, r, x)      \
    {                                            \
        x = *m;                                  \
        a = (a ^ (mix)) + *(m2++);               \
        *(m++) = y = ind(mm, x) + a + b;         \
        *(r++) = b = ind(mm, y >> RANDSIZL) + x; \
    }

#define mix(a, b, c, d, e, f, g, h) \
    {                               \
        a ^= b << 11;               \
        d += a;                     \
        b += c;                     \
        b ^= c >> 2;                \
        e += b;                     \
        c += d;                     \
        c ^= d << 8;                \
        f += c;                     \
        d += e;                     \
        d ^= e >> 16;               \
        g += d;                     \
        e += f;                     \
        e ^= f << 10;               \
        h += e;                     \
        f += g;                     \
        f ^= g >> 4;                \
        a += f;                     \
        g += h;                     \
        g ^= h << 8;                \
        b += g;                     \
        h += a;                     \
        h ^= a >> 9;                \
        c += h;                     \
        a += b;                     \
    }

void CRandomGenerator::Isaac()
{
    unsigned _int32 a, b, x, y, *m, *mm, *m2, *r, *mend;
    mm = randmem;
    r = randrsl;
    a = randa;
    b = randb + (++randc);
    for (m = mm, mend = m2 = m + (RANDSIZ / 2); m < mend;)
    {
        rngstep(a << 13, a, b, mm, m, m2, r, x);
        rngstep(a >> 6, a, b, mm, m, m2, r, x);
        rngstep(a << 2, a, b, mm, m, m2, r, x);
        rngstep(a >> 16, a, b, mm, m, m2, r, x);
    }
    for (m2 = mm; m2 < mend;)
    {
        rngstep(a << 13, a, b, mm, m, m2, r, x);
        rngstep(a >> 6, a, b, mm, m, m2, r, x);
        rngstep(a << 2, a, b, mm, m, m2, r, x);
        rngstep(a >> 16, a, b, mm, m, m2, r, x);
    }
    randb = b;
    randa = a;
}

void CRandomGenerator::Init()
{
    int i;
    unsigned _int32 a, b, c, d, e, f, g, h;
    unsigned _int32 *m, *r;

    if (bIsReady)
        return;
    FillRandRsl();
    randa = randb = randc = 0;
    m = randmem;
    r = randrsl;
    a = b = c = d = e = f = g = h = 0x9e3779b9; /* the golden ratio */

    for (i = 0; i < 4; ++i) /* scramble it */
        mix(a, b, c, d, e, f, g, h);

    /* initialize using the contents of r[] as the seed */
    for (i = 0; i < RANDSIZ; i += 8)
    {
        a += r[i];
        b += r[i + 1];
        c += r[i + 2];
        d += r[i + 3];
        e += r[i + 4];
        f += r[i + 5];
        g += r[i + 6];
        h += r[i + 7];
        mix(a, b, c, d, e, f, g, h);
        m[i] = a;
        m[i + 1] = b;
        m[i + 2] = c;
        m[i + 3] = d;
        m[i + 4] = e;
        m[i + 5] = f;
        m[i + 6] = g;
        m[i + 7] = h;
    }
    /* do a second pass to make all of the seed affect all of m */
    for (i = 0; i < RANDSIZ; i += 8)
    {
        a += m[i];
        b += m[i + 1];
        c += m[i + 2];
        d += m[i + 3];
        e += m[i + 4];
        f += m[i + 5];
        g += m[i + 6];
        h += m[i + 7];
        mix(a, b, c, d, e, f, g, h);
        m[i] = a;
        m[i + 1] = b;
        m[i + 2] = c;
        m[i + 3] = d;
        m[i + 4] = e;
        m[i + 5] = f;
        m[i + 6] = g;
        m[i + 7] = h;
    }
    Isaac();           /* fill in the first set of results */
    randcnt = RANDSIZ; /* prepare to use the first set of results */
    bIsReady = TRUE;
}

// --------------------------- FillRandRsl()
// ---------------------------------------------------------------

const int N_FROM_START = 1024;

BOOL CRandomGenerator::RecFindFile(LPSTR pszFindedName, LPCSTR pszBaseMask, int nToFind,
                                   int *pnTotFinded)
{
    CFileFind fileFind;
    BOOL frv = fileFind.FindFile(pszBaseMask);
    while (frv)
    {
        frv = fileFind.FindNextFile();
        if (fileFind.IsDots() || fileFind.IsSystem() || fileFind.IsHidden())
            continue;
        if (fileFind.IsDirectory())
        {
            if (RecFindFile(pszFindedName, fileFind.GetFilePath() + "\\*.*", nToFind,
                            pnTotFinded) == TRUE)
                return TRUE;
            continue;
        }
        if (*pnTotFinded >= nToFind)
        {
            if (fileFind.GetLength() >= N_FROM_START + sizeof(randrsl))
            {
                strcpy(pszFindedName, fileFind.GetFilePath());
                return TRUE;
            }
            (*pnTotFinded)--;
        }
        (*pnTotFinded)++;
    }
    return FALSE;
}

// The purpose of this func is to fill randrsl[RANDSIZ] arrays
// with initial random values
// It's uses first RANDSIZ values from random file for this
void CRandomGenerator::FillRandRsl()
{
    // find first drive
    char buf[1024];
    char pszMaskToFindFiles[256];
    int nSize = GetLogicalDriveStrings(sizeof(buf), buf);
    int i;
    for (i = 0; i < nSize;)
    {
        char *pDrive = buf + i;
        i += strlen(pDrive) + 1;
        if (GetDriveType(pDrive) == DRIVE_FIXED || GetDriveType(pDrive) == DRIVE_REMOTE)
        {
            strcpy(pszMaskToFindFiles, pDrive);
            strcat(pszMaskToFindFiles, "*.*");
            break;
        }
    }
    if (i == nSize)
        return; // cannot find any hd, run without initialization

    //
    srand(timeGetTime());
    char pszFindedName[256];
    BOOL bSuccess = FALSE;

    while (!bSuccess)
    {
        int nTotFinded = 0;
        if (!RecFindFile(pszFindedName, pszMaskToFindFiles, rand() % 512 + 1, &nTotFinded))
        {
            int nToFind = rand() % (nTotFinded - 1) + 1;
            nTotFinded = 0;
            if (!RecFindFile(pszFindedName, pszMaskToFindFiles, nToFind, &nTotFinded))
                continue;
        }
        bSuccess = TRUE;
        TRY
        {
            CFile inFile(pszFindedName, CFile::modeRead | CFile::shareDenyNone | CFile::typeBinary);
            srand(timeGetTime());
            inFile.Seek(N_FROM_START - rand() % (N_FROM_START - 512), CFile::begin);
            if (inFile.Read(randrsl, sizeof(randrsl)) != sizeof(randrsl))
                bSuccess = FALSE;
            inFile.Close();
        }
        CATCH(CFileException, e)
        {
            bSuccess = FALSE;
        }
        END_CATCH;
        BOOL bHaveNotZero = FALSE;
        for (int i = 0; i < RANDSIZ; i++)
            if (randrsl[i])
            {
                bHaveNotZero = TRUE;
                break;
            }
        if (bHaveNotZero == FALSE)
        {
            bSuccess = FALSE;
            Sleep(10);
        }
        for (i = 0; i < RANDSIZ; i++)
            randrsl[i] ^= rand();
    }
}
