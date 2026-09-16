#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Spline.h"
/////////////////////////////////////////////////////////////////////////////////////
// void CSpline::Setup3DSpline( CPlot3D& plot0, CPlot3D& plot1, CPlot3D& plot2, CPlot3D& plot3 )
// setup coeffs for 3D spline
// plot0 = plot[i-1], plot1 = plot[0], plot2 = plot[i+1], plot3 = plot[i+2]
void CSpline::Setup3DSpline(CPlot3D &plot0, CPlot3D &plot1, CPlot3D &plot2, CPlot3D &plot3)
{
    // 'a' coeff serie
    a0 = (plot0.x + 4.0f * plot1.x + plot2.x) * (1.0f / 6.0f);
    a1 = (-plot0.x + plot2.x) * (1.0f / 2.0f);
    a2 = (plot0.x - 2.0f * plot1.x + plot2.x) * (1.0f / 2.0f);
    a3 = (-plot0.x + 3.0f * (plot1.x - plot2.x) + plot3.x) * (1.0f / 6.0f);
    // 'b' coeff serie
    b0 = (plot0.y + 4.0f * plot1.y + plot2.y) * (1.0f / 6.0f);
    b1 = (-plot0.y + plot2.y) * (1.0f / 2.0f);
    b2 = (plot0.y - 2.0f * plot1.y + plot2.y) * (1.0f / 2.0f);
    b3 = (-plot0.y + 3.0f * (plot1.y - plot2.y) + plot3.y) * (1.0f / 6.0f);
    // 'c' coeff serie
    c0 = (plot0.z + 4.0f * plot1.z + plot2.z) * (1.0f / 6.0f);
    c1 = (-plot0.z + plot2.z) * (1.0f / 2.0f);
    c2 = (plot0.z - 2.0f * plot1.z + plot2.z) * (1.0f / 2.0f);
    c3 = (-plot0.z + 3.0f * (plot1.z - plot2.z) + plot3.z) * (1.0f / 6.0f);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSpline::Setup2DSpline( CPlot2D& plot0, CPlot2D& plot1, CPlot2D& plot2, CPlot2D& plot3 )
// setup coeffs for 2D spline
// plot0 = plot[i-1], plot1 = plot[0], plot2 = plot[i+1], plot3 = plot[i+2]
void CSpline::Setup2DSpline(CPlot2D &plot0, CPlot2D &plot1, CPlot2D &plot2, CPlot2D &plot3)
{
    // 'a' coeff serie
    a0 = (plot0.x + 4.0f * plot1.x + plot2.x) * (1.0f / 6.0f);
    a1 = (-plot0.x + plot2.x) * (1.0f / 2.0f);
    a2 = (plot0.x - 2.0f * plot1.x + plot2.x) * (1.0f / 2.0f);
    a3 = (-plot0.x + 3.0f * (plot1.x - plot2.x) + plot3.x) * (1.0f / 6.0f);
    // 'b' coeff serie
    b0 = (plot0.y + 4.0f * plot1.y + plot2.y) * (1.0f / 6.0f);
    b1 = (-plot0.y + plot2.y) * (1.0f / 2.0f);
    b2 = (plot0.y - 2.0f * plot1.y + plot2.y) * (1.0f / 2.0f);
    b3 = (-plot0.y + 3.0f * (plot1.y - plot2.y) + plot3.y) * (1.0f / 6.0f);
}
/////////////////////////////////////////////////////////////////////////////////////
