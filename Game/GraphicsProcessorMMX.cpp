#include "StdAfx.h"
#include "GraphicsProcessorMMX.h"
#include <mmintrin.h>

void CGraphicsProcessorMMX::PutOneLayer(LPDWORD pDst, LPDWORD pSrc, int nPix, bool bRLE)
{
    if (bRLE)
        __asm {
		pxor mm0, mm0
		mov edi, nPix
		shr edi, 2
		mov esi, pDst
		mov ecx, pSrc
a0:
		xor eax, eax
		mov al, [ecx+3]
		cmp eax, 0
		jz a2

		movd mm3, [esi]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi], mm3
a1:
		movd mm3, [esi+4]
		xor eax, eax
		mov al, [ecx+7]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+4]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+4], mm3

		movd mm3, [esi+8]
		xor eax, eax
		mov al, [ecx+11]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+8]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+8], mm3

		movd mm3, [esi+12]
		xor eax, eax
		mov al, [ecx+15]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+12]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+12], mm3

		add esi, 16
		add ecx, 16
		dec edi
		jnz a0
		jmp a3
a2:
		mov ebx, [ecx]
		cmp ebx, 0
		jz a1

		add esi, ebx
		add ecx, 4
		shr ebx, 4
		sub edi, ebx
		jnz a0
