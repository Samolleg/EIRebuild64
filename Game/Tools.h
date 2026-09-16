#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include <assert.h>
/////////////////////////////////////////////////////////////////////////////////////
// different constants with 'pi'
const double pi = 3.141592653589793238462;
const float FP_PI = 3.1415926536f;
const float FP_2PI = FP_PI * 2.0f;
const float FP_PI2 = FP_PI / 2.0f;
const float FP_PI4 = FP_PI / 4.0f;
const float FP_INV_PI = 1.0f / FP_PI;
const float FP_EPSILON = 1e-12f;
const float FP_EPSILON2 = FP_EPSILON * FP_EPSILON;
//
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ZERO(a) (memset(&a, 0, sizeof(a)))

template <class T>
inline void FREE(T *&pointer)
{
    if (pointer == 0)
        ASSERT("Deleting NULL pointer.\n" && 0);
    else
        delete pointer;
    pointer = 0;
}

const DWORD FP_ONE_BITS = 0x3F800000;
inline DWORD FP_BITS(float fp)
{
    return *reinterpret_cast<DWORD *>(&fp);
}
inline DWORD FP_ABS_BITS(float fp)
{
    return (FP_BITS(fp) & 0x7FFFFFFF);
}
inline DWORD FP_SIGN_BIT(float fp)
{
    return (FP_BITS(fp) & 0x80000000);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// standard is long long but msvc does not support this name
typedef __int64 int64;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE_OUT, class TYPE_IN>
inline TYPE_OUT bit_cast(TYPE_IN val)
{
    return *reinterpret_cast<TYPE_OUT *>(&val);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE_OUT, class TYPE_IN>
inline TYPE_OUT *ptr_cast(TYPE_IN *pVal)
{
    return reinterpret_cast<TYPE_OUT *>(pVal);
}
/////////////////////////////////////////////////////////////////////////////////////
// Return the next power of 2 higher than the input
// If the input is already a power of 2, the output will be the same as the input.
// Got this from Brian Sharp's sweng mailing list.
inline int GetNextPow2(DWORD n)
{
    n -= 1;

    n |= n >> 16;
    n |= n >> 8;
    n |= n >> 4;
    n |= n >> 2;
    n |= n >> 1;

    return n + 1;
}
inline int GetNextPow2(int n)
{
    return GetNextPow2(DWORD(n));
}
/////////////////////////////////////////////////////////////////////////////////////
// 'magic' division on '2' and '4' in float
inline float Div2(float fVal)
{
    _asm
    {
		mov eax, [fVal]
		add eax, 0xff800000
		mov [fVal], eax
    }
    return fVal;
}
inline float Div4(float fVal)
{
    _asm
    {
		mov eax, [fVal]
		add eax, 0xff000000
		mov [fVal], eax
    }
    return fVal;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE_RESULT, class TYPE_VAL>
inline TYPE_RESULT IsValInRange(TYPE_VAL valVal, TYPE_VAL valMin, TYPE_VAL valMax)
{
    return ((valVal >= valMin) && (valVal <= valMax));
}
template <class TYPE_RESULT, class TYPE_VAL>
inline TYPE_RESULT IsValInRangeLeft(TYPE_VAL valVal, TYPE_VAL valMin, TYPE_VAL valMax)
{
    return ((valVal >= valMin) && (valVal < valMax));
}
template <class TYPE_RESULT, class TYPE_VAL>
inline TYPE_RESULT IsValInRangeRight(TYPE_VAL valVal, TYPE_VAL valMin, TYPE_VAL valMax)
{
    return ((valVal > valMin) && (valVal <= valMax));
}
template <class TYPE_RESULT, class TYPE_VAL>
inline TYPE_RESULT IsValInRangeCenter(TYPE_VAL valVal, TYPE_VAL valMin, TYPE_VAL valMax)
{
    return ((valVal > valMin) && (valVal < valMax));
}
/////////////////////////////////////////////////////////////////////////////////////
// Newton-Raffson reciprocal
// return: 1.0f/fp
inline float nr_rcpr(float fp)
{
    int _i = 2 * FP_ONE_BITS - *reinterpret_cast<int *>(&fp);
    float r = *reinterpret_cast<float *>(&_i);
    return (r * (2.0f - fp * r));
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float  fast_exp( float fp )
// fast approx. exponent
inline float fast_exp(float fp)
{
    float e = -1.44269504f * static_cast<float>(0x00800000) * fp;
    int _i = static_cast<int>(e) + FP_ONE_BITS;
    return *reinterpret_cast<float *>(&_i);
}
/////////////////////////////////////////////////////////////////////////////////////
// square and triple functions
template <class TYPE>
inline TYPE square(TYPE x)
{
    return x * x;
}

inline float sqr(float a)
{
    return a * a;
}
inline int sqr(int a)
{
    return a * a;
}

template <class TYPE>
inline TYPE triple(TYPE x)
{
    return x * x * x;
}
/////////////////////////////////////////////////////////////////////////////////////
inline float FlipSign(float fVal)
{
    _asm
    {
		mov eax, [fVal]
		xor eax, 0x80000000
		mov [fVal], eax
    }
    return fVal;
}
/////////////////////////////////////////////////////////////////////////////////////
// signum function
template <class TYPE>
inline TYPE Sign(const TYPE x)
{
    if (x < 0)
        return -1;
    else if (x > 0)
        return +1;
    else
        return 0;
}
// calculates sign for integer variable, returns -1, 0, 1. template specialization
#pragma warning(disable : 4035)
template <>
inline int Sign<int>(const int nVal)
{
    _asm
    {
		mov eax, nVal
		test eax, 0x7FFFFFFF
		setne cl
		sar eax, 31
		or al, cl
    }
}
template <>
inline short int Sign<short int>(const short int nVal)
{
    _asm
    {
		mov ax, nVal
		test ax, 0x7FFF
		setne cl
		sar ax, 15
		or al, cl
    }
}
#pragma warning(default : 4035)
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// dice and random functions
inline int Random(const int iMax)
{
    return (rand() * iMax / RAND_MAX) % iMax;
} // from 0 to max - 1
inline int Dice(const int iMax)
{
    return 1 + Random(iMax);
} // from 1 to max
// Calc normalized canonical line equation ax+by+c=0
inline void GetLineEq(float x1, float x2, float y1, float y2, float *a, float *b, float *c);
// float as dword functions
inline DWORD Float2DW(float fpVar);
inline BOOL IsNegative(float fpVar);
inline BOOL IsPositive(float fpVar);
inline BOOL IsZero(float fpVar);
// Fast float sqrt
inline float FSqrt(float fpVal);
/////////////////////////////////////////////////////////////////////////////////////
// radian <=> degree conversion functions
inline float ToRadian(const float angle)
{
    return angle * (pi / 180.0f);
}

inline float ToDegree(const float angle)
{
    return angle * (180.0f / pi);
}

template <class TYPE>
inline TYPE NormalizeAngleInDegree(const TYPE angle)
{
    return TYPE(angle % 360.0);
}

template <class TYPE>
inline TYPE SignumNormalizeAngleInDegree(const TYPE angle)
{
    return TYPE((angle + 180 * Sign(angle)) % 360 - 180 * Sign(angle));
}

template <class TYPE>
inline TYPE NormalizeAngleInRadian(const TYPE angle)
{
    return TYPE(angle % (2.0 * pi));
}
/////////////////////////////////////////////////////////////////////////////////////
inline void MemSetDWord(DWORD *lpData, const DWORD value, const int nCount)
{
    _asm
    {
		push edi
		mov ecx, nCount
		mov edi, lpData
		mov eax, value
		rep stosd
		pop edi
    }
}

inline void MemSetInt(int *lpData, const int value, const int nCount)
{
    _asm
    {
		push edi
		mov ecx, nCount
		mov edi, lpData
		mov eax, value
		rep stosd
		pop edi
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// inline float CorrectCosAngle( float angle )
// correct value to fit in range [-1...1] (for preparing acos/asin call value)
inline float CorrectCosAngle(float angle)
{
    if (angle > 1)
        angle = 1;
    else if (angle < -1)
        angle = -1;

    return angle;
}
/////////////////////////////////////////////////////////////////////////////////////
// very fast float-to-int conversion. uses current FPU rounding state
inline int MINT(const float fpVar)
{
    int nRet;
    __asm {
		fld dword ptr fpVar
		fistp nRet
    }
    ;
    return nRet;
}
// the same as MINT
inline int Float2Int(const float fpVar)
{
    int nRet;
    __asm {
		fld dword ptr fpVar
		fistp nRet
    }
    ;
    return nRet;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_lt( float x, float y, float val1, float val2 )
// very fast comparison: 'x' < 'y' ? val1 : val2
inline float select_lt(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		fld         dword ptr [x]
		fcomp       dword ptr [y]
        // store compare flags
		fnstsw      ax
            // test comparison result and set '1' or '0'
            // load 'val1' and 'val2'
		mov					edx, [val1]
		mov					ecx, [val2]
            // create mask for merging
		and					eax, 0100h
		shl					eax, 23
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_gt( float x, float y, float val1, float val2 )
// very fast comparison: 'x' > 'y' ? val1 : val2
inline float select_gt(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		fld         dword ptr [x]
		fcomp       dword ptr [y]
        // store compare flags
		fnstsw      ax
            // test comparison result and set '1' or '0'
            // load 'val1' and 'val2'
		mov					edx, [val1]
		mov					ecx, [val2]
        // create mask for merging
		test        ah, 41h
		sete				al
		shl					eax, 31
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_le( float x, float y, float val1, float val2 )
// very fast comparison: 'x' <= 'y' ? val1 : val2
inline float select_le(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		fld         dword ptr [x]
		fcomp       dword ptr [y]
        // store compare flags
		fnstsw      ax
            // test comparison result and set '1' or '0'
            // load 'val1' and 'val2'
		mov					edx, [val1]
		mov					ecx, [val2]
        // create mask for merging
		test        ah, 41h
		setne				al
		shl					eax, 31
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_ge( float x, float y, float val1, float val2 )
// very fast comparison: 'x' >= 'y' ? val1 : val2
inline float select_ge(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		fld         dword ptr [x]
		fcomp       dword ptr [y]
        // store compare flags
		fnstsw      ax
            // test comparison result and set '1' or '0'
            // load 'val1' and 'val2'
		mov					edx, [val1]
		mov					ecx, [val2]
            // create mask for merging
		and					eax, 0100h
		xor					eax, 0100h
		shl					eax, 23
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_eq( float x, float y, float val1, float val2 )
// very fast comparison: 'x' == 'y' ? val1 : val2
inline float select_eq(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		mov edx, [x]
		cmp edx, [y]
        // test comparison result and set '1' or '0'
        // load 'val1' and 'val2'
        // create mask for merging
		sete				al
		mov					edx, [val1]
		shl					eax, 31
		mov					ecx, [val2]
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_ne( float x, float y, float val1, float val2 )
// very fast comparison: 'x' != 'y' ? val1 : val2
inline float select_ne(float x, float y, float val1, float val2)
{
    float z;
    _asm
    {
        // loading 'x' and compare with 'y'
		xor					eax, eax
		mov edx, [x]
		cmp edx, [y]
        // test comparison result and set '1' or '0'
        // load 'val1' and 'val2'
        // create mask for merging
		setne				al
		mov					edx, [val1]
		shl					eax, 31
		mov					ecx, [val2]
		sar					eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and					edx, eax
		not					eax
		and					ecx, eax
		or					edx, ecx

		mov					[z], edx
    }
    return z;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline BYTE CheckForViewingFrustum( float xp, float yp, float zp, float wp )
// performs next check:
//			( (xp <= -wp)      ) |
//			( (xp >   wp) << 1 ) |
//			( (yp <= -wp) << 2 ) |
//			( (yp >   wp) << 3 ) |
//			( (zp <=  0 ) << 4 ) |
//			( (zp >   wp) << 5 );
inline BYTE CheckForViewingFrustum(float xp, float yp, float zp, float wp)
{
    float wp2 = wp + wp;
    BYTE value;

    xp += wp;
    yp += wp;

    _asm
        {         // xp <= 0, yp <= 0, zp <= 0
 		mov 	edx, xp
 		mov 	eax, yp
 		shr 	edx, 31
 		mov 	ecx, zp
 		shr 	eax, 31
 		shr 	ecx, 31
 		shl 	eax, 2
 		shl 	ecx, 4
 		or 		edx, eax
                  // xp > wp2, yp > wp2, zp > wp
 		fld 	[wp2]
 		fcomp [xp]
 		or 		edx, ecx
 		fnstsw ax
 		fld 	[wp2]
 		and 	eax, 0100h
 		fcomp [yp]
 		shr 	eax, 7
 		or 		edx, eax
 		fnstsw ax
 		fld 	[wp]
 		and 	eax, 0100h
 		fcomp [zp]
 		shr 	eax, 5
 		or 		edx, eax
 		fnstsw ax
 		and 	eax, 0100h
 		shr 	eax, 3
                  // form return value in eax and move it to 'value'
		or 		eax, edx
		mov [value], al
        }
    ;

    return value;
}
/////////////////////////////////////////////////////////////////////////////////////
// min/max functions
template <class TYPE>
inline TYPE Min(TYPE val1, TYPE val2)
{
    return (val1 < val2 ? val1 : val2);
}
template <class TYPE>
inline TYPE Max(TYPE val1, TYPE val2)
{
    return (val1 > val2 ? val1 : val2);
}
/////////////////////////////////////////////////////////////////////////////////////
// returns minimum of two float values
template <>
inline float Min<float>(float a, float b)
{
    float fpRet;
    _asm
    {
        // comparing
		fld         dword ptr [b]
		fcomp		dword ptr [a]
		fnstsw      ax
		mov			ecx, dword ptr [b]
		shl			eax, 23
		sar			eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and			ecx, eax
		not			eax
		and			eax, dword ptr [a]
		or			eax, ecx
		mov			[fpRet], eax
    }
    return fpRet;
}
// returns minimum of two float values
template <>
inline float Max<float>(float a, float b)
{
    float fpRet;
    _asm
    {
        // comparing
		fld         dword ptr [a]
		fcomp		dword ptr [b]
		fnstsw      ax
		mov			ecx, dword ptr [b]
		shl			eax, 23
		sar			eax, 31
            // merging: (val1 & mask) | (val2 & ~mask)
		and			ecx, eax
		not			eax
		and			eax, dword ptr [a]
		or			eax, ecx
		mov			[fpRet], eax
    }
    return fpRet;
}
/////////////////////////////////////////////////////////////////////////////////////
// Calc normalized canonical line equation ax+by+c=0
inline void GetLineEq(float x1, float x2, float y1, float y2, float *a, float *b, float *c)
{
    float ta = y1 - y2;
    float tb = x2 - x1;
    float tc = -x1 * ta - y1 * tb;

    float fpMod = ta * ta + tb * tb;
    if (fpMod < 1e-7)
    {
        *a = 0.7071f;
        *b = -0.7071f;
        *c = 0;
        return;
    }

    float sq = FSqrt(fpMod);

    *a = ta / sq;
    *b = tb / sq;
    *c = tc / sq;
}

/////////////////////////////////////////////////////////////////////////////////////
//	Floats as dwords
inline DWORD Float2DW(float fpVar)
{
    return *(reinterpret_cast<unsigned int *>(&fpVar));
}
inline BOOL IsNegative(float fpVar)
{
    return *(reinterpret_cast<unsigned int *>(&fpVar)) & 0x80000000;
}
inline BOOL IsPositive(float fpVar)
{
    return (*(reinterpret_cast<unsigned int *>(&fpVar)) & 0x80000000) == 0;
}
inline BOOL IsZero(float fpVar)
{
    return *(reinterpret_cast<unsigned int *>(&fpVar)) == 0;
}
/*
inline float fabs( const float fpValue )
{
        _asm
        {
                mov		eax, [fpValue]
                and		eax, 7FFFFFFFh
                mov		[fpValue], eax
        }
        return fpValue;
}
*/
inline float fabs2(float x)
{
    return x * x;
}
inline float fabs2(float x, float y, float z)
{
    return x * x + y * y + z * z;
}
inline float fabs(float x, float y, float z)
{
    return (float)sqrt(x * x + y * y + z * z);
}
inline float fabs2(float x, float y)
{
    return x * x + y * y;
}
inline float fabs(float x, float y)
{
    return (float)hypot(x, y);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline bool Normalize(TYPE &x, TYPE &y)
{
    TYPE u = fabs2(x, y);
    if (fabs(u - TYPE(1)) < FP_EPSILON)
        return true; // already normalized
    if (u < FP_EPSILON2)
        return false; // can't normalize
    u = static_cast<TYPE>(TYPE(1) / sqrt(u));
    x *= u;
    y *= u;
    return true;
}
template <class TYPE>
inline bool Normalize(TYPE &x, TYPE &y, TYPE &z)
{
    TYPE u = fabs2(x, y, z);
    if (fabs(u - TYPE(1)) < FP_EPSILON)
        return true; // already normalized
    if (u < FP_EPSILON2)
        return false; // can't normalize
    u = static_cast<TYPE>(TYPE(1) / sqrt(u));
    x *= u;
    y *= u;
    z *= u;
    return true;
}
template <class TYPE>
inline bool Normalize(TYPE &x, TYPE &y, TYPE &z, TYPE &w)
{
    TYPE u = fabs2(x, y, z, w);
    if (fabs(u - TYPE(1)) < FP_EPSILON)
        return true; // already normalized
    if (u < FP_EPSILON2)
        return false; // can't normalize
    u = static_cast<TYPE>(TYPE(1) / sqrt(u));
    x *= u;
    y *= u;
    z *= u;
    w *= u;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float sqrtf(float);
inline float FSqrt(float fpVal)
{
    return sqrtf(fpVal);
}
inline float ACos(float fpCos)
{
    return static_cast<float>(acos(fpCos));
}
/////////////////////////////////////////////////////////////////////////////////////
#define MINIMIZE_INT(nToMin, nHow)                                                      \
    _asm mov ecx, nToMin _asm cmp ecx, nHow _asm setl al _asm shl eax, 31 _asm sar eax, \
        31 _asm and nToMin, eax _asm not eax _asm and eax, nHow _asm or nToMin, eax

#define MINIMIZE_UINT(nToMin, nHow)                                                     \
    _asm mov ecx, nToMin _asm cmp ecx, nHow _asm setb al _asm shl eax, 31 _asm sar eax, \
        31 _asm and nToMin, eax _asm not eax _asm and eax, nHow _asm or nToMin, eax
/////////////////////////////////////////////////////////////////////////////////////
inline void Copy64Bits(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
    }
}

inline void Copy128Bits(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
		fild qword ptr [ ECX + 8 ]
		fistp qword ptr [ EDX + 8 ]
    }
}

inline void Copy256Bits(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
		fild qword ptr [ ECX + 8 ]
		fistp qword ptr [ EDX + 8 ]
		fild qword ptr [ ECX + 16 ]
		fistp qword ptr [ EDX + 16 ]
		fild qword ptr [ ECX + 24 ]
		fistp qword ptr [ EDX + 24 ]
    }
}
/////////////////////////////////////////////////////////////////////////////////////
inline void Copy8Bytes(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
    }
}

inline void Copy16Bytes(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
		fild qword ptr [ ECX + 8 ]
		fistp qword ptr [ EDX + 8 ]
    }
}

inline void Copy32Bytes(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		fistp qword ptr [ EDX ]
		fild qword ptr [ ECX + 8 ]
		fistp qword ptr [ EDX + 8 ]
		fild qword ptr [ ECX + 16 ]
		fistp qword ptr [ EDX + 16 ]
		fild qword ptr [ ECX + 24 ]
		fistp qword ptr [ EDX + 24 ]
    }
}
/////////////////////////////////////////////////////////////////////////////////////
const DWORD CPUID_MMX_FEATURE_PRESENT = 0x00800000;
const DWORD CPUID_SSE_FEATURE_PRESENT = 0x02000000;
#define GET_CPUID __asm _emit 0x0f __asm _emit 0xa2
inline void GetCPUID(char str[13], DWORD &dwRes)
{
    static char szRez[13];
    static DWORD dwRes1;
    szRez[12] = 0;
    _asm {
		pusha // keep compiler happy
		pushfd // get extended flags
		pop eax // store extended flags in eax
		mov ebx, eax // save current flags
		xor eax, 200000h // toggle bit 21
		push eax // put new flags on stack
		popfd // flags updated now in flags
		pushfd // get extended flags
		pop eax // store extended flags in eax
		xor eax, ebx // if bit 21 r/w then eax <> 0
		je q // can't toggle id bit (21) no cpuid here

		mov	eax, 1
		GET_CPUID // perform CPUID command
		mov dwRes1, edx // store CPUID in dwRes1
		mov dword ptr[szRez], eax // store ASCII vendor name
		mov dword ptr[szRez]+4, ebx // in the szRez
		mov dword ptr[szRez]+8, ecx //
	q:
		popa
    }
    strcpy(str, szRez);
    dwRes = dwRes1;
}
#undef GET_CPUID

// for each по CList с возможностью удаления элемента (через CList::RemoveAt)
template <class Arg, class ArgType>
void ForEach(CList<Arg, ArgType> &list, std::function<void(ArgType arg, POSITION pos)> fn)
{
    for (auto pos = list.GetHeadPosition(); pos;)
    {
        auto posCopy = pos;
        auto val = list.GetNext(pos);
        fn(val, posCopy);
    }
}

// for each по CList без возможности удаления элемента
template <class Type, class ArgType>
void ForEach(CList<Type, ArgType> &list, std::function<void(ArgType arg)> fn)
{
    POSITION pos = list.GetHeadPosition();
    while (pos)
        fn(list.GetNext(pos));
}

/////////////////////////////////////////////////////////////////////////////////////
