#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "RandomGen.h"
#include "Crypto.h"
/*==========================================================================================================

        Encryption class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

// Long math functions
// Generate secret and public keys for Diffie-Hellman
void GenKeys(void *pPrimePow, void *pPrimeMod, int nDim, void *pSKey, void *pPKey);
// Generate Diffie-Hellman session key
void GenDHKey(void *pPrimeMod, void *pSKey, void *pRemotePKey, int nDim, void *pSessKey);
// Dimension of pRes must be at nDim*2
void Multiply(void *pA, void *pB, int nDim, void *pRes);
// Dimension of pRes must be nDim, pA must have 4 additional bytes for temporary purposes
void Module(void *pA, void *pMod, int nDim, void *pRes);
// Modular exponent, nDim must be <= N_CRYPTO_MAX_DIM
const int N_CRYPTO_MAX_DIM = 128;
void PowMod(void *pA, void *pX, void *pMod, int nDim, void *pRes);

// Hash functions
void MDInit(void *pMDbuf);                  // Init hash buffer
void MDCompress(void *pMDbuf, void *pMess); // Update hash buffer with pMess

// ---------------------------------- CCrypter
// -------------------------------------------------------------
static unsigned _int32 nPrimePow[CRY_KEY_LEN / 4] = { 0xe1e8d679, 0x721b753e, 0x64db2cd3,
                                                      0xcb8fa781, 0xf75cb385, 0xa31e9d4f,
                                                      0x8cd0e379, 0xe4f2c453 };
static unsigned _int32 nPrimeMod[CRY_KEY_LEN / 4] = { 0x6b5d254b, 0x1dbd35cf, 0x43b6dd3a,
                                                      0x00709714, 0x3bfd37d9, 0x50db2ffa,
                                                      0x62c97af0, 0xe9476236 };

void CCrypter::GenerateKeys()
{
    GenKeys(nPrimePow, nPrimeMod, CRY_KEY_LEN, secretKey, publicKey);
}

void CCrypter::GetPublicKey(void *lpBuf)
{
    memcpy(lpBuf, publicKey, sizeof(publicKey));
}

void CCrypter::SetRemotePublicKey(void *lpBuf)
{
    GenDHKey(nPrimeMod, secretKey, lpBuf, CRY_KEY_LEN, sessionKey);
    InitGamma(hashEncrypt, gammaForEncrypt, nGammaForEncryptLeft);
    InitGamma(hashDecrypt, gammaForDecrypt, nGammaForDecryptLeft);
}

void CCrypter::EncodeBlock(void *_pBuf, int nBufLen, BYTE *pHash, BYTE *pGamma, int &nGammaLeft)
{
    LPBYTE pBuf = LPBYTE(_pBuf);
    LPBYTE pGammaPtr = pGamma + CRY_HASH_LEN - nGammaLeft;
    while (nBufLen)
    {
        while (nGammaLeft == CRY_HASH_LEN && nBufLen >= CRY_HASH_LEN)
        {
            assert(CRY_HASH_LEN == 16);

            *LPDWORD(pBuf) ^= *LPDWORD(pGammaPtr);
            *LPDWORD(pBuf + 4) ^= *LPDWORD(pGammaPtr + 4);
            *LPDWORD(pBuf + 8) ^= *LPDWORD(pGammaPtr + 8);
            *LPDWORD(pBuf + 12) ^= *LPDWORD(pGammaPtr + 12);

            NextGamma(pHash, pGamma, nGammaLeft);
            pBuf += 16;
            nBufLen -= CRY_HASH_LEN;
        }
        for (; nGammaLeft && nBufLen; nGammaLeft--, nBufLen--)
            *pBuf++ ^= *pGammaPtr++;
        if (nBufLen)
        {
            NextGamma(pHash, pGamma, nGammaLeft);
            pGammaPtr = pGamma;
        }
    }
}

void CCrypter::InitGamma(BYTE *pHash, BYTE *pGamma, int &nGammaLeft)
{
    assert(CRY_KEY_LEN >= CRY_HASH_LEN);
    assert(CRY_KEY_LEN % CRY_HASH_LEN == 0);

    memset(pHash, 0, CRY_HASH_LEN);
    LPBYTE pSess = sessionKey;
    for (int i = 0; i < CRY_KEY_LEN; i += CRY_HASH_LEN)
    {
        LPBYTE pHashTmp = pHash;
        for (int j = 0; j < CRY_HASH_LEN; j++)
            *pHashTmp++ ^= *pSess++;
    }
    MDInit(pGamma);
    NextGamma(pHash, pGamma, nGammaLeft);
}

inline void CCrypter::NextGamma(BYTE *pHash, BYTE *pGamma, int &nGammaLeft)
{
    (*((DWORD *)pHash))++;
    MDCompress(pGamma, pHash);
    nGammaLeft = CRY_HASH_LEN;
}

// -------------------------------- Long arithmetic
// --------------------------------------------------------

static void Multiply(void *pA, void *pB, int nDim, void *pRes)
{
    assert(nDim % 4 == 0);
    nDim >>= 2;

    int i, j;
    _asm {
		push ebx
		push esi
		push edi

		xor eax, eax
		cld
		mov edi, [pRes]
		mov ecx, [nDim]
		shl ecx, 1
		rep stosd

		mov ebx, [pB]
		mov [i], 0
		
		LoopI:
			mov esi, [pA]
			mov edi, [pRes]
			mov eax, i
			shl eax, 2
			add edi, eax
			mov eax, [nDim]
			mov [j], eax
			xor ecx, ecx
			
			LoopJ:
				mov eax, [esi]
				add esi, 4
				mul dword ptr [ebx]
				add eax, [edi]
				adc edx, 0
				add eax, ecx
				adc edx, 0
				mov [edi], eax
				mov ecx, edx
				add edi, 4
			dec [j]
			jnz LoopJ
			
			mov [edi], ecx
		
		add ebx, 4
		inc [i]
		mov eax, [nDim]
		cmp [i], eax
		jl LoopI

		pop edi
		pop esi
		pop ebx
    }
}

// ------------------------------------- Module
// ------------------------------------------------------------
typedef unsigned _int32 MINT32;
typedef unsigned _int16 MINT16;

#pragma warning(disable : 4244)
static void Module(void *_pA, void *_pMod, int nDim, void *_pRes)
{
    MINT16 *pA = (MINT16 *)_pA;
    MINT16 *pMod = (MINT16 *)_pMod;
    MINT16 *pRes = (MINT16 *)_pRes;
    const MINT32 BB = 0x10000;

    memset(pRes, 0, nDim);

    assert(nDim % 4 == 0);
    nDim >>= 1;

    _asm
        {
		xor eax, eax
		cld
		mov edi, [pRes]
		mov ecx, [nDim]
		shr ecx, 1
		rep stosd
        }

    MINT32 T0, T1, T2, T3, qq;
    MINT16 pr, r, T, p;
    int i, j, mu, mv, mq, sd, ind, ind1;

    pA[nDim] = 0;
    pA[nDim + 1] = 0;

    for (i = nDim - 1; i >= 0; i--)
        if (pA[i] < pMod[i])
            goto m4;
        else if (pA[i] > pMod[i])
            goto m5;
m5:
    for (i = nDim - 1; i >= 0; i--)
        if (pA[i] != 0)
        {
            mu = i + 1;
            goto m0;
        }
m0:
    for (i = nDim - 1; i >= 0; i--)
        if (pMod[i] != 0)
        {
            mv = i + 1;
            goto m1;
        }
m1:
    if (mv == 1)
    {
        r = 0;
        for (i = mu - 1; i >= 0; i--)
        {
            T0 = (MINT32(r) << 16) ^ pA[i];
            pRes[i] = T0 / pMod[0];
            r = T0 - MINT32(pMod[0]) * MINT32(pRes[i]);
        }
        pA[0] = r;
        for (i = mu - 1; i > 0; i--)
            pA[i] = 0;
        mq = mu;
        goto m4;
    }
    p = BB / 2;
    for (i = 0; i < 16; i++)
    {
        T = pMod[mv - 1] & p;
        if (T == p)
        {
            sd = i;
            mu++;
            if (sd == 0)
                goto m3;
            if (pA[mu - 2] >= 2 * p)
                mu++;
            goto m2;
        }
        p /= 2;
    }
m2:
    pr = 0;
    for (i = 0; i < mv; i++)
    {
        T = pMod[i];
        pMod[i] = (pMod[i] << sd) ^ pr;
        pr = T >> (16 - sd);
    }
    pr = 0;
    for (i = 0; i < mu; i++)
    {
        T = pA[i];
        pA[i] = (pA[i] << sd) ^ pr;
        pr = T >> (16 - sd);
    }
m3:
    mq = mu - mv;
    for (i = mq - 1; i >= 0; i--)
    {
        ind = mv + i;
        T3 = (MINT32(pA[ind]) << 16) ^ pA[ind - 1];
        qq = T3 / pMod[mv - 1];
        T1 = qq * pMod[mv - 2];
        T2 = ((T3 - qq * pMod[mv - 1]) << 16) ^ pA[ind - 2];
        if (T1 > T2)
            qq--;
        pr = 0;
        for (j = 0; j < mv; j++)
        {
            ind1 = i + j;
            T0 = MINT32(pA[ind1]) - qq * pMod[j] - MINT16(BB - pr);
            pA[ind1] = T0;
            pr = T0 >> 16;
        }
        T0 = MINT32(pA[ind]) - MINT16(BB - pr);
        pA[ind] = T0;
        pr = T0 >> 16;
        if (pr == 0)
            pRes[i] = qq;
        else
        {
            pRes[i] = qq - 1;
            pr = 0;
            for (j = 0; j < mv; j++)
            {
                ind1 = i + j;
                T0 = MINT32(pA[ind1]) + MINT32(pMod[j]) + pr;
                pA[ind1] = T0;
                pr = T0 >> 16;
            }
            pA[ind] += pr;
        }
    }
    if (sd == 0)
        goto m4;
    pr = 0;
    for (i = mv - 1; i >= 0; i--)
    {
        T = pA[i];
        pA[i] = (pA[i] >> sd) ^ pr;
        pr = T << (16 - sd);
    }
m4:
    for (i = 0; i < nDim; i++)
        pRes[i] = pA[i];

    pA[nDim] = 0;
    pA[nDim + 1] = 0;
}
#pragma warning(default : 4244)

// ---------------------------------- Modular exponent
// -----------------------------------------------------

static void PowMod(void *_pA, void *_pX, void *pMod, int nDim, void *pRes)
{
    assert(nDim <= N_CRYPTO_MAX_DIM);

    MINT32 *pMask = (MINT32 *)_pX;

    BYTE pA[N_CRYPTO_MAX_DIM * 2];
    memcpy(pA, _pA, nDim);

    BYTE pTempRes[N_CRYPTO_MAX_DIM * 2 + 4];
    BYTE pTempMod[N_CRYPTO_MAX_DIM * 2];
    memcpy(pTempMod, pMod, nDim);
    memset(pTempMod + nDim, 0, nDim);

    BYTE pOut[N_CRYPTO_MAX_DIM * 2];
    memcpy(pOut, pA, nDim);

    MINT32 dwMask;
    for (int i = 0; i < nDim * 8; i++)
    {
        if ((i & 31) == 0)
            dwMask = *pMask++;
        else
            dwMask >>= 1;
        if (dwMask & 1)
        {
            Multiply(pOut, pA, nDim, pTempRes);
            Module(pTempRes, pTempMod, nDim * 2, pOut);
        }
        Multiply(pOut, pOut, nDim, pTempRes);
        Module(pTempRes, pTempMod, nDim * 2, pOut);
    }
    memcpy(pRes, pOut, nDim);
}

// ------------------------------------- Create keys
// -------------------------------------------------------

static void GenDHKey(void *pPrimeMod, void *pSKey, void *pRemotePKey, int nDim, void *pSessKey)
{
    PowMod(pRemotePKey, pSKey, pPrimeMod, nDim, pSessKey);

    /*
    const int N_RUN_NUM = 100;
    DWORD dwTime = timeGetTime();
    dwTime = timeGetTime();
    for ( int i = 0; i < N_RUN_NUM; i++ )
            PowMod( pRemotePKey, pSKey, pPrimeMod, nDim, pSessKey );
    dwTime = timeGetTime() - dwTime;
    printf( "\n=========== DH Key ============\n %f per second\n",
            float( N_RUN_NUM ) * 1000.f / float( dwTime ) );
    */
    /*
    printf( "\n=========== DH Key ============\n" );
    LPDWORD SK = LPDWORD( pSessKey );
    for ( int i = 0; i < nDim; i+=4 )
            printf( "%x ", *SK++ );
    printf( "\n===============================\n" );

    DWORD nPrimeMod[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    DWORD dwPrime[4] = { 0xFF, 0xAA, 0, 0 };
    DWORD dwSA[4] = { 10, 0xDF, 0, 0 };
    DWORD dwSB[4] = { 3, 0x56, 0, 0 };
    DWORD dwPA[4];
    DWORD dwPB[4];
    DWORD dwDHA[4];
    DWORD dwDHB[4];

    PowMod( dwPrime, dwSA, nPrimeMod, 16, dwPA );
    PowMod( dwPrime, dwSB, nPrimeMod, 16, dwPB );

    PowMod( dwPB, dwSA, nPrimeMod, 16, dwDHA );
    PowMod( dwPA, dwSB, nPrimeMod, 16, dwDHB );

    DWORD dwA[1] = { 2 };
    DWORD dwX[1] = { 4 };
    DWORD dwM[1] = { 0xFFFFFFFF };
    DWORD dwR[1];

    PowMod( dwA, dwX, dwM, 4, dwR );
    */
}

static void GenKeys(void *pPrimePow, void *pPrimeMod, int nDim, void *pSKey, void *pPKey)
{
    assert(nDim % 4 == 0);

    unsigned _int32 *pKey = (unsigned _int32 *)pSKey;
    for (int i = 0; i < nDim; i += 4)
        *pKey++ = random.Get();

    PowMod(pPrimePow, pSKey, pPrimeMod, nDim, pPKey);
}

// --------------------------------- Hash function (RIPEMD-128) implemetation
// ------------------------------

// ROL(x, nDim) cyclically rotates x over nDim bits to the left
// x must be of an unsigned 32 bits type and 0 <= nDim < 32.
// #define ROL( x, nDim )			((x)=(((x) << (nDim)) | ((x) >> (32-(nDim)))))
#define ROL(x, nDim) _asm rol x, nDim

// the four basic functions F(), G() and H()
#define F(x, y, z) ((x) ^ (y) ^ (z))
#define G(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define H(x, y, z) (((x) | ~(y)) ^ (z))
#define I(x, y, z) (((x) & (z)) | ((y) & ~(z)))
// the eight basic operations FF() through III()
#define FF(a, b, c, d, x, s)           \
    {                                  \
        (a) += F((b), (c), (d)) + (x); \
        ROL(a, s)                      \
    }

#define GG(a, b, c, d, x, s)                          \
    {                                                 \
        (a) += G((b), (c), (d)) + (x) + 0x5a827999UL; \
        ROL(a, s)                                     \
    }

#define HH(a, b, c, d, x, s)                          \
    {                                                 \
        (a) += H((b), (c), (d)) + (x) + 0x6ed9eba1UL; \
        ROL(a, s)                                     \
    }

#define II(a, b, c, d, x, s)                          \
    {                                                 \
        (a) += I((b), (c), (d)) + (x) + 0x8f1bbcdcUL; \
        ROL(a, s)                                     \
    }

#define FFF(a, b, c, d, x, s)          \
    {                                  \
        (a) += F((b), (c), (d)) + (x); \
        ROL(a, s)                      \
    }

#define GGG(a, b, c, d, x, s)                         \
    {                                                 \
        (a) += G((b), (c), (d)) + (x) + 0x6d703ef3UL; \
        ROL(a, s)                                     \
    }
#define HHH(a, b, c, d, x, s)                         \
    {                                                 \
        (a) += H((b), (c), (d)) + (x) + 0x5c4dd124UL; \
        ROL(a, s)                                     \
    }

#define III(a, b, c, d, x, s)                         \
    {                                                 \
        (a) += I((b), (c), (d)) + (x) + 0x50a28be6UL; \
        ROL(a, s)                                     \
    }