a3:
		emms
        }
        else
        {
            __asm {
		pxor mm0, mm0
		mov edi, nPix
		shr edi, 2
		mov esi, pDst
		mov ecx, pSrc
a4:
		movd mm3, [esi]
		xor eax, eax
		mov al, [ecx+7]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi], mm3

		movd mm3, [esi+4]
		xor eax, eax
		mov al, [ecx+7]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+4]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+4], mm3

		movd mm3, [esi+8]
		xor eax, eax
		mov al, [ecx+11]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+8]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+8], mm3

		movd mm3, [esi+12]
		xor eax, eax
		mov al, [ecx+15]
		xor eax, 0x000000FF
		movd mm5, eax
		movd mm2, [ecx+12]
		punpcklbw mm5, mm5
		punpcklbw mm2, mm0
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		paddw mm3, mm2
		packuswb mm3, mm0
		movd [esi+12], mm3

		add esi, 16
		add ecx, 16
		dec edi
		jne a4
		emms
            }
        }
}
////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::GenerateMipmapLevel(LPDWORD pDst, LPDWORD pSrc, int iDstSize)
{
    __asm {
		pxor mm0, mm0
		mov edx, iDstSize
		mov ecx, pSrc
		mov ebx, pDst
		mov eax, 0x80000000
		movd mm7, eax
		punpcklbw mm7, mm0
		mov esi, edx
		mov edi, edx
a0:
		mov eax, [ecx]
		movd mm2, eax
		mov eax, [ecx+4]
		punpcklbw mm2, mm0
		movd mm3, eax
		mov eax, [ecx+edx*8]
		punpcklbw mm3, mm0
		movd mm4, eax
		paddw mm2, mm3
		mov eax, [ecx+edx*8+4]
		punpcklbw mm4, mm0
		movd mm5, eax
		paddw mm2, mm4
		add ecx, 8
		paddw mm2, mm7
		punpcklbw mm5, mm0
		paddw mm2, mm5
		psrlw mm2, 2
		packuswb mm2, mm0
		movd [ebx], mm2
		add ebx, 4
		dec edi
		jne a0
		lea ecx, [ecx+edx*8]
		mov edi, edx
		dec esi
		jne a0
		emms
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
void CGraphicsProcessorMMX::DecompressDXT(LPWORD pDst, LPBYTE pSrc, EDXTDecodeTypes decodeType,
                                          int nSizeX, int nSizeY)
{
    // DXTn decoding
    LPWORD colors, color2, color3, alpha;

    DWORD pix;
    WORD alpha4;
    LPWORD ptrdst;
    int i, j, n;
    __m64 _mmx_const1, _mmx_const2, _mmx_const3, _mmx_const4, _mmx_alpha;
    __m64 _mmx_color0, _mmx_color1, _mmx_color2, _mmx_color3, _mmx_colors;
    color2 = (LPWORD)&_mmx_color2;
    color3 = (LPWORD)&_mmx_color3;
    colors = (LPWORD)&_mmx_colors;

    _mmx_const3 = _mm_set1_pi16(0x5555);

    switch (decodeType)
    {
        //////////////////////////////////////////////// DXT1_TO_565
    case DXT1_TO_565:

        j = nSizeY >> 2;
        while (j)
        {
            i = nSizeX >> 2;
            while (i)
            {
                colors[0] = *((LPWORD)pSrc);
                colors[1] = *((LPWORD)(pSrc + 2));
                pix = *((LPDWORD)(pSrc + 4));
                _mmx_color0 = _mm_set_pi16(0, colors[0] >> 11, colors[0] & 0x07e0,
                                           colors[0] & 0x001f);
                _mmx_color1 = _mm_set_pi16(0, colors[1] >> 11, colors[1] & 0x07e0,
                                           colors[1] & 0x001f);
                if (colors[0] > colors[1])
                {
                    _mmx_color2 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color0, 1), _mmx_color1),
                                            _mmx_const3);
                    _mmx_color3 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color1, 1), _mmx_color0),
                                            _mmx_const3);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = color3[0] | (color3[1] & 0x07e0) | (color3[2] << 11);
                }
                else
                {
                    _mmx_color2 = _m_psrlwi(_m_paddw(_mmx_color0, _mmx_color1), 1);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = 0;
                }
                ptrdst = pDst;
                n = 4;
                while (n)
                {
                    ptrdst[0] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[1] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[2] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[3] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst += nSizeX;
                    n--;
                }
                pSrc += 8;
                pDst += 4;
                i--;
            }
            pDst += nSizeX * 3;
            j--;
        }

        break;

        //////////////////////////////////////////////// DXT1_TO_555
    case DXT1_TO_555:

        _mmx_const1 = _mm_set1_pi16(0xffc0);
        _mmx_const2 = _mm_set1_pi16(0x001f);
        j = nSizeY >> 2;
        while (j)
        {
            i = nSizeX >> 2;
            while (i)
            {
                colors[0] = *((LPWORD)pSrc);
                colors[1] = *((LPWORD)(pSrc + 2));
                pix = *((LPDWORD)(pSrc + 4));
                _mmx_color0 = _mm_set_pi16(0, colors[0] >> 11, colors[0] & 0x07e0,
                                           colors[0] & 0x001f);
                _mmx_color1 = _mm_set_pi16(0, colors[1] >> 11, colors[1] & 0x07e0,
                                           colors[1] & 0x001f);
                if (colors[0] > colors[1])
                {
                    _mmx_color2 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color0, 1), _mmx_color1),
                                            _mmx_const3);
                    _mmx_color3 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color1, 1), _mmx_color0),
                                            _mmx_const3);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = color3[0] | (color3[1] & 0x07e0) | (color3[2] << 11);
                }
                else
                {
                    _mmx_color2 = _m_psrlwi(_m_paddw(_mmx_color0, _mmx_color1), 1);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = 0;
                }
                // convert from 565 to 555
                _mmx_colors = _m_por(_m_psrlwi(_m_pand(_mmx_colors, _mmx_const1), 1),
                                     _m_pand(_mmx_colors, _mmx_const2));
                ptrdst = pDst;
                n = 4;
                while (n)
                {
                    ptrdst[0] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[1] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[2] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[3] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst += nSizeX;
                    n--;
                }
                pSrc += 8;
                pDst += 4;
                i--;
            }
            pDst += nSizeX * 3;
            j--;
        }

        break;

        //////////////////////////////////////////////// DXT1_TO_1555
    case DXT1_TO_1555:

        _mmx_const1 = _mm_set1_pi16(0xffc0);
        _mmx_const2 = _mm_set1_pi16(0x001f);
        _mmx_alpha = _mm_set1_pi16(0x8000);
        alpha = (LPWORD)&_mmx_alpha;
        j = nSizeY >> 2;
        while (j)
        {
            i = nSizeX >> 2;
            while (i)
            {
                colors[0] = *((LPWORD)pSrc);
                colors[1] = *((LPWORD)(pSrc + 2));
                pix = *((LPDWORD)(pSrc + 4));
                _mmx_color0 = _mm_set_pi16(0, colors[0] >> 11, colors[0] & 0x07e0,
                                           colors[0] & 0x001f);
                _mmx_color1 = _mm_set_pi16(0, colors[1] >> 11, colors[1] & 0x07e0,
                                           colors[1] & 0x001f);
                if (colors[0] > colors[1])
                {
                    _mmx_color2 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color0, 1), _mmx_color1),
                                            _mmx_const3);
                    _mmx_color3 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color1, 1), _mmx_color0),
                                            _mmx_const3);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = color3[0] | (color3[1] & 0x07e0) | (color3[2] << 11);
                    alpha[3] = 0x8000;
                }
                else
                {
                    _mmx_color2 = _m_psrlwi(_m_paddw(_mmx_color0, _mmx_color1), 1);
                    colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                    colors[3] = 0;
                    alpha[3] = 0;
                }
                // convert from 565 to 1555
                _mmx_colors = _m_por(_mmx_alpha,
                                     _m_por(_m_psrlwi(_m_pand(_mmx_colors, _mmx_const1), 1),
                                            _m_pand(_mmx_colors, _mmx_const2)));
                ptrdst = pDst;
                n = 4;
                while (n)
                {
                    ptrdst[0] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[1] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[2] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst[3] = colors[pix & 3];
                    pix >>= 2;
                    ptrdst += nSizeX;
                    n--;
                }
                pSrc += 8;
                pDst += 4;
                i--;
            }
            pDst += nSizeX * 3;
            j--;
        }

        break;

        //////////////////////////////////////////////// DXT2_TO_4444, DXT3_TO_4444
    case DXT2_TO_4444:
    case DXT3_TO_4444:

        _mmx_const1 = _mm_set1_pi16(0xf000);
        _mmx_const2 = _mm_set1_pi16(0x0780);
        _mmx_const4 = _mm_set1_pi16(0x001e);
        pSrc += 8;
        j = nSizeY >> 2;
        while (j)
        {
            i = nSizeX >> 2;
            while (i)
            {
                colors[0] = *((LPWORD)pSrc);
                colors[1] = *((LPWORD)(pSrc + 2));
                pix = *((LPDWORD)(pSrc + 4));
                _mmx_color0 = _mm_set_pi16(0, colors[0] >> 11, colors[0] & 0x07e0,
                                           colors[0] & 0x001f);
                _mmx_color1 = _mm_set_pi16(0, colors[1] >> 11, colors[1] & 0x07e0,
                                           colors[1] & 0x001f);
                _mmx_color2 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color0, 1), _mmx_color1),
                                        _mmx_const3);
                _mmx_color3 = _m_pmulhw(_m_paddw(_m_psllwi(_mmx_color1, 1), _mmx_color0),
                                        _mmx_const3);
                colors[2] = color2[0] | (color2[1] & 0x07e0) | (color2[2] << 11);
                colors[3] = color3[0] | (color3[1] & 0x07e0) | (color3[2] << 11);
                // convert from 565 to 444
                _mmx_colors = _m_por(_m_psrlwi(_m_pand(_mmx_colors, _mmx_const1), 4),
                                     _m_por(_m_psrlwi(_m_pand(_mmx_colors, _mmx_const2), 3),
                                            _m_psrlwi(_m_pand(_mmx_colors, _mmx_const4), 1)));
                ptrdst = pDst;
                n = 4;
                while (n)
                {
                    alpha4 = *((LPWORD)pSrc - n);
                    ptrdst[0] = colors[pix & 3] | ((alpha4 & 15) << 12);
                    pix >>= 2;
                    alpha4 >>= 4;
                    ptrdst[1] = colors[pix & 3] | ((alpha4 & 15) << 12);
                    pix >>= 2;
                    alpha4 >>= 4;
                    ptrdst[2] = colors[pix & 3] | ((alpha4 & 15) << 12);
                    pix >>= 2;
                    alpha4 >>= 4;
                    ptrdst[3] = colors[pix & 3] | ((alpha4 & 15) << 12);
                    pix >>= 2;
                    ptrdst += nSizeX;
                    n--;
                }
                pSrc += 16;
                pDst += 4;
                i--;
            }
            pDst += nSizeX * 3;
            j--;
        }

        break;
    }
    _m_empty();
}
/////////////////////////////////////////////////////////////////////////////////////
void CGraphicsProcessorMMX::BlurVertical(LPWORD pSrc, int nSizeX, int nSizeY)
{
    int i, j, step;
    __m64 _mmx_prev, _mmx_sum, _mmx_const, _mmx_const2;
    __m64 *_mmx_ptr, *_mmx_ptr_1, *_mmx_ptr1;

    _mmx_const = _mm_set1_pi16(0xf000);
    _mmx_const2 = _mm_set1_pi16(0x5556);
    i = step = nSizeX >> 2;
    while (i)
    {
        _mmx_ptr_1 = (__m64 *)pSrc;
        _mmx_ptr = _mmx_ptr_1 + step;
        _mmx_ptr1 = _mmx_ptr + step;

        *_mmx_ptr_1 = _m_psrlwi(*_mmx_ptr_1, 8);
        *_mmx_ptr = _m_psrlwi(*_mmx_ptr, 8);

        _mmx_sum = _m_paddw(*_mmx_ptr_1, *_mmx_ptr);
        _mmx_prev = _m_pand(_m_psllwi(_m_pmulhw(_mmx_sum, _mmx_const2), 8), _mmx_const);

        j = nSizeY - 2;
        while (j)
        {
            *_mmx_ptr1 = _m_psrlwi(*_mmx_ptr1, 8);

            _mmx_sum = _m_paddw(_m_paddw(*_mmx_ptr_1, *_mmx_ptr), *_mmx_ptr1);
            *_mmx_ptr_1 = _mmx_prev;
            _mmx_prev = _m_pand(_m_psllwi(_m_pmulhw(_mmx_sum, _mmx_const2), 8), _mmx_const);

            _mmx_ptr_1 += step;
            _mmx_ptr += step;
            _mmx_ptr1 += step;

            j--;
        }
        _mmx_sum = _m_paddw(*_mmx_ptr_1, *_mmx_ptr);
        *_mmx_ptr_1 = _mmx_prev;
        *_mmx_ptr = _m_pand(_m_psllwi(_m_pmulhw(_mmx_sum, _mmx_const2), 8), _mmx_const);
        pSrc += 4;
        i--;
    }
    _m_empty();
    /*
            int i, j, step;
            __m64 _mmx_prev, _mmx_sum, _mmx_const;
            __m64 *_mmx_ptr, *_mmx_ptr_1, *_mmx_ptr1, *_mmx_ptr2;

            _mmx_const = _mm_set1_pi16( 0xf000 );
            i = step = nSizeX >> 2;
            while (i)
            {
                    _mmx_ptr_1 = (__m64*)pSrc;
                    _mmx_ptr   = _mmx_ptr_1 + step;
                    _mmx_ptr1  = _mmx_ptr + step;
                    _mmx_ptr2  = _mmx_ptr1 + step;

                    *_mmx_ptr_1 = _m_psrlwi( *_mmx_ptr_1, 2 );
                    *_mmx_ptr   = _m_psrlwi( *_mmx_ptr, 2 );
                    *_mmx_ptr1  = _m_psrlwi( *_mmx_ptr1, 2 );

                    _mmx_sum = _m_paddw( _m_paddw( *_mmx_ptr_1, *_mmx_ptr ), *_mmx_ptr1 );
                    _mmx_prev = _m_pand( _mmx_sum, _mmx_const );

                    j = nSizeY - 3;
                    while (j)
                    {
                            *_mmx_ptr2 = _m_psrlwi( *_mmx_ptr2, 2 );

                            _mmx_sum = _m_paddw( _m_paddw( _m_paddw( *_mmx_ptr_1, *_mmx_ptr ),
       *_mmx_ptr1 ), *_mmx_ptr2 );
                            *_mmx_ptr_1 = _mmx_prev;
                            _mmx_prev = _m_pand( _mmx_sum, _mmx_const );

                            _mmx_ptr_1 += step;
                            _mmx_ptr   += step;
                            _mmx_ptr1  += step;
                            _mmx_ptr2  += step;

                            j--;
                    }
                    _mmx_sum = _m_paddw( _m_paddw( *_mmx_ptr_1, *_mmx_ptr ), *_mmx_ptr1 );
                    *_mmx_ptr_1 = _mmx_prev;
                    _mmx_prev = _m_pand( _mmx_sum, _mmx_const );
                    _mmx_sum = _m_paddw( *_mmx_ptr, *_mmx_ptr1 );
                    *_mmx_ptr = _mmx_prev;
                    *_mmx_ptr1 = _m_pand( _mmx_sum, _mmx_const );
                    pSrc += 4;
                    i--;
            }
            _m_empty();
    */
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
    __asm {
		pxor mm0, mm0
		mov edi, nPix
		shr edi, 2
		mov ecx, pSrc
a0:
		movd mm3, [ecx]
		xor eax, eax
		mov al, [ecx+3]
		movd mm5, eax
		punpcklbw mm5, mm5
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		packuswb mm3, mm0
		movd [ecx], mm3
		mov [ecx+3], al

		movd mm3, [ecx+4]
		xor eax, eax
		mov al, [ecx+7]
		movd mm5, eax
		punpcklbw mm5, mm5
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		packuswb mm3, mm0
		movd [ecx+4], mm3
		mov [ecx+7], al

		movd mm3, [ecx+8]
		xor eax, eax
		mov al, [ecx+11]
		movd mm5, eax
		punpcklbw mm5, mm5
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		packuswb mm3, mm0
		movd [ecx+8], mm3
		mov [ecx+11], al

		movd mm3, [ecx+12]
		xor eax, eax
		mov al, [ecx+15]
		movd mm5, eax
		punpcklbw mm5, mm5
		punpcklbw mm5, mm5
		punpcklbw mm3, mm0
		punpcklbw mm5, mm0
		pmullw mm3, mm5
		psrlw mm3, 8
		packuswb mm3, mm0
		movd [ecx+12], mm3
		mov [ecx+15], al

		add ecx, 16
		dec edi
		jne a0
		emms
    }
}
/////////////////////////////////////////////////////////////////////////////////////