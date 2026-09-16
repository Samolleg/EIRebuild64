#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
// реальные значения при изменении 'alpha' от 0 до 1 изменяются находятся между 'p1' и 'p2'
class CBSpline
{
private:
    float a0, a1, a2, a3;

public:
    void Setup(const float p0, const float p1, const float p2, const float p3)
    {
        a0 = (p0 + 4.0f * p1 + p2) * (1.0f / 6.0f);
        a1 = (-p0 + p2) * (1.0f / 2.0f);
        a2 = (p0 - 2.0f * p1 + p2) * (1.0f / 2.0f);
        a3 = (-p0 + 3.0f * (p1 - p2) + p3) * (1.0f / 6.0f);
    }
    float Get(const float alpha) const { return ((a3 * alpha + a2) * alpha + a1) * alpha + a0; }
    float operator()(const float alpha) const { return Get(alpha); }
};
class CBSpline3D
{
private:
    CBSpline x, y, z;

public:
    void Setup(const CPlot3D &p0, const CPlot3D &p1, const CPlot3D &p2, const CPlot3D &p3)
    {
        x.Setup(p0.x, p1.x, p2.x, p3.x);
        y.Setup(p0.y, p1.y, p2.y, p3.y);
        z.Setup(p0.z, p1.z, p2.z, p3.z);
    }
    CPlot3D Get(const float alpha) const { return CPlot3D(x(alpha), y(alpha), z(alpha)); }
    CPlot3D operator()(const float alpha) const { return Get(alpha); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CSpline
{
private:
    float a0, a1, a2, a3;
    float b0, b1, b2, b3;
    float c0, c1, c2, c3;

public:
    CSpline() = default;

    void Setup3DSpline(CPlot3D &plot0, CPlot3D &plot1, CPlot3D &plot2, CPlot3D &plot3);
    void Setup2DSpline(CPlot2D &plot0, CPlot2D &plot1, CPlot2D &plot2, CPlot2D &plot3);

    CPlot3D Get3DPlot(const float alpha);
    CPlot2D Get2DPlot(const float alpha);
};

/////////////////////////////////////////////////////////////////////////////////////
struct SPolynom3
{
    float fpKoef[4];
    SPolynom3() { memset(fpKoef, 0, sizeof(fpKoef)); }
    // посчитать коэффициенты полинома, если в точке 0 его значение fpA0 и производная fpD0
    // а в точке fpAlpha fpA1 и fpD1 соответсвенно
    void Spline22(float fpA0, float fpD0, float fpA1, float fpD1, float fpAlpha);
    // посчитать коэффициенты полинома, если в точке 0 его значение fpA0 и производная fpD0
    // а в точке fpAlpha значение полинома fpA1 и его вторая производная в точке 0 равна 0
    void Spline21(float fpA0, float fpD0, float fpA1, float fpAlpha);
    // посчитать коэффициенты полинома, если в точке 0 его значение fpA0, а в точке fpAlpha
    // значение полинома fpA1, его первая производная fpD1, его вторая производная равна 0
    void Spline12(float fpA0, float fpA1, float fpD1, float fpAlpha);
    // посчитать прямую по двум точкам
    void Spline11(float fpA0, float fpA1, float fpAlpha);
    float Calc(float x);    // вычислить значение полинома
    float CalcDer(float x); // вычислить значение производной полинома
};

/////////////////////////////////////////////////////////////////////////////////////
struct SBezierCurve
{
public:
    CPlot3D ptA, ptB, ptC, ptD;

public:
    void Setup(const CPlot3D &ptP1, const CPlot3D &ptP2, const CPlot3D &ptP3, const CPlot3D &ptP4)
    {
        ptA = 3.0f * ptP2 - ptP1 - 3.0f * ptP3 + ptP4;
        ptB = 3.0f * ptP1 + 3.0f * ptP3 - 6.0f * ptP2;
        ptC = 3.0f * ptP2 - 3.0f * ptP1;
        ptD = ptP1;
    }
    CPlot3D GetPlot(float t) const { return ((ptA * t + ptB) * t + ptC) * t + ptD; }
    CPlot3D operator()(float t) const { return ((ptA * t + ptB) * t + ptC) * t + ptD; }
};
/////////////////////////////////////////////////////////////////////////////////////
inline void SPolynom3::Spline22(float fpA0, float fpD0, float fpA1, float fpD1, float fpAlpha)
{
    float fpR, fpAlpha1;
    fpKoef[0] = fpA0;
    fpKoef[1] = fpD0;
    fpAlpha1 = 1 / fpAlpha;
    fpR = 2 * fpAlpha1 * (fpA1 - fpKoef[0] - fpKoef[1] * fpAlpha);
    fpKoef[3] = fpAlpha1 * fpAlpha1 * (fpD1 - fpKoef[1] - fpR); // fpD1 - 2*fpA1 + 2*fpA0 + fpD0;
    fpKoef[2] = 0.5f * fpAlpha1 *
                (fpR - 2 * fpKoef[3] * fpAlpha * fpAlpha); // fpA1 - fpA0 - fpD0 - fpKoef[3];
}

inline void SPolynom3::Spline21(float fpA0, float fpD0, float fpA1, float fpAlpha)
{
    float fpAlpha1;
    fpKoef[0] = fpA0;
    fpKoef[1] = fpD0;
    fpAlpha1 = 1 / fpAlpha;
    fpKoef[3] = fpAlpha1 * fpAlpha1 * fpAlpha1 * (fpA1 - fpKoef[0] - fpKoef[1] * fpAlpha);
    fpKoef[2] = 0;
}

inline void SPolynom3::Spline12(float fpA0, float fpA1, float fpD1, float fpAlpha)
{
    float fpAlpha1;
    fpKoef[0] = fpA0;
    fpAlpha1 = 1 / fpAlpha;
    fpKoef[3] = fpAlpha1 * fpAlpha1 * (fpAlpha1 * (fpA1 - fpA0) - fpD1);
    fpKoef[2] = -3 * fpKoef[3] * fpAlpha;
    fpKoef[1] = fpD1 + 3 * fpKoef[3] * fpAlpha * fpAlpha;
}

inline void SPolynom3::Spline11(float fpA0, float fpA1, float fpAlpha)
{
    fpKoef[0] = fpA0;
    fpKoef[1] = (fpA1 - fpA0) / fpAlpha;
    fpKoef[2] = fpKoef[3] = 0;
}

inline float SPolynom3::Calc(float x)
{
    return ((fpKoef[3] * x + fpKoef[2]) * x + fpKoef[1]) * x + fpKoef[0];
}

inline float SPolynom3::CalcDer(float x)
{
    return (3 * fpKoef[3] * x + 2 * fpKoef[2]) * x + fpKoef[1];
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// __inline CPlot3D CSpline::Get3DPlot( const float alpha )
// get spline-interpolated value with coeff 'alpha' of the pre-setup plots in 3D space
__inline CPlot3D CSpline::Get3DPlot(const float alpha)
{
    return CPlot3D(((a3 * alpha + a2) * alpha + a1) * alpha + a0,
                   ((b3 * alpha + b2) * alpha + b1) * alpha + b0,
                   ((c3 * alpha + c2) * alpha + c1) * alpha + c0);
}
/////////////////////////////////////////////////////////////////////////////////////
// __inline CPlot2D CSpline::Get2DPlot( const float alpha )
// get spline-interpolated value with coeff 'alpha' of the pre-setup plots in 2D space
__inline CPlot2D CSpline::Get2DPlot(const float alpha)
{
    return CPlot2D(((a3 * alpha + a2) * alpha + a1) * alpha + a0,
                   ((b3 * alpha + b2) * alpha + b1) * alpha + b0);
}
/////////////////////////////////////////////////////////////////////////////////////
