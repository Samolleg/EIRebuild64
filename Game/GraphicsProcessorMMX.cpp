#include "StdAfx.h"
#include "GraphicsProcessorMMX.h"
#include <emmintrin.h>

namespace
{
// Four BGRA pixels, with the original per-word multiply/shift and byte saturation.
inline void BlendFour(LPDWORD pDst, const DWORD *pSrc, bool rle)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pSrc));
    const __m128i dst = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pDst));
    // The non-RLE assembly uses pixel 1's alpha for BOTH pixels 0 and 1.
    const __m128i alphaSrc = rle ? src : _mm_shuffle_epi32(src, _MM_SHUFFLE(3, 2, 1, 1));
    __m128i lo = _mm_unpacklo_epi8(alphaSrc, zero);
    __m128i hi = _mm_unpackhi_epi8(alphaSrc, zero);
    lo = _mm_shufflehi_epi16(_mm_shufflelo_epi16(lo, _MM_SHUFFLE(3, 3, 3, 3)),
                            _MM_SHUFFLE(3, 3, 3, 3));
    hi = _mm_shufflehi_epi16(_mm_shufflelo_epi16(hi, _MM_SHUFFLE(3, 3, 3, 3)),
                            _MM_SHUFFLE(3, 3, 3, 3));
    lo = _mm_sub_epi16(_mm_set1_epi16(255), lo);
    hi = _mm_sub_epi16(_mm_set1_epi16(255), hi);
    lo = _mm_add_epi16(_mm_srli_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(dst, zero), lo), 8),
                       _mm_unpacklo_epi8(src, zero));
    hi = _mm_add_epi16(_mm_srli_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(dst, zero), hi), 8),
                       _mm_unpackhi_epi8(src, zero));
    __m128i result = _mm_packus_epi16(lo, hi);
    // A literal zero at the start of an RLE group leaves that destination pixel alone.
    if (rle && pSrc[0] == 0)
    {
        const __m128i first = _mm_set_epi32(0, 0, 0, -1);
        result = _mm_or_si128(_mm_and_si128(first, dst), _mm_andnot_si128(first, result));
    }
    _mm_storeu_si128(reinterpret_cast<__m128i *>(pDst), result);
}

inline DWORD BlendPixel(DWORD dst, DWORD src, unsigned alpha)
{
    DWORD result = 0;
    for (unsigned shift = 0; shift != 32; shift += 8)
    {
        const unsigned value = (((dst >> shift) & 255) * (255 - alpha) >> 8) +
                               ((src >> shift) & 255);
        result |= Min(value, 255u) << shift;
    }
    return result;
}

inline __m128i BlurAlpha(__m128i sum)
{
    // Signed PMULHW is exact here: sums are nonnegative and at most 3 * 255.
    return _mm_and_si128(_mm_slli_epi16(_mm_mulhi_epi16(sum, _mm_set1_epi16(0x5556)), 8),
                         _mm_set1_epi16(static_cast<short>(0xf000)));
}
}

