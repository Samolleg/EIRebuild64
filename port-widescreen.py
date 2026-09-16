from pathlib import Path
import re
root=Path(__file__).resolve().parent/'Game'
files={}
def read(name):
    if name not in files: files[name]=(root/name).read_bytes().decode('cp1251').replace('\r\n','\n')
    return files[name]
def replace(name,old,new,count=None):
    s=read(name); assert old in s,(name,old)
    files[name]=s.replace(old,new) if count is None else s.replace(old,new,count)
def include(name):
    s=read(name); i=s.index('\n'); files[name]=s[:i+1]+'#include "WideScreen.h"\n'+s[i+1:]
def body(name,signature,transform):
    s=read(name); start=s.index(signature); start=s.index('{',start)
    # Existing functions delimit balanced scopes; skip strings and comments.
    token=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    depth=0
    for m in token.finditer(s,start):
        if m[0]=='{': depth+=1
        elif m[0]=='}':
            depth-=1
            if not depth: end=m.end();break
    files[name]=s[:start]+transform(s[start:end])+s[end:]

for name in ['I3DMain.cpp','I3DFigure.cpp','I3DObject.cpp','I3DScreenMission.cpp','DXGeometryProcessor.cpp','I3DToolTip.cpp','I3DScreenTutorial.cpp','I3DScreenGlobalMap.cpp']:
    include(name)

# Positions include the effective-area offset; sizes and local coordinates do not.
name='I3DMain.cpp'
for kind,var,fields in [('CRect','rect',['left','right']),('CPoint','point',['x'])]:
    sig=f'void CInterface3D::VirtualToScreen({kind} &{var})'
    body(name,sig,lambda b,var=var,fields=fields: re.sub(rf'{var}\.(left|right|x) = {var}\.\1 \* nScreenWidth / 800;',lambda m:f'{var}.{m[1]} = {var}.{m[1]} * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800 + (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2;',b))
for kind,var in [('CRect','rect'),('CPoint','pt')]:
    body(name,f'void CInterface3D::ScreenToVirtual({kind} &{var})',lambda b,var=var: re.sub(rf'{var}\.(left|right|x) = {var}\.\1 \* 800 / nScreenWidth;',lambda m:f'{var}.{m[1]} = ({var}.{m[1]} - (nScreenWidth - WideScreen::Effective(nScreenWidth, nScreenHeight)) / 2) * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight);',b))
replace(name,'point.cx * nScreenWidth / 800','point.cx * WideScreen::Effective(nScreenWidth, nScreenHeight) / 800')
replace(name,'pt.cx * 800 / nScreenWidth','pt.cx * 800 / WideScreen::Effective(nScreenWidth, nScreenHeight)')
replace(name,'w / fScale / 800','WideScreen::Effective() / fScale / 800')
replace(name,'this, 0, 0, 800, 600, BS_VISIBLE','this, 0, 0, nScreenWidth, nScreenHeight, BS_VISIBLE, false')
files[name]+='''
void CInterface3D::VirtualToScreenLocal(CRect &r)
{
    CPoint origin(0, 0); VirtualToScreen(origin); VirtualToScreen(r); r.OffsetRect(-origin.x, -origin.y);
}
void CInterface3D::VirtualToScreenLocal(CPoint &p)
{
    CSize size(p.x, p.y); VirtualToScreen(size); p = CPoint(size.cx, size.cy);
}
void CInterface3D::ScreenToVirtualLocal(CPoint &p)
{
    CSize size(p.x, p.y); ScreenToVirtual(size); p = CPoint(size.cx, size.cy);
}
'''
replace('I3DMain.h','    void VirtualToScreen(CRect &rect);','    void VirtualToScreenLocal(CRect &rect);\n    void VirtualToScreenLocal(CPoint &point);\n    void ScreenToVirtualLocal(CPoint &point);\n    void VirtualToScreen(CRect &rect);')
body('DXGeometryProcessor.cpp','void CDXGeometryProcessor::CreateClipScaleMatrix(',lambda b:b.replace('Identity(matrix);','Identity(matrix);\n    matrix._11 = float(WideScreen::Effective()) / theSettings.dwWidth;'))
body('I3DFigure.cpp','void CI3DFigure::SetRelativeToScreen(',lambda b:b.replace('{','{\n    xScr += WideScreen::currentShift;',1))
body('I3DObject.cpp','void CI3DPolyArea::MakeRect(',lambda b:b.replace('{','{\n    x1 += WideScreen::currentShift;\n    x2 += WideScreen::currentShift;',1))
body('I3DObject.cpp','void CI3DPolyArea::AddPoint(',lambda b:b.replace('{','{\n    point.x += WideScreen::currentShift;',1))
body('I3DObject.cpp','int CI3DSurface::DrawText(',lambda b:b.replace('VirtualToScreen(rcDraw)','VirtualToScreenLocal(rcDraw)'))