// --------------------------------- MDInit
// ----------------------------------------------------------------
static void MDInit(void *MDbuf)
{
    *(LPDWORD(MDbuf) + 0) = 0x67452301UL;
    *(LPDWORD(MDbuf) + 1) = 0xefcdab89UL;
    *(LPDWORD(MDbuf) + 2) = 0x98badcfeUL;
    *(LPDWORD(MDbuf) + 3) = 0x10325476UL;
}
// -------------------------------- MDCompress
// -------------------------------------------------------------
static void MDCompress(void *pMDbuf, void *pX)
{
    LPDWORD MDbuf = LPDWORD(pMDbuf);
    LPBYTE X = LPBYTE(pX);
    DWORD aa = MDbuf[0], bb = MDbuf[1], cc = MDbuf[2], dd = MDbuf[3];
    DWORD aaa2 = MDbuf[0], bbb = MDbuf[1], ccc = MDbuf[2], ddd = MDbuf[3];
    /* round 1 */
    FF(aa, bb, cc, dd, X[0], 11);
    FF(dd, aa, bb, cc, X[1], 14);
    FF(cc, dd, aa, bb, X[2], 15);
    FF(bb, cc, dd, aa, X[3], 12);
    FF(aa, bb, cc, dd, X[4], 5);
    FF(dd, aa, bb, cc, X[5], 8);
    FF(cc, dd, aa, bb, X[6], 7);
    FF(bb, cc, dd, aa, X[7], 9);
    FF(aa, bb, cc, dd, X[8], 11);
    FF(dd, aa, bb, cc, X[9], 13);
    FF(cc, dd, aa, bb, X[10], 14);
    FF(bb, cc, dd, aa, X[11], 15);
    FF(aa, bb, cc, dd, X[12], 6);
    FF(dd, aa, bb, cc, X[13], 7);
    FF(cc, dd, aa, bb, X[14], 9);
    FF(bb, cc, dd, aa, X[15], 8);
    /* round 2 */
    GG(aa, bb, cc, dd, X[7], 7);
    GG(dd, aa, bb, cc, X[4], 6);
    GG(cc, dd, aa, bb, X[13], 8);
    GG(bb, cc, dd, aa, X[1], 13);
    GG(aa, bb, cc, dd, X[10], 11);
    GG(dd, aa, bb, cc, X[6], 9);
    GG(cc, dd, aa, bb, X[15], 7);
    GG(bb, cc, dd, aa, X[3], 15);
    GG(aa, bb, cc, dd, X[12], 7);
    GG(dd, aa, bb, cc, X[0], 12);
    GG(cc, dd, aa, bb, X[9], 15);
    GG(bb, cc, dd, aa, X[5], 9);
    GG(aa, bb, cc, dd, X[2], 11);
    GG(dd, aa, bb, cc, X[14], 7);
    GG(cc, dd, aa, bb, X[11], 13);
    GG(bb, cc, dd, aa, X[8], 12);
    /* round 3 */
    HH(aa, bb, cc, dd, X[3], 11);
    HH(dd, aa, bb, cc, X[10], 13);
    HH(cc, dd, aa, bb, X[14], 6);
    HH(bb, cc, dd, aa, X[4], 7);
    HH(aa, bb, cc, dd, X[9], 14);
    HH(dd, aa, bb, cc, X[15], 9);
    HH(cc, dd, aa, bb, X[8], 13);
    HH(bb, cc, dd, aa, X[1], 15);
    HH(aa, bb, cc, dd, X[2], 14);
    HH(dd, aa, bb, cc, X[7], 8);
    HH(cc, dd, aa, bb, X[0], 13);
    HH(bb, cc, dd, aa, X[6], 6);
    HH(aa, bb, cc, dd, X[13], 5);
    HH(dd, aa, bb, cc, X[11], 12);
    HH(cc, dd, aa, bb, X[5], 7);
    HH(bb, cc, dd, aa, X[12], 5);
    /* round 4 */
    II(aa, bb, cc, dd, X[1], 11);
    II(dd, aa, bb, cc, X[9], 12);
    II(cc, dd, aa, bb, X[11], 14);
    II(bb, cc, dd, aa, X[10], 15);
    II(aa, bb, cc, dd, X[0], 14);
    II(dd, aa, bb, cc, X[8], 15);
    II(cc, dd, aa, bb, X[12], 9);
    II(bb, cc, dd, aa, X[4], 8);
    II(aa, bb, cc, dd, X[13], 9);
    II(dd, aa, bb, cc, X[3], 14);
    II(cc, dd, aa, bb, X[7], 5);
    II(bb, cc, dd, aa, X[15], 6);
    II(aa, bb, cc, dd, X[14], 8);
    II(dd, aa, bb, cc, X[5], 6);
    II(cc, dd, aa, bb, X[6], 5);
    II(bb, cc, dd, aa, X[2], 12);
    /* parallel round 1 */
    III(aaa2, bbb, ccc, ddd, X[5], 8);
    III(ddd, aaa2, bbb, ccc, X[14], 9);
    III(ccc, ddd, aaa2, bbb, X[7], 9);
    III(bbb, ccc, ddd, aaa2, X[0], 11);
    III(aaa2, bbb, ccc, ddd, X[9], 13);
    III(ddd, aaa2, bbb, ccc, X[2], 15);
    III(ccc, ddd, aaa2, bbb, X[11], 15);
    III(bbb, ccc, ddd, aaa2, X[4], 5);
    III(aaa2, bbb, ccc, ddd, X[13], 7);
    III(ddd, aaa2, bbb, ccc, X[6], 7);
    III(ccc, ddd, aaa2, bbb, X[15], 8);
    III(bbb, ccc, ddd, aaa2, X[8], 11);
    III(aaa2, bbb, ccc, ddd, X[1], 14);
    III(ddd, aaa2, bbb, ccc, X[10], 14);
    III(ccc, ddd, aaa2, bbb, X[3], 12);
    III(bbb, ccc, ddd, aaa2, X[12], 6);
    /* parallel round 2 */
    HHH(aaa2, bbb, ccc, ddd, X[6], 9);
    HHH(ddd, aaa2, bbb, ccc, X[11], 13);
    HHH(ccc, ddd, aaa2, bbb, X[3], 15);
    HHH(bbb, ccc, ddd, aaa2, X[7], 7);
    HHH(aaa2, bbb, ccc, ddd, X[0], 12);
    HHH(ddd, aaa2, bbb, ccc, X[13], 8);
    HHH(ccc, ddd, aaa2, bbb, X[5], 9);
    HHH(bbb, ccc, ddd, aaa2, X[10], 11);
    HHH(aaa2, bbb, ccc, ddd, X[14], 7);
    HHH(ddd, aaa2, bbb, ccc, X[15], 7);
    HHH(ccc, ddd, aaa2, bbb, X[8], 12);
    HHH(bbb, ccc, ddd, aaa2, X[12], 7);
    HHH(aaa2, bbb, ccc, ddd, X[4], 6);
    HHH(ddd, aaa2, bbb, ccc, X[9], 15);
    HHH(ccc, ddd, aaa2, bbb, X[1], 13);
    HHH(bbb, ccc, ddd, aaa2, X[2], 11);
    /* parallel round 3 */
    GGG(aaa2, bbb, ccc, ddd, X[15], 9);
    GGG(ddd, aaa2, bbb, ccc, X[5], 7);
    GGG(ccc, ddd, aaa2, bbb, X[1], 15);
    GGG(bbb, ccc, ddd, aaa2, X[3], 11);
    GGG(aaa2, bbb, ccc, ddd, X[7], 8);
    GGG(ddd, aaa2, bbb, ccc, X[14], 6);
    GGG(ccc, ddd, aaa2, bbb, X[6], 6);
    GGG(bbb, ccc, ddd, aaa2, X[9], 14);
    GGG(aaa2, bbb, ccc, ddd, X[11], 12);
    GGG(ddd, aaa2, bbb, ccc, X[8], 13);
    GGG(ccc, ddd, aaa2, bbb, X[12], 5);
    GGG(bbb, ccc, ddd, aaa2, X[2], 14);
    GGG(aaa2, bbb, ccc, ddd, X[10], 13);
    GGG(ddd, aaa2, bbb, ccc, X[0], 13);
    GGG(ccc, ddd, aaa2, bbb, X[4], 7);
    GGG(bbb, ccc, ddd, aaa2, X[13], 5);
    /* parallel round 4 */
    FFF(aaa2, bbb, ccc, ddd, X[8], 15);
    FFF(ddd, aaa2, bbb, ccc, X[6], 5);
    FFF(ccc, ddd, aaa2, bbb, X[4], 8);
    FFF(bbb, ccc, ddd, aaa2, X[1], 11);
    FFF(aaa2, bbb, ccc, ddd, X[3], 14);
    FFF(ddd, aaa2, bbb, ccc, X[11], 14);
    FFF(ccc, ddd, aaa2, bbb, X[15], 6);
    FFF(bbb, ccc, ddd, aaa2, X[0], 14);
    FFF(aaa2, bbb, ccc, ddd, X[5], 6);
    FFF(ddd, aaa2, bbb, ccc, X[12], 9);
    FFF(ccc, ddd, aaa2, bbb, X[2], 12);
    FFF(bbb, ccc, ddd, aaa2, X[13], 9);
    FFF(aaa2, bbb, ccc, ddd, X[9], 12);
    FFF(ddd, aaa2, bbb, ccc, X[7], 5);
    FFF(ccc, ddd, aaa2, bbb, X[10], 15);
    FFF(bbb, ccc, ddd, aaa2, X[14], 8);
    /* combine results */
    ddd += cc + MDbuf[1]; /* final result for MDbuf[0] */
    MDbuf[1] = MDbuf[2] + dd + aaa2;
    MDbuf[2] = MDbuf[3] + aa + bbb;
    MDbuf[3] = MDbuf[0] + bb + ccc;
    MDbuf[0] = ddd;
}
