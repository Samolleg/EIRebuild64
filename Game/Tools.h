#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include <assert.h>
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <xmmintrin.h>
#include <type_traits>
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

template <class TYPE_OUT, class TYPE_IN>
inline TYPE_OUT bit_cast(TYPE_IN val)
{
    static_assert(sizeof(TYPE_OUT) == sizeof(TYPE_IN), "bit_cast requires equal sizes");
    static_assert(std::is_trivially_copyable<TYPE_OUT>::value &&
                  std::is_trivially_copyable<TYPE_IN>::value, "bit_cast requires trivial types");
    TYPE_OUT result;
    std::memcpy(&result, &val, sizeof(result));
    return result;
}

const DWORD FP_ONE_BITS = 0x3F800000;
inline DWORD FP_BITS(float fp)
{
    return bit_cast<DWORD>(fp);
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
inline size_t GetNextPow2(size_t n)
{
    --n;
    for (size_t shift = 1; shift < sizeof(n) * 8; shift *= 2)
        n |= n >> shift;
    return n + 1;
}
/////////////////////////////////////////////////////////////////////////////////////
// 'magic' division on '2' and '4' in float
inline float Div2(float fVal)
{
    return bit_cast<float>(FP_BITS(fVal) + static_cast<DWORD>(0xff800000));
}
inline float Div4(float fVal)
{
    return bit_cast<float>(FP_BITS(fVal) + static_cast<DWORD>(0xff000000));
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
    DWORD _i = 2 * FP_ONE_BITS - FP_BITS(fp);
    float r = bit_cast<float>(_i);
    return (r * (2.0f - fp * r));
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float  fast_exp( float fp )
// fast approx. exponent
inline float fast_exp(float fp)
{
    float e = -1.44269504f * static_cast<float>(0x00800000) * fp;
    int _i = static_cast<int>(e) + FP_ONE_BITS;
    return bit_cast<float>(_i);
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
    return bit_cast<float>(FP_BITS(fVal) ^ static_cast<DWORD>(0x80000000));
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
template <>
inline int Sign<int>(const int nVal)
{
    return (nVal > 0) - (nVal < 0);
}
template <>
inline short int Sign<short int>(const short int nVal)
{
    return static_cast<short>((nVal > 0) - (nVal < 0));
}
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
inline void MemSetDWord(DWORD *lpData, const DWORD value, const size_t nCount)
{
    std::fill_n(lpData, nCount, value);
}

inline void MemSetInt(int *lpData, const int value, const size_t nCount)
{
    std::fill_n(lpData, nCount, value);
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
// Float-to-int conversion with explicit signed32 overflow behaviour.
// Explicit conversion policy; ordinary execution starts in nearest/even mode.
enum class FloatRounding
{
    NearestEven = FE_TONEAREST,
    Chop = FE_TOWARDZERO,
    Down = FE_DOWNWARD,
    Up = FE_UPWARD
};

// fenv controls the x64 SSE environment. Restore rounding on every exit; leave
// exception flags alone. Scope this object to the original rounding region.
class ScopedFloatRounding
{
    int saved;
    bool active = true;

public:
    explicit ScopedFloatRounding(FloatRounding mode)
        : saved(std::fegetround())
    {
        std::fesetround(static_cast<int>(mode));
    }
    void Restore()
    {
        if (active)
        {
            std::fesetround(saved);
            active = false;
        }
    }
    ~ScopedFloatRounding() { Restore(); }
    ScopedFloatRounding(const ScopedFloatRounding &) = delete;
    ScopedFloatRounding &operator=(const ScopedFloatRounding &) = delete;
};

inline int RoundFloatToInt(float value, FloatRounding mode)
{
    if (mode == FloatRounding::Chop)
        return _mm_cvttss_si32(_mm_set_ss(value));

    // Explicit modes are independent of the caller's rounding environment.
    const ScopedFloatRounding rounding(mode);
    return _mm_cvtss_si32(_mm_set_ss(value));
}

// Read MXCSR at runtime, also in callers without fenv_access enabled.
// Masked invalid conversions produce INT32_MIN, as the original x87 FISTP did.
inline int MINT(const float fpVar)
{
    return _mm_cvtss_si32(_mm_set_ss(fpVar));
}
// the same as MINT
inline int Float2Int(const float fpVar)
{
    return MINT(fpVar);
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_lt( float x, float y, float val1, float val2 )
// very fast comparison: 'x' < 'y' ? val1 : val2
inline float select_lt(float x, float y, float val1, float val2)
{
    // x87 C0 is also set for unordered operands.
    return !(x >= y) ? val1 : val2;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_gt( float x, float y, float val1, float val2 )
// very fast comparison: 'x' > 'y' ? val1 : val2
inline float select_gt(float x, float y, float val1, float val2)
{
    return x > y ? val1 : val2;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_le( float x, float y, float val1, float val2 )
// very fast comparison: 'x' <= 'y' ? val1 : val2
inline float select_le(float x, float y, float val1, float val2)
{
    return !(x > y) ? val1 : val2;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_ge( float x, float y, float val1, float val2 )
// very fast comparison: 'x' >= 'y' ? val1 : val2
inline float select_ge(float x, float y, float val1, float val2)
{
    return x >= y ? val1 : val2;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_eq( float x, float y, float val1, float val2 )
// very fast comparison: 'x' == 'y' ? val1 : val2
inline float select_eq(float x, float y, float val1, float val2)
{
    // These two selectors compare representations, including NaNs and +/-0.
    return FP_BITS(x) == FP_BITS(y) ? val1 : val2;
}
/////////////////////////////////////////////////////////////////////////////////////
// inline float select_ne( float x, float y, float val1, float val2 )
// very fast comparison: 'x' != 'y' ? val1 : val2
inline float select_ne(float x, float y, float val1, float val2)
{
    return FP_BITS(x) != FP_BITS(y) ? val1 : val2;
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
    const float wp2 = wp + wp;
    xp += wp;
    yp += wp;
    return static_cast<BYTE>((FP_BITS(xp) >> 31) |
                            (static_cast<DWORD>(!(wp2 >= xp)) << 1) |
                            ((FP_BITS(yp) >> 31) << 2) |
                            (static_cast<DWORD>(!(wp2 >= yp)) << 3) |
                            ((FP_BITS(zp) >> 31) << 4) |
                            (static_cast<DWORD>(!(wp >= zp)) << 5));
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
    // Keep the first operand on ties and the second on unordered comparisons.
    return !(b >= a) ? b : a;
}
// returns minimum of two float values
template <>
inline float Max<float>(float a, float b)
{
    return !(a >= b) ? b : a;
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
    return FP_BITS(fpVar);
}
inline BOOL IsNegative(float fpVar)
{
    return static_cast<BOOL>(FP_BITS(fpVar) & static_cast<DWORD>(0x80000000));
}
inline BOOL IsPositive(float fpVar)
{
    return (FP_BITS(fpVar) & static_cast<DWORD>(0x80000000)) == 0;
}
inline BOOL IsZero(float fpVar)
{
    return FP_BITS(fpVar) == 0;
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
template <class T, class U>
inline void MINIMIZE_INT(T &nToMin, U nHow)
{
    const int32_t current = static_cast<int32_t>(nToMin);
    const int32_t limit = static_cast<int32_t>(nHow);
    nToMin = static_cast<T>(current < limit ? current : limit);
}

template <class T, class U>
inline void MINIMIZE_UINT(T &nToMin, U nHow)
{
    const uint32_t current = static_cast<uint32_t>(nToMin);
    const uint32_t limit = static_cast<uint32_t>(nHow);
    nToMin = static_cast<T>(current < limit ? current : limit);
}
/////////////////////////////////////////////////////////////////////////////////////
inline void Copy64Bits(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 8);
}

inline void Copy128Bits(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 16);
}

inline void Copy256Bits(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 32);
}
/////////////////////////////////////////////////////////////////////////////////////
inline void Copy8Bytes(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 8);
}

inline void Copy16Bytes(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 16);
}

inline void Copy32Bytes(void *fpDst, const void *fpSrc)
{
    std::memcpy(fpDst, fpSrc, 32);
}
/////////////////////////////////////////////////////////////////////////////////////
const DWORD CPUID_MMX_FEATURE_PRESENT = 0x00800000;
const DWORD CPUID_SSE_FEATURE_PRESENT = 0x02000000;
inline void GetCPUID(char str[13], DWORD &dwRes)
{
    int registers[4];
    __cpuid(registers, 1);
    // Preserve the legacy leaf-1 EAX/EBX/ECX bytes, not a leaf-0 vendor ID.
    std::memcpy(str, registers, 12);
    str[12] = 0;
    dwRes = static_cast<DWORD>(registers[3]);
}

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