void CGraphicsProcessorMMX::PutOneLayer(LPDWORD pDst, LPDWORD pSrc, int nPix, bool bRLE)
{
    while (nPix > 0)
    {
        if (bRLE && (pSrc[0] >> 24) == 0 && pSrc[0] != 0)
        {
            // A run is a byte count, not a pointer or a native-sized word.
            const DWORD skip = *pSrc++ >> 2;
            pDst += skip;
            nPix -= static_cast<int>(skip);
            continue;
        }
        if (nPix >= 4)
            BlendFour(pDst, pSrc, bRLE);
        else
        {
            for (int i = 0; i < nPix; ++i)
            {
                if (bRLE && i == 0 && pSrc[0] == 0)
                    continue;
                const int alphaIndex = !bRLE && i == 0 && nPix > 1 ? 1 : i;
                pDst[i] = BlendPixel(pDst[i], pSrc[i], pSrc[alphaIndex] >> 24);
            }
        }
        const int count = Min(nPix, 4);
        pSrc += count;
        pDst += count;
        nPix -= count;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::GenerateMipmapLevel(LPDWORD pDst, LPDWORD pSrc, int iDstSize)
{
    if (iDstSize <= 0)
        return;
    const size_t srcWidth = size_t(iDstSize) * 2;
    const __m128i zero = _mm_setzero_si128();
    const __m128i alphaBias = _mm_set_epi16(128, 0, 0, 0, 128, 0, 0, 0);
    for (int y = 0; y < iDstSize; ++y)
    {
        const DWORD *top = pSrc + size_t(y) * 2 * srcWidth;
        const DWORD *bottom = top + srcWidth;
        int x = 0;
        for (; x + 1 < iDstSize; x += 2)
        {
            const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(top + x * 2));
            const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(bottom + x * 2));
            const __m128i lo = _mm_add_epi16(_mm_unpacklo_epi8(a, zero), _mm_unpacklo_epi8(b, zero));
            const __m128i hi = _mm_add_epi16(_mm_unpackhi_epi8(a, zero), _mm_unpackhi_epi8(b, zero));
            const __m128i sums = _mm_unpacklo_epi64(_mm_add_epi16(lo, _mm_srli_si128(lo, 8)),
                                                   _mm_add_epi16(hi, _mm_srli_si128(hi, 8)));
            const __m128i result = _mm_packus_epi16(_mm_srli_epi16(_mm_add_epi16(sums, alphaBias), 2), zero);
            _mm_storel_epi64(reinterpret_cast<__m128i *>(pDst + x), result);
        }
        if (x < iDstSize)
        {
            DWORD result = 0;
            for (unsigned shift = 0; shift != 32; shift += 8)
            {
                const unsigned sum = ((top[x * 2] >> shift) & 255) +
                                     ((top[x * 2 + 1] >> shift) & 255) +
                                     ((bottom[x * 2] >> shift) & 255) +
                                     ((bottom[x * 2 + 1] >> shift) & 255) +
                                     (shift == 24 ? 128 : 0);
                result |= Min(sum >> 2, 255u) << shift;
            }
            pDst[x] = result;
        }
        pDst += iDstSize;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::SetupTextureFormat(ETextureFormats format, DWORD dwAMask, DWORD dwRMask,
                                               DWORD dwGMask, DWORD dwBMask)
{
    DWORD dwAShift = 32, dwRShift = 24, dwGShift = 16, dwBShift = 8;
    switch (format)
    {
    case TEX_FORMAT_565:
        if (dwRMask == 0xF800 && dwGMask == 0x07E0 && dwBMask == 0x001F)
            bIs565Default = TRUE;
        else
        {
            bIs565Default = FALSE;
            dwRMask565 = dwRMask;
            dwGMask565 = dwGMask;
            dwBMask565 = dwBMask;
            while (dwRMask)
            {
                dwRMask >>= 1;
                dwRShift--;
            }
            while (dwGMask)
            {
                dwGMask >>= 1;
                dwGShift--;
            }
            while (dwBMask)
            {
                dwBMask >>= 1;
                dwBShift--;
            }
            dwRShift565 = dwRShift;
            dwGShift565 = dwGShift;
            dwBShift565 = dwBShift;
        }
        break;

    case TEX_FORMAT_555:
        if (dwRMask == 0x7C00 && dwGMask == 0x03E0 && dwBMask == 0x001F)
            bIs555Default = TRUE;
        else
        {
            bIs555Default = FALSE;
            dwRMask555 = dwRMask;
            dwGMask555 = dwGMask;
            dwBMask555 = dwBMask;
            while (dwRMask)
            {
                dwRMask >>= 1;
                dwRShift--;
            }
            while (dwGMask)
            {
                dwGMask >>= 1;
                dwGShift--;
            }
            while (dwBMask)
            {
                dwBMask >>= 1;
                dwBShift--;
            }
            dwRShift555 = dwRShift;
            dwGShift555 = dwGShift;
            dwBShift555 = dwBShift;
        }
        break;

    case TEX_FORMAT_1555:
        if (dwAMask == 0x8000 && dwRMask == 0x7C00 && dwGMask == 0x03E0 && dwBMask == 0x001F)
            bIs1555Default = TRUE;
        else
        {
            bIs1555Default = FALSE;
            dwAMask1555 = dwAMask;
            dwRMask1555 = dwRMask;
            dwGMask1555 = dwGMask;
            dwBMask1555 = dwBMask;
            while (dwAMask)
            {
                dwAMask >>= 1;
                dwAShift--;
            }
            while (dwRMask)
            {
                dwRMask >>= 1;
                dwRShift--;
            }
            while (dwGMask)
            {
                dwGMask >>= 1;
                dwGShift--;
            }
            while (dwBMask)
            {
                dwBMask >>= 1;
                dwBShift--;
            }
            dwAShift1555 = dwAShift;
            dwRShift1555 = dwRShift;
            dwGShift1555 = dwGShift;
            dwBShift1555 = dwBShift;
        }
        break;

    default:
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::Convert32To16Bit(LPWORD pDst, LPDWORD pSrc, ETextureFormats format,
                                             int nPix)
{
    DWORD r, g, b, a;
    DWORD dwColor;
    int i;

    i = nPix;

    if (format == TEX_FORMAT_565)
    {
        if (!bIs565Default)
            while (i)
            {
                dwColor = *pSrc++;
                r = (dwColor >> dwRShift565) & dwRMask565;
                g = (dwColor >> dwGShift565) & dwGMask565;
                b = (dwColor >> dwBShift565) & dwBMask565;
                *pDst++ = WORD(r | g | b);
                i--;
            }
        else
            while (i)
            {
                dwColor = *pSrc++;
                r = (dwColor >> 8) & 0xF800;
                g = (dwColor >> 5) & 0x07E0;
                b = (dwColor >> 3) & 0x001F;
                *pDst++ = WORD(r | g | b);
                i--;
            }
        return;
    }

    if (format == TEX_FORMAT_555)
    {
        if (!bIs555Default)
            while (i)
            {
                dwColor = *pSrc++;
                r = (dwColor >> dwRShift555) & dwRMask555;
                g = (dwColor >> dwGShift555) & dwGMask555;
                b = (dwColor >> dwBShift555) & dwBMask555;
                *pDst++ = WORD(r | g | b);
                i--;
            }
        else
            while (i)
            {
                dwColor = *pSrc++;
                r = (dwColor >> 9) & 0x7C00;
                g = (dwColor >> 6) & 0x03E0;
                b = (dwColor >> 3) & 0x001F;
                *pDst++ = WORD(r | g | b);
                i--;
            }
        return;
    }

    if (format == TEX_FORMAT_1555)
    {
        if (!bIs1555Default)
            while (i)
            {
                dwColor = *pSrc++;
                a = (dwColor >> dwAShift1555) & dwAMask1555;
                r = (dwColor >> dwRShift1555) & dwRMask1555;
                g = (dwColor >> dwGShift1555) & dwGMask1555;
                b = (dwColor >> dwBShift1555) & dwBMask1555;
                *pDst++ = WORD(a | r | g | b);
                i--;
            }
        else
            while (i)
            {
                dwColor = *pSrc++;
                a = (dwColor >> 16) & 0x8000;
                r = (dwColor >> 9) & 0x7C00;
                g = (dwColor >> 6) & 0x03E0;
                b = (dwColor >> 3) & 0x001F;
                *pDst++ = WORD(a | r | g | b);
                i--;
            }
        return;
    }

    if (format == TEX_FORMAT_4444)
    {
        while (i)
        {
            dwColor = *pSrc++;
            a = (dwColor >> 16) & 0xF000;
            r = (dwColor >> 12) & 0x0F00;
            g = (dwColor >> 8) & 0x00F0;
            b = (dwColor >> 4) & 0x000F;
            *pDst++ = WORD(a | r | g | b);
            i--;
        }
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::RasterizeTriangle(SShadowTextureRenderParam &strp)
{
    int mx1, my1, mx2, my2, mx3, my3;
    int xlen1, ylen1, xlen2, ylen2, xinc1, xinc2, xerr1, xerr2;
    int x1, x2, y, ymax, i;
    int nDstWidth = strp.dwDstPitch >> 1;
    int nSrcWidth = strp.dwSrcPitch >> 1;

    if (strp.y1 < strp.y2)
        if (strp.y1 < strp.y3)
        { // 123
            mx1 = strp.x1;
            my1 = strp.y1;
            mx2 = strp.x2;
            my2 = strp.y2;
            mx3 = strp.x3;
            my3 = strp.y3;
        }
        else
        { // 312
            mx1 = strp.x3;
            my1 = strp.y3;
            mx2 = strp.x1;
            my2 = strp.y1;
            mx3 = strp.x2;
            my3 = strp.y2;
        }
    else if (strp.y2 < strp.y3)
    { // 231
        mx1 = strp.x2;
        my1 = strp.y2;
        mx2 = strp.x3;
        my2 = strp.y3;
        mx3 = strp.x1;
        my3 = strp.y1;
    }
    else
    { // 312
        mx1 = strp.x3;
        my1 = strp.y3;
        mx2 = strp.x1;
        my2 = strp.y1;
        mx3 = strp.x2;
        my3 = strp.y2;
    }
    // mx1, my1 - coord of vert. with min Y
    // next order is shifted order of triangle
    xlen1 = mx3 - mx1;
    ylen1 = my3 - my1;
    xlen2 = mx2 - mx1;
    ylen2 = my2 - my1;

    // wrong orientation
    if (xlen1 * ylen2 >= xlen2 * ylen1)
        return;

    // increments
    if (xlen1 < 0)
    {
        xinc1 = -1;
        xlen1 = -xlen1;
    }
    else
        xinc1 = 1;
    if (xlen2 < 0)
    {
        xinc2 = -1;
        xlen2 = -xlen2;
    }
    else
        xinc2 = 1;

    y = my1;
    ymax = max(my2, my3);
    x1 = x2 = mx1;
    xerr1 = 0;
    xerr2 = 0;

    // to avoid endless looping
    if (ylen1 == 0)
    {
        ylen1++;
        x1 = mx3;
    }
    if (ylen2 == 0)
    {
        ylen2++;
        x2 = mx2;
    }
    LPWORD ptr = strp.lpDstTexture + y * nDstWidth;
    LPWORD rptr;

    // main loop
    while (y <= ymax)
    {
        while (xerr1 >= ylen1)
        {
            x1 += xinc1;
            xerr1 -= ylen1;
        }
        while (xerr2 >= ylen2)
        {
            x2 += xinc2;
            xerr2 -= ylen2;
        }
        xerr1 += xlen1;
        xerr2 += xlen2;

        // fill
        i = x2 - x1 + 1;
        rptr = ptr + x1;
        while (i > 0)
        {
            *(rptr++) = 0xf000;
            i--;
        }

        // check for reaching one of vertices by Y
        // if both reached - doesn't matter cause main loop ends here
        if (y == my3)
        {
            xlen1 = mx2 - mx3;
            ylen1 = my2 - my3;
            if (xlen1 < 0)
            {
                xinc1 = -1;
                xlen1 = -xlen1;
            }
            else
                xinc1 = 1;
            x1 = mx3;
            xerr1 = 0;
        }
        if (y == my2)
        {
            xlen2 = mx3 - mx2;
            ylen2 = my3 - my2;
            if (xlen2 < 0)
            {
                xinc2 = -1;
                xlen2 = -xlen2;
            }
            else
                xinc2 = 1;
            x2 = mx2;
            xerr2 = 0;
        }

        // next line
        y++;
        ptr += nDstWidth;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::RasterizeTriangleWithAlpha(SShadowTextureRenderParam &strp)
{
    int mx1, my1, mx2, my2, mx3, my3;
    int tx1, ty1, tx2, ty2, tx3, ty3;
    int dx1, dy1, dx2, dy2, dx3, dy3;
    int xlen1, ylen1, xlen2, ylen2, xinc1, xinc2, xerr1, xerr2;
    int uxadd, vxadd, uerr, verr, u, v, uvcheck;
    int x1, x2, y, ymax, i, m, n, mm, nn, mmxinc, nnxinc, mmyinc, nnyinc;
    int nDstWidth = strp.dwDstPitch >> 1;
    int nSrcWidth = strp.dwSrcPitch >> 1;
    LPWORD srcbits;

    if (strp.y1 < strp.y2)
        if (strp.y1 < strp.y3)
        { // 123
            mx1 = strp.x1;
            my1 = strp.y1;
            mx2 = strp.x2;
            my2 = strp.y2;
            mx3 = strp.x3;
            my3 = strp.y3;
            tx1 = strp.tu1;
            ty1 = strp.tv1;
            tx2 = strp.tu2 - tx1;
            ty2 = strp.tv2 - ty1;
            tx3 = strp.tu3 - tx1;
            ty3 = strp.tv3 - ty1;
        }
        else
        { // 312
            mx1 = strp.x3;
            my1 = strp.y3;
            mx2 = strp.x1;
            my2 = strp.y1;
            mx3 = strp.x2;
            my3 = strp.y2;
            tx1 = strp.tu3;
            ty1 = strp.tv3;
            tx2 = strp.tu1 - tx1;
            ty2 = strp.tv1 - ty1;
            tx3 = strp.tu2 - tx1;
            ty3 = strp.tv2 - ty1;
        }
    else if (strp.y2 < strp.y3)
    { // 231
        mx1 = strp.x2;
        my1 = strp.y2;
        mx2 = strp.x3;
        my2 = strp.y3;
        mx3 = strp.x1;
        my3 = strp.y1;
        tx1 = strp.tu2;
        ty1 = strp.tv2;
        tx2 = strp.tu3 - tx1;
        ty2 = strp.tv3 - ty1;
        tx3 = strp.tu1 - tx1;
        ty3 = strp.tv1 - ty1;
    }
    else
    { // 312
        mx1 = strp.x3;
        my1 = strp.y3;
        mx2 = strp.x1;
        my2 = strp.y1;
        mx3 = strp.x2;
        my3 = strp.y2;
        tx1 = strp.tu3;
        ty1 = strp.tv3;
        tx2 = strp.tu1 - tx1;
        ty2 = strp.tv1 - ty1;
        tx3 = strp.tu2 - tx1;
        ty3 = strp.tv2 - ty1;
    }
    // mx1, my1 - coord of vert. with min Y
    // next order is shifted order of triangle
    dx1 = mx1;
    dy1 = my1;
    dx3 = xlen1 = mx3 - mx1;
    dy3 = ylen1 = my3 - my1;
    dx2 = xlen2 = mx2 - mx1;
    dy2 = ylen2 = my2 - my1;

    // for UV mapping
    uvcheck = xlen2 * ylen1 - xlen1 * ylen2;

    // wrong orientation
    if (uvcheck <= 0)
        return;

    uxadd = ylen1 * tx2 - ylen2 * tx3;
    vxadd = ylen1 * ty2 - ylen2 * ty3;

    // increments
    if (xlen1 < 0)
    {
        xinc1 = -1;
        xlen1 = -xlen1;
    }
    else
        xinc1 = 1;
    if (xlen2 < 0)
    {
        xinc2 = -1;
        xlen2 = -xlen2;
    }
    else
        xinc2 = 1;

    y = my1;
    ymax = max(my2, my3);
    x1 = mx1 + 1;
    x2 = mx1 - 1;
    xerr1 = 0;
    xerr2 = 0;

    // to avoid endless looping
    if (y == my3)
    {
        xlen1 = mx2 - mx3;
        ylen1 = my2 - my3;
        if (xlen1 < 0)
        {
            xinc1 = -1;
            xlen1 = -xlen1;
        }
        else
            xinc1 = 1;
        x1 = mx3 + 1;
    }
    if (y == my2)
    {
        xlen2 = mx3 - mx2;
        ylen2 = my3 - my2;
        if (xlen2 < 0)
        {
            xinc2 = -1;
            xlen2 = -xlen2;
        }
        else
            xinc2 = 1;
        x2 = mx2 - 1;
    }

    LPWORD ptr = strp.lpDstTexture + y * nDstWidth;
    LPWORD rptr, sptr;
    srcbits = strp.lpSrcTexture + tx1 * nSrcWidth + ty1;

    m = (x1 - dx1) * dy3 - (y - dy1) * dx3;
    n = -(x1 - dx1) * dy2 + (y - dy1) * dx2;

    mm = m * tx2 + n * tx3;
    nn = m * ty2 + n * ty3;

    mmxinc = xinc1 * (dy3 * tx2 - dy2 * tx3);
    nnxinc = xinc1 * (dy3 * ty2 - dy2 * ty3);
    mmyinc = -dx3 * tx2 + dx2 * tx3;
    nnyinc = -dx3 * ty2 + dx2 * ty3;

    // main loop
    while (y <= ymax)
    {
        while (xerr1 >= ylen1)
        {
            x1 += xinc1;
            mm += mmxinc;
            nn += nnxinc;
            xerr1 -= ylen1;
        }
        while (xerr2 >= ylen2)
        {
            x2 += xinc2;
            xerr2 -= ylen2;
        }
        xerr1 += xlen1;
        xerr2 += xlen2;

        // fill
        i = x2 - x1 + 1;
        rptr = ptr + x1;

        u = mm / uvcheck;
        v = nn / uvcheck;
        uerr = mm - u * uvcheck;
        verr = nn - v * uvcheck;
        //		u += tx1;
        //		v += ty1;
        sptr = srcbits + u * nSrcWidth + v;
        while (i > 0)
        {
            while (uerr >= uvcheck)
            {
                // u++;
                sptr += nSrcWidth;
                uerr -= uvcheck;
            }
            while (verr >= uvcheck)
            {
                // v++;
                sptr++;
                verr -= uvcheck;
            }
            while (uerr < 0)
            {
                // u--;
                sptr -= nSrcWidth;
                uerr += uvcheck;
            }
            while (verr < 0)
            {
                // v--;
                sptr--;
                verr += uvcheck;
            }

            uerr += uxadd;
            verr += vxadd;

            if ((*sptr) & 0x8000)
                *rptr = 0xf000;

            i--;
            rptr++;
        }

        // check for reaching one of vertices by Y
        // if both reached - doesn't matter cause main loop ends here
        if (y == my3)
        {
            xlen1 = mx2 - mx3;
            ylen1 = my2 - my3;
            if (xlen1 < 0)
            {
                xinc1 = -1;
                xlen1 = -xlen1;
            }
            else
                xinc1 = 1;
            x1 = mx3 + 1;
            xerr1 = xlen1;

            m = (x1 - dx1) * dy3 - (y - dy1) * dx3;
            n = -(x1 - dx1) * dy2 + (y - dy1) * dx2;
            mm = (m * tx2 + n * tx3);
            nn = (m * ty2 + n * ty3);
            mmxinc = xinc1 * (dy3 * tx2 - dy2 * tx3);
            nnxinc = xinc1 * (dy3 * ty2 - dy2 * ty3);
        }
        if (y == my2)
        {
            xlen2 = mx3 - mx2;
            ylen2 = my3 - my2;
            if (xlen2 < 0)
            {
                xinc2 = -1;
                xlen2 = -xlen2;
            }
            else
                xinc2 = 1;
            x2 = mx2 - 1;
            xerr2 = xlen2;
        }

        // next line
        y++;

        mm += mmyinc;
        nn += nnyinc;
        ptr += nDstWidth;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
namespace
{
inline WORD Interpolate565(WORD a, WORD b)
{
    // Preserve PMULHW with 0x5555 (not division by 3), including green's masked bits.
    const unsigned blue = (((a & 31) * 2 + (b & 31)) * 0x5555) >> 16;
    const unsigned green = ((((a & 0x07e0) * 2 + (b & 0x07e0)) * 0x5555) >> 16) & 0x07e0;
    const unsigned red = (((a >> 11) * 2 + (b >> 11)) * 0x5555) >> 16;
    return static_cast<WORD>(blue | green | (red << 11));
}

inline WORD Average565(WORD a, WORD b)
{
    const unsigned blue = ((a & 31) + (b & 31)) >> 1;
    const unsigned green = (((a & 0x07e0) + (b & 0x07e0)) >> 1) & 0x07e0;
    const unsigned red = ((a >> 11) + (b >> 11)) >> 1;
    return static_cast<WORD>(blue | green | (red << 11));
}
}

void CGraphicsProcessorMMX::DecompressDXT(LPWORD pDst, LPBYTE pSrc, EDXTDecodeTypes decodeType,
                                          int nSizeX, int nSizeY)
{
    if (decodeType < DXT1_TO_565 || decodeType > DXT3_TO_4444)
        return;
    const bool explicitAlpha = decodeType == DXT2_TO_4444 || decodeType == DXT3_TO_4444;
    for (int by = 0; by < (nSizeY >> 2); ++by)
    {
        for (int bx = 0; bx < (nSizeX >> 2); ++bx)
        {
            const BYTE *block = pSrc + (explicitAlpha ? 8 : 0);
            WORD colors[4];
            DWORD indices;
            memcpy(colors, block, 2 * sizeof(WORD));
            memcpy(&indices, block + 4, sizeof(indices));
            const bool fourColors = explicitAlpha || colors[0] > colors[1];
            colors[2] = fourColors ? Interpolate565(colors[0], colors[1]) :
                                    Average565(colors[0], colors[1]);
            colors[3] = fourColors ? Interpolate565(colors[1], colors[0]) : 0;
            for (int c = 0; c < 4; ++c)
            {
                if (decodeType == DXT1_TO_555 || decodeType == DXT1_TO_1555)
                {
                    colors[c] = static_cast<WORD>(((colors[c] & 0xffc0) >> 1) | (colors[c] & 31));
                    if (decodeType == DXT1_TO_1555 && (fourColors || c != 3))
                        colors[c] |= 0x8000;
                }
                else if (explicitAlpha)
                    colors[c] = static_cast<WORD>(((colors[c] & 0xf000) >> 4) |
                                                  ((colors[c] & 0x0780) >> 3) |
                                                  ((colors[c] & 0x001e) >> 1));
            }
            WORD *dst = pDst + size_t(by) * 4 * nSizeX + bx * 4;
            for (int y = 0; y < 4; ++y)
            {
                WORD alpha = 0;
                if (explicitAlpha)
                    memcpy(&alpha, pSrc + y * sizeof(WORD), sizeof(alpha));
                for (int x = 0; x < 4; ++x)
                {
                    dst[x] = colors[indices & 3] | ((alpha & 15) << 12);
                    indices >>= 2;
                    alpha >>= 4;
                }
                dst += nSizeX;
            }
            pSrc += explicitAlpha ? 16 : 8;
        }
    }
}
//
void CGraphicsProcessorMMX::BlurVertical(LPWORD pSrc, int nSizeX, int nSizeY)
{
    if (nSizeX <= 0 || nSizeY <= 0)
        return;
    int x = 0;
    for (; x + 7 < nSizeX; x += 8)
    {
        WORD *dst = pSrc + x;
        __m128i prev = _mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(dst)), 8);
        if (nSizeY == 1)
        {
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), BlurAlpha(prev));
            continue;
        }
        __m128i current = _mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + nSizeX)), 8);
        __m128i result = BlurAlpha(_mm_add_epi16(prev, current));
        for (int y = 2; y < nSizeY; ++y)
        {
            const __m128i next = _mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + size_t(y) * nSizeX)), 8);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), result);
            dst += nSizeX;
            result = BlurAlpha(_mm_add_epi16(_mm_add_epi16(prev, current), next));
            prev = current;
            current = next;
        }
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), result);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + nSizeX), BlurAlpha(_mm_add_epi16(prev, current)));
    }
    // Complete columns outside the SIMD group without reading beyond the row.
    for (; x < nSizeX; ++x)
    {
        WORD *dst = pSrc + x;
        unsigned prev = *dst >> 8;
        if (nSizeY == 1)
        {
            *dst = static_cast<WORD>(((prev * 0x5556) >> 16) << 8) & 0xf000;
            continue;
        }
        unsigned current = dst[nSizeX] >> 8;
        WORD result = static_cast<WORD>((((prev + current) * 0x5556) >> 16) << 8) & 0xf000;
        for (int y = 2; y < nSizeY; ++y)
        {
            const unsigned next = pSrc[size_t(y) * nSizeX + x] >> 8;
            *dst = result;
            dst += nSizeX;
            result = static_cast<WORD>((((prev + current + next) * 0x5556) >> 16) << 8) & 0xf000;
            prev = current;
            current = next;
        }
        *dst = result;
        dst[nSizeX] = static_cast<WORD>((((prev + current) * 0x5556) >> 16) << 8) & 0xf000;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::BlurHorizontal(LPWORD pSrc, int nSizeX, int nSizeY)
{
    DWORD dwSum;
    WORD wPrev;
    int i, j;

    j = nSizeY;
    while (j)
    {
        dwSum = DWORD(*(pSrc + 0)) + DWORD(*(pSrc + 1));
        wPrev = WORD((dwSum * 0x5556) >> 16) & 0xf000;
        pSrc++;
        i = nSizeX - 2;
        while (i)
        {
            dwSum = DWORD(*(pSrc - 1)) + DWORD(*(pSrc + 0)) + DWORD(*(pSrc + 1));
            *(pSrc - 1) = wPrev;
            wPrev = WORD((dwSum * 0x5556) >> 16) & 0xf000;
            pSrc++;
            i--;
        }
        dwSum = DWORD(*(pSrc - 1)) + DWORD(*(pSrc + 0));
        *(pSrc - 1) = wPrev;
        *(pSrc + 0) = WORD((dwSum * 0x5556) >> 16) & 0xf000;
        pSrc++;
        j--;
    }
    /*
            DWORD dwSum;
            WORD wPrev;
            int i, j;

            j = nSizeY;
            while (j)
            {
                    dwSum = DWORD(*(pSrc+0)) + DWORD(*(pSrc+1)) + DWORD(*(pSrc+2));
                    wPrev = WORD( dwSum >> 2 ) & 0xf000;
                    pSrc++;
                    i = nSizeX - 3;
                    while (i)
                    {
                            dwSum = DWORD(*(pSrc-1)) + DWORD(*(pSrc+0)) + DWORD(*(pSrc+1)) +
       DWORD(*(pSrc+2));
                            *(pSrc-1) = wPrev;
                            wPrev = WORD( dwSum >> 2 ) & 0xf000;
                            pSrc++;
                            i--;
                    }
                    dwSum = DWORD(*(pSrc-1)) + DWORD(*(pSrc+0)) + DWORD(*(pSrc+1));
                    *(pSrc-1) = wPrev;
                    wPrev = WORD( dwSum >> 2 ) & 0xf000;
                    pSrc++;
                    dwSum = DWORD(*(pSrc-1)) + DWORD(*(pSrc+0));
                    *(pSrc-1) = wPrev;
                    *(pSrc+0) = WORD( dwSum >> 2 ) & 0xf000;
                    pSrc++;
                    j--;
            }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::PrecalculateAsLayer(LPDWORD pSrc, int nPix)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i alphaMask = _mm_set1_epi32(static_cast<int>(0xff000000u));
    int i = 0;
    for (; i + 3 < nPix; i += 4)
    {
        const __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pSrc + i));
        __m128i lo = _mm_unpacklo_epi8(src, zero);
        __m128i hi = _mm_unpackhi_epi8(src, zero);
        const __m128i alphaLo = _mm_shufflehi_epi16(_mm_shufflelo_epi16(lo, _MM_SHUFFLE(3, 3, 3, 3)),
                                                   _MM_SHUFFLE(3, 3, 3, 3));
        const __m128i alphaHi = _mm_shufflehi_epi16(_mm_shufflelo_epi16(hi, _MM_SHUFFLE(3, 3, 3, 3)),
                                                   _MM_SHUFFLE(3, 3, 3, 3));
        lo = _mm_srli_epi16(_mm_mullo_epi16(lo, alphaLo), 8);
        hi = _mm_srli_epi16(_mm_mullo_epi16(hi, alphaHi), 8);
        const __m128i rgb = _mm_packus_epi16(lo, hi);
        const __m128i result = _mm_or_si128(_mm_andnot_si128(alphaMask, rgb), _mm_and_si128(alphaMask, src));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(pSrc + i), result);
    }
    for (; i < nPix; ++i)
    {
        const DWORD src = pSrc[i];
        const unsigned alpha = src >> 24;
        pSrc[i] = (src & 0xff000000) |
                  ((((src >> 16) & 255) * alpha >> 8) << 16) |
                  ((((src >> 8) & 255) * alpha >> 8) << 8) |
                  ((src & 255) * alpha >> 8);
    }
}
/////////////////////////////////////////////////////////////////////////////////////