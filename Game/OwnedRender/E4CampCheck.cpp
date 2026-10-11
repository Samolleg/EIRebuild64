#include "E4Camp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace OwnedRender::E4;
using namespace owned;
namespace {
void require(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Fixture { bool current=true;int row=0,retired=0; } fixture;
Host host(){return {&fixture,[](void* p){return static_cast<Fixture*>(p)->current;},
    [](void* p,List list){return Scroll{list==List::stash,static_cast<Fixture*>(p)->row,12,8,5};},
    [](void* p,List,int row){static_cast<Fixture*>(p)->row=row;},
    [](void* p){++static_cast<Fixture*>(p)->retired;}};}
}
int main(){
    SetEnvironmentVariableA("EI_RENDERER_ATTRIBUTION","1");
    CampBlitPiece pieces[kCampBlitPieces];RECT mapped{};CampDecoration decoration;
    std::uint32_t rgb=99;
    require(!camp_inventory_notice(),"menu has no camp notice");
    require(!camp_inventory_take_decoration(decoration),"menu has no camp decoration");
    require(!camp_inventory_preview_specular_rgb(rgb)&&rgb==99,"menu preview does not overwrite caller");
    require(camp_inventory_blit({0,0,100,100},{0,0,100,100},pieces)==-1,"menu blit passes through");
    camp_inventory_output(1920,1080);Activate(host());BeginDraw(0);
    require(Active()&&Current(),"real c56 16:9 layout admitted");
    const auto grid=Layout().grid(false);const auto p=Pixels({grid.left+1,grid.top+1,grid.left+2,grid.top+2});
    require(camp_inventory_wheel({p.left,p.top},-60,0)&&fixture.row==0,"partial wheel retained");
    require(camp_inventory_wheel({p.left,p.top},-60,0)&&fixture.row==1,"wheel uses rows exactly once");
    fixture.current=false;
    require(!camp_inventory_wheel({p.left,p.top},-120,0)&&fixture.row==1,"modal screen owns input");
    fixture.current=true;
    require(!camp_inventory_input(nullptr,WM_KEYDOWN,'A',0,false),"keys stay in native dispatcher");
    camp_inventory_input(nullptr,WM_KILLFOCUS,0,0,false);
    require(!camp_inventory_wheel({p.left,p.top},-120,0),"focus loss rejects wheel");
    camp_inventory_input(nullptr,WM_SETFOCUS,0,0,false);
    HWND window=CreateWindowExW(0,L"STATIC",L"E4 hidden capture contract",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"hidden capture fixture created");
    owned::camp::Rect rail;require(RailTrack(List::stash,rail),"overflow produces a rail");
    const auto rail_pixels=Pixels(rail);const int rail_x=(rail_pixels.left+rail_pixels.right)/2;
    require(camp_inventory_input(window,WM_LBUTTONDOWN,0,MAKELPARAM(rail_x,rail_pixels.top+2),false)&&GetCapture()==window,"rail acquires native capture");
    require(camp_inventory_input(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(rail_x,rail_pixels.bottom),false)&&fixture.row==4,"drag clamps at final row");
    require(camp_inventory_input(window,WM_LBUTTONUP,0,MAKELPARAM(rail_x,rail_pixels.bottom),false)&&GetCapture()!=window,"release cancels capture");
    DestroyWindow(window);
    {
        Scope scope(Role::stats);
        const auto src=Pixels({0,100,200,400});const auto expected=Pixels(Layout().stats);
        require(camp_inventory_blit(src,{0,0,200,300},pieces)==1,"stats maps one band");
        require(std::abs(pieces[0].destination.left-expected.left)<=1&&std::abs(pieces[0].destination.bottom-expected.bottom)<=1,"stats affine agrees with c56 placement");
        require(camp_inventory_clear(src,mapped),"scoped clear is transformed");
        require(!camp_inventory_clear({0,0,1920,1080},mapped),"full output clear is not transformed");
        require(camp_inventory_blit({0,0,0,0},{0,0,0,0},pieces)==0,"empty blit cannot divide by zero");
        {Preview preview(0x123456);require(camp_inventory_preview_specular_rgb(rgb)&&rgb==0x123456,"preview uses current synchronous item");}
        require(!camp_inventory_preview_specular_rgb(rgb),"preview borrow ends with draw");
    }
    {
        Scope scope(Role::trade);const auto src=Pixels({200,100,600,500});
        require(camp_inventory_blit(src,{0,0,400,400},pieces)==16,"trade maps all sixteen cells");
    }
    CampDecoration published;published.count=1;PublishDecoration(published);
    require(camp_inventory_take_decoration(decoration)&&decoration.count==1,"decoration is an actual publication");
    require(!camp_inventory_take_decoration(decoration),"decoration consumed once");
    ObserveGrid(false,4,48,5,4,8);
    {ItemCost item(true);{MissCost miss(true);FigureMiss();}}
    const auto cost=camp_inventory_take_scroll_cost();
    require(cost.observed&&cost.valid&&cost.item_calls==1&&cost.miss_calls==1&&cost.figure_misses==1,"real QPC scopes count item and cache misses");
    require(!camp_inventory_take_scroll_cost().observed,"scroll cost consumed once");
    camp_inventory_art_failed();require(std::strstr(camp_inventory_notice(),"artwork"),"art failure reported");
    camp_inventory_popup_failed();require(std::strstr(camp_inventory_notice(),"popup"),"popup failure reported");
    camp_inventory_popup_published(false);
    camp_inventory_set_wide(false);BeginDraw(0);require(!Active(),"existing wide setting applies at draw");
    camp_inventory_set_wide(true);BeginDraw(0);require(Active(),"wide layout restored");
    camp_inventory_output(1280,800);require(!Active(),"live resize invalidates camp");
    Deactivate(&fixture);Activate(host());BeginDraw(0);require(Active()&&Layout().compact,"reopen uses compact 16:10 layout");
    Deactivate(&fixture);require(!Active()&&!camp_inventory_notice(),"deactivate retires borrowed owner");
    require(fixture.retired>=2,"resources retired on lifecycle boundaries");
    camp_inventory_cancel_drag();camp_inventory_retire_textures();
    std::puts("PASS: E4 menu/lifecycle, c56 layouts, blit, wheel, focus, preview, publication and QPC contracts (synthetic host; no game)");
}
