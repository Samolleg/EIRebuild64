#pragma once

#include "Plot.h"

#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
namespace NQuantizator {
typedef std::vector<CPlot3D> CPlot3DVector;
void Quanizate(CPlot3DVector &input, CPlot3DVector &palette, const int nDivisionDepth);
CPlot3D FindBestMatch(const CPlot3DVector &palette, const CPlot3D &plot);
} // namespace NQuantizator