# Scope substitutions replace paired naked start/end hooks and explicit frame offsets.
mission='I3DScreenMission.cpp'
scopes={
 'CI3DObjectMap':['Init','ScrollFigures','OnIdle'],
 'CI3DObjectInfo':['Init','ScrollFigures','Draw','OnIdle'],
 'CI3DObjectWeapons':['CI3DObjectWeapons','AddWeapon','ShowSelectWeapon'],
 'CI3DObjectItems':['CI3DObjectItems','AddItem','PlaceItem'],
 'CI3DObjectSkills':['CI3DObjectSkills','AddSkill'],
 'CI3DObjectSpells':['CI3DObjectSpells','AddSpell'],
 'CI3DObjectClock':['Init','OnIdle'],
 'CI3DObjectCommands':['Init','SetMode']}
for cls,methods in scopes.items():
    side=-1 if cls in ['CI3DObjectInfo','CI3DObjectWeapons','CI3DObjectSkills','CI3DObjectCommands'] else 1
    for method in methods:
        def adjust(b,side=side):
            b=b.replace('{',f'{{\n    WideScreen::Scope wideScope({side});',1)
            # Object bounding rect is separate from its polygon areas.
            b=re.sub(r'(?<![>.])\bSetRect\(([^,\n]+),([^,\n]+),([^,\n]+),([^\n]+)\);',r'SetRect(\1 + WideScreen::currentShift,\2,\3 + WideScreen::currentShift,\4);',b)
            return b
        body(mission,cls+'::'+method+'(',adjust)
replace(mission,'CPoint(700 + slide_nCurPixels, 80)','CPoint(700 + WideScreen::VirtualShift() + slide_nCurPixels, 80)')
replace(mission,'CPoint(20 - slide_nCurPixels, 0)','CPoint(20 - WideScreen::VirtualShift() - slide_nCurPixels, 0)')
replace(mission,'(point.x - 700)','(point.x - 700 - WideScreen::VirtualShift())')
body(mission,'BOOL CI3DObjectInfo::Draw(',lambda b:b.replace('VirtualToScreen(rcTmp','VirtualToScreenLocal(rcTmp').replace('100 - slide_nCurPixels','100 - WideScreen::VirtualShift() - slide_nCurPixels'))

# Surface-relative carets/underline and camera deltas must not gain position offsets.
for name in ['I3DScrollBar.cpp','I3DScreenSaveLoad.cpp','I3DScreenConnection.cpp','I3DChat.cpp']:
    if (root/name).exists():
        s=read(name)
        files[name]=s.replace('VirtualToScreen(p1)','VirtualToScreenLocal(p1)').replace('VirtualToScreen(p2)','VirtualToScreenLocal(p2)')
for name in ['I3DHandler.cpp','I3DScreenGlobalMap.cpp']:
    replace(name,'ScreenToVirtual(ptIncrement)','ScreenToVirtualLocal(ptIncrement)')
replace('I3DToolTip.cpp','ScreenToVirtual(shift)','ScreenToVirtualLocal(shift)')
replace('I3DToolTip.cpp','rect.right > 800','rect.right > 800 + WideScreen::VirtualShift()')
replace('I3DToolTip.cpp','rect.right - 800','rect.right - 800 - WideScreen::VirtualShift()')
replace('I3DToolTip.cpp','rect.left < 0','rect.left < -WideScreen::VirtualShift()')
replace('I3DToolTip.cpp','CPoint(-rect.left, 0)','CPoint(-WideScreen::VirtualShift() - rect.left, 0)')
replace('I3DScreenGlobalMap.cpp','GetPixel(point.x, point.y)','GetPixel(point.x - WideScreen::ScreenShift() / 2, point.y)')

# Script tutorial window anchors from the addon's exact page lists.
body('I3DScreenTutorial.cpp','void CI3DScreenTutorial::RedrawPage(',lambda b:b.replace('    CI3DFigure *pFig;', '''    if (mbScript)
    {
        CString pageKey; pageKey.Format("%s:%d", (LPCSTR)mszTutorial, mnCurrentPage);
        const char *left[] = { "zt9:0", "zt12:0", "zt12:1", "zt13:0", "zt14:0", "zt14:2", "zt17:0", "zt20:0", "zt22:0" };
        const char *right[] = { "zt3:2", "zt4:0", "zt4:1", "zt7:0", "zt10:0", "zt10:1", "zt18:0" };
        for (const char *key : left) if (pageKey == key) rcWindow.OffsetRect(-WideScreen::VirtualShift(), 0);
        for (const char *key : right) if (pageKey == key) rcWindow.OffsetRect(WideScreen::VirtualShift(), 0);
    }
    CI3DFigure *pFig;''',1))

for name,s in files.items(): (root/name).write_bytes(s.replace('\n','\r\n').encode('cp1251'))
print('Modified:',', '.join(files))
