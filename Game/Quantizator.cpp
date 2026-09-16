#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Quantizator.h"
#include "Plot.h"

#include <vector>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <numeric>
/////////////////////////////////////////////////////////////////////////////////////
namespace NQuantizator // begin of namespace NQuantizator
{
/////////////////////////////////////////////////////////////////////////////////////
struct SParallelepiped
{
    float minx, miny, minz;
    float maxx, maxy, maxz;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SQuantizationEntry
{
public:
    CPlot3D pt;
    int nCount;

public:
    explicit SQuantizationEntry(const CPlot3D &ptNew)
        : pt(ptNew)
        , nCount(1)
    {
    }
    explicit SQuantizationEntry(const CPlot3D &ptNew, int nNewCount)
        : pt(ptNew)
        , nCount(nNewCount)
    {
    }

    friend const SQuantizationEntry operator+(const SQuantizationEntry &pt1,
                                              const SQuantizationEntry &pt2);
};
/////////////////////////////////////////////////////////////////////////////////////
typedef std::vector<SQuantizationEntry> CQuantizationVector;
typedef std::unordered_map<DWORD, DWORD> CQuantizationMap;
/////////////////////////////////////////////////////////////////////////////////////
inline const SQuantizationEntry operator+(const SQuantizationEntry &pt1,
                                          const SQuantizationEntry &pt2)
{
    return SQuantizationEntry(pt1.pt + pt2.pt * float(pt2.nCount), pt1.nCount + pt2.nCount);
}
/////////////////////////////////////////////////////////////////////////////////////
class CPlot3DComponentCompareFunctional
{
private:
    int nComponentIndex;

public:
    explicit CPlot3DComponentCompareFunctional(int nComponent)
        : nComponentIndex(nComponent)
    {
    }

    bool operator()(const SQuantizationEntry &plot1, const SQuantizationEntry &plot2)
    {
        return reinterpret_cast<const float *>(&plot1.pt)[nComponentIndex] <
               reinterpret_cast<const float *>(&plot2.pt)[nComponentIndex];
    }
};
/////////////////////////////////////////////////////////////////////////////////////
void QuanizateLayer(CQuantizationVector &plots, std::list<CQuantizationVector> &vectors)
{
    int i;
    //
    if (plots.empty())
        return;
    // first, calc rect
    SParallelepiped piped;
    piped.minx = piped.maxx = plots[0].pt.x;
    piped.miny = piped.maxy = plots[0].pt.y;
    piped.minz = piped.maxz = plots[0].pt.z;
    for (i = 1; i < plots.size(); ++i)
    {
        piped.minx = Min(piped.minx, plots[i].pt.x);
        piped.maxx = Max(piped.maxx, plots[i].pt.x);

        piped.miny = Min(piped.miny, plots[i].pt.y);
        piped.maxy = Max(piped.maxy, plots[i].pt.y);

        piped.minz = Min(piped.minz, plots[i].pt.z);
        piped.maxz = Max(piped.maxz, plots[i].pt.z);
    }
    // then, choose division side
    float fpSideValue = piped.maxx - piped.minx;
    int nSide = 0;
    if (fpSideValue < piped.maxy - piped.miny)
    {
        fpSideValue = piped.maxy - piped.miny;
        nSide = 1;
    }
    if (fpSideValue < piped.maxz - piped.minz)
    {
        fpSideValue = piped.maxz - piped.minz;
        nSide = 2;
    }
    //
    CPlot3DComponentCompareFunctional cmpfnct(nSide);
    std::sort(plots.begin(), plots.end(), cmpfnct);

    CQuantizationVector plots1;
    auto median = plots.begin();
    std::advance(median, plots.size() / 2);
    if (std::distance(plots.begin(), median) >= 1)
    {
        vectors.push_back(plots1);
        vectors.back().insert(vectors.back().begin(), plots.begin(), median);
    }
    if (std::distance(median, plots.end()) >= 1)
    {
        vectors.push_back(plots1);
        vectors.back().insert(vectors.back().begin(), median, plots.end());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void Quanizate(CPlot3DVector &plots, CPlot3DVector &palette, const int nDivisionDepth)
{
    std::list<CQuantizationVector> result;
    int i, j;
    //
    if (nDivisionDepth == 0)
    {
        CQuantizationVector plots1;
        result.push_back(plots1);
        for (auto it = plots.begin(); it != plots.end(); ++it)
            result.back().push_back(SQuantizationEntry(*it));
    }
    else
    {
        CQuantizationVector quantplots;
        CQuantizationMap quantmap;
        // compose quantization map
        for (auto it = plots.begin(); it != plots.end(); ++it)
        {
            const CPlot3D &pt = *it;
            DWORD dwKey = ((DWORD(pt.x * 127.0f) & 0x3FF) << 20) |
                          ((DWORD(pt.y * 127.0f) & 0x3FF) << 10) | ((DWORD(pt.z * 127.0f) & 0x3FF));
            ++quantmap[dwKey];
        }
        // fill vector from quanization map
        quantplots.reserve(quantmap.size());
        for (auto pos = quantmap.begin(); pos != quantmap.end(); ++pos)
        {
            DWORD dwValue, dwKey = pos->first;
            CPlot3D pt;
            // x
            dwValue = ((dwKey >> 20) & 0x3FF);
            if (dwValue > 512)
                dwValue |= 0xfffffc00;
            pt.x = float(bit_cast<long>(dwValue)) / 127.0f;
            // y
            dwValue = ((dwKey >> 10) & 0x3FF);
            if (dwValue > 512)
                dwValue |= 0xfffffc00;
            pt.y = float(bit_cast<long>(dwValue)) / 127.0f;
            // z
            dwValue = ((dwKey) & 0x3FF);
            if (dwValue > 512)
                dwValue |= 0xfffffc00;
            pt.z = float(bit_cast<long>(dwValue)) / 127.0f;

            quantplots.push_back(SQuantizationEntry(pt, pos->second));
        }
        // 1st division
        QuanizateLayer(quantplots, result);
        // perform other divisions
        for (i = 1; i < nDivisionDepth; ++i)
        {
            auto iter = result.begin();
            int nSize = result.size();
            for (j = 0; j < nSize; ++iter, ++j)
                QuanizateLayer(*iter, result);
            auto enditer = result.begin();
            std::advance(enditer, nSize);
            result.erase(result.begin(), enditer);
        }
    }
    // calculate average values for each vector
    for (auto iter = result.begin(); iter != result.end(); ++iter)
    {
        SQuantizationEntry plot = std::accumulate(iter->begin(), iter->end(),
                                                  SQuantizationEntry(VNULL, 0));
        plot.pt *= 1.0f / static_cast<float>(plot.nCount);
        palette.push_back(plot.pt);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D FindBestMatch(const CPlot3DVector &palette, const CPlot3D &plot)
{
    CPlot3D match = palette[0];
    for (auto iter = palette.begin(); iter != palette.end(); ++iter)
    {
        if (fabs2(plot - *iter) < fabs2(plot - match))
            match = *iter;
    }

    return match;
}
/////////////////////////////////////////////////////////////////////////////////////
}; // end of namespace NQuantizator
/////////////////////////////////////////////////////////////////////////////////////
