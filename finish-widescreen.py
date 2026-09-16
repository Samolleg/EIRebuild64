from pathlib import Path
p=Path('work/eirebuild-local/port-widescreen.py')
exec(p.read_text().split('for name in [')[0].replace("Path(__file__).resolve().parent/'Game'", "Path('work/eirebuild-local/Game')"))
n='I3DScreenTutorial.cpp'
s=read(n); a=s.index('    if (mbScript)\n    {\n        CString pageKey'); z=s.index('    CI3DFigure *pFig;',a)
block=s[a:z].replace('rcWindow.OffsetRect(-WideScreen::VirtualShift(), 0);','{ l -= WideScreen::VirtualShift(); r -= WideScreen::VirtualShift(); }').replace('rcWindow.OffsetRect(WideScreen::VirtualShift(), 0);','{ l += WideScreen::VirtualShift(); r += WideScreen::VirtualShift(); }')
files[n]=s[:a]+s[z:]
replace(n,'            CI3DFigure *pFig = AddFigure();', '\n'.join('        '+line if line else '' for line in block.split('\n'))+'\n            CI3DFigure *pFig = AddFigure();',1)
body('I3DObject.cpp','void CI3DSurface::DrawRect(',lambda b:b.replace('VirtualToScreen(rcDraw)','VirtualToScreenLocal(rcDraw)'))
body('I3DObject.cpp','DWORD CI3DSurface::GetPixel(',lambda b:b.replace('if (!pSurface)','if (!pSurface || nX < 0 || nY < 0 || nX >= pSurface->GetWidth() || nY >= pSurface->GetHeight())',1).replace('    int pixelIdx', '    if (!pixelBuffer) return 0;\n    int pixelIdx',1))
panels='''
    // WideScreen.cpp side fillers; keep the original center layout.
    CI3DFigure *wideLeft = AddFigure();
    wideLeft->MakeBar(2, 100, -float(WideScreen::VirtualShift()), 0, 0, 600, 227, 62, 256, 176);
    wideLeft->SetAmbient(.3f, .3f, .3f);
    CI3DFigure *wideRight = AddFigure();
    wideRight->MakeBar(2, 100, 800, 0, 800 + float(WideScreen::VirtualShift()), 600, 256, 176, 227, 62);
    wideRight->SetAmbient(.3f, .3f, .3f);
'''
for n in ['I3DScreenCamp.cpp','I3DScreenChoosePers.cpp']: include(n)
body('I3DScreenCamp.cpp','void CI3DScreenCamp::OnActivate()',lambda b:b[:-1]+'    LPTEXTURE wideTexture = AddTexture("battle00");\n'+panels+'    wideLeft->SetTexture(wideTexture);\n    wideRight->SetTexture(wideTexture);\n}\n')
body('I3DScreenChoosePers.cpp','void CI3DScreenChoosePers::OnActivate()',lambda b:b[:-1]+panels+'    wideLeft->Show(FALSE);\n    wideRight->Show(FALSE);\n}\n')
body('I3DScreenChoosePers.cpp','void CI3DScreenChoosePers::SetMode(',lambda b:b[:-1]+'''    if (figures.GetSize() == 21)
    {
        figures[19]->Show(eNewMode != ISM_CHOOSEPERS);
        figures[20]->Show(eNewMode != ISM_CHOOSEPERS);
    }
}
''')
for n,s in files.items(): (root/n).write_bytes(s.replace('\n','\r\n').encode('cp1251'))
print('Completed tutorial highlight rectangles, local frames, map bounds, camp/character side fillers.')
