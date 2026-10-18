#pragma once
#include <afxwin.h>
#include "owned_ui_layers.h"

namespace OwnedRender {
// Direct entry points: MFC lives in a separate DLL, so no host import hook
// can capture its drawing. Ordinary DCs retain their real GDI behavior.
inline int E2DrawText(CDC* dc,LPCSTR text,int count,CRect& rect,UINT format) {
    return owned::ui_layer_draw_text_a(dc->GetSafeHdc(),text,count,&rect,format);
}
inline int E2DrawText(CDC* dc,LPCSTR text,int count,const CRect& rect,UINT format) {
    CRect copy=rect;
    return E2DrawText(dc,text,count,copy,format);
}
inline void E2FillSolidRect(CDC* dc,const CRect& rect,COLORREF color) {
    const HDC target=dc->GetSafeHdc();
    const COLORREF previous=::SetBkColor(target,color);
    owned::ui_layer_ext_text_out_a(target,0,0,ETO_OPAQUE,&rect,nullptr,0,nullptr);
    ::SetBkColor(target,previous);
}
inline void E2FillSolidRect(CDC* dc,int x,int y,int width,int height,COLORREF color) {
    E2FillSolidRect(dc,CRect(x,y,x+width,y+height),color);
}
inline int E2FrameRect(CDC* dc,const CRect& rect,CBrush* brush) {
    return owned::ui_layer_frame_rect(dc->GetSafeHdc(),&rect,
        static_cast<HBRUSH>(brush->GetSafeHandle()));
}
inline BOOL E2LineTo(CDC* dc,int x,int y) {
    return owned::ui_layer_line_to(dc->GetSafeHdc(),x,y);
}
inline COLORREF E2SetPixel(CDC* dc,int x,int y,COLORREF color) {
    return owned::ui_layer_set_pixel(dc->GetSafeHdc(),x,y,color);
}
inline BOOL E2TextOut(CDC* dc,int x,int y,LPCSTR text,int count) {
    return owned::ui_layer_text_out_a(dc->GetSafeHdc(),x,y,text,count);
}
inline BOOL E2PolylineTo(CDC* dc,const POINT* points,DWORD count) {
    return owned::ui_layer_polyline_to(dc->GetSafeHdc(),points,count);
}
inline BOOL E2PatBlt(CDC* dc,int x,int y,int width,int height,DWORD rop) {
    return owned::ui_layer_pat_blt(dc->GetSafeHdc(),x,y,width,height,rop);
}
inline LONG E2TabbedTextOut(CDC* dc,int x,int y,LPCSTR text,int count,
                          int tabCount,const INT* tabs,int tabOrigin) {
    return owned::ui_layer_tabbed_text_out_a(dc->GetSafeHdc(),x,y,text,count,tabCount,tabs,tabOrigin);
}
inline BOOL E2GrayString(CDC* dc,HBRUSH brush,GRAYSTRINGPROC proc,LPARAM data,
                        int count,int x,int y,int width,int height) {
    return owned::ui_layer_gray_string_a(dc->GetSafeHdc(),brush,proc,data,count,x,y,width,height);
}
inline LRESULT E2SendMessage(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    return owned::ui_layer_send_message_a(window,message,wparam,lparam);
}
}
