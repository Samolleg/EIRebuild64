#pragma once

const DWORD FIG_SIGNATURE_MASK = 0x00FFFFFF;
const DWORD FIG_SIGNATURE_FIGN = 0x00474946;
const DWORD FIG_SIGNATURE_FIG1 = 0x31474946;
const DWORD FIG_SIGNATURE_FIG6 = 0x36474946;
const DWORD FIG_SIGNATURE_FIG8 = 0x38474946;
/////////////////////////////////////////////////////////////////////////////////////
// ** data placement in figure file:
// ** HEADER
// ** (16x) bounding box data (depend on format)
// ** (16x) vertices (depend on format)
// ** (16x) normales
// ** (16x) maps
// ** indices
// ** components
// ** light components
/////////////////////////////////////////////////////////////////////////////////////
struct SFigureFormatHeader
{
public:
    DWORD dwSignature;          // figure file signature (fig1, fig6, fig8)
    DWORD dwNumVertices;        // number of geometry vertices (result vertices) in SXYZW4 format
    DWORD dwNumNormales;        // number of normales in SXYZW4 format
    DWORD dwNumMaps;            // number of mappings in CPlot2D format
    DWORD dwNumIndices;         // number of triangle indices
    DWORD dwNumComponents;      // number of components, TLvertices composed from data from
    DWORD dwNumLightComponents; // number of components to performat lighting
    DWORD dwUserDataShift;      // number of bytes of user data. NOTE: must be 16 bytes aligned
public:
    LPVOID GetUsedData() const
    {
        return const_cast<BYTE *>(reinterpret_cast<const BYTE *>(this) + sizeof(*this));
    }
    LPVOID GetFigureData() const
    {
        return static_cast<BYTE *>(GetUsedData()) + dwUserDataShift;
    }
};
