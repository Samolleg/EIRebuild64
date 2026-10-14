#pragma once
#include <camp_inventory.h>

class CI3DScreenCamp;
class CI3DObject;
class CI3DObjectInventory;
class CI3DObjectInventoryItems;
class CI3DItemScreen;
class CRenderer;
class CItem;
class CI3DFigure;
struct SDrawItem;

namespace owned {struct PreparedCampIcon;}
namespace OwnedRender::E4 {
enum class Role { none, root, background, info, help, stats, wagon, vendor,
                  quick, spells, perks, trade, repair, construction, comparison, blueprint };
enum class List { stash, spell_vendor, item_vendor, spells, learned, available };
struct Scroll { bool visible=false; int position=0, total=0, page=0, category=0; };
struct Host {
    void* context=nullptr;
    bool (*current)(void*)=nullptr;
    Scroll (*scroll)(void*,List)=nullptr;
    void (*scroll_to)(void*,List,int)=nullptr;
    void (*retire)(void*)=nullptr;
    void (*wheel_sound)(void*)=nullptr;
};
void Activate(Host host) noexcept;
void Deactivate(void* owner) noexcept;
void BeginDraw(int mode) noexcept;
void PublishDecoration(const owned::CampDecoration& decoration) noexcept;
void DrawDecorationBackground(CRenderer* renderer);
void FinishDraw2D(CRenderer* renderer);
bool Active() noexcept;
bool Current() noexcept;
const owned::camp::Layout& Layout() noexcept;
owned::camp::Rect Stage() noexcept;
owned::camp::Rect Actions() noexcept;
owned::camp::Placement Uniform(Role role) noexcept;
owned::camp::Placement PerkBand(float x,float y) noexcept;
RECT Pixels(owned::camp::Rect rect) noexcept;
void SetCards(std::array<bool,3> cards) noexcept;
bool Popup(owned::CampRenderRequest request,owned::CampPopupDraw draw,void* data);
Role Drawing() noexcept;
bool RailTrack(List list,owned::camp::Rect& track) noexcept;
struct Scope {
    Role previous;
    explicit Scope(Role role) noexcept;
    ~Scope();
};
struct Frame {CRenderer* renderer;explicit Frame(CRenderer*) noexcept;~Frame();};
struct Preview {
    Preview* previous;
    std::uint32_t rgb;
    bool enabled;
    explicit Preview(std::uint32_t color,bool active=true) noexcept;
    ~Preview();
};
struct ItemCost {
    std::uint64_t begin=0;
    explicit ItemCost(bool measure) noexcept;
    ~ItemCost();
};
struct MissCost {
    std::uint64_t begin=0;
    explicit MissCost(bool miss) noexcept;
    ~MissCost();
};
struct PreviewMaterial {
    CI3DFigure* figure;
    unsigned long previous;
    PreviewMaterial(CI3DFigure* value,bool active,std::uint32_t rgb);
    ~PreviewMaterial();
};
void FigureMiss() noexcept;
void ObserveGrid(bool vendor,int first,int count,int category,int columns,int rows) noexcept;
bool Message(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept;
}

// The only typed accessor for camp's private UI state. No retail ABI/offsets.
struct E4CampAccess {
    static void Activate(CI3DScreenCamp*);
    static void Deactivate(CI3DScreenCamp*);
    static void BeginDraw(CI3DScreenCamp*);
    static OwnedRender::E4::Role RoleOf(CI3DObject*);
    static bool Draw(CI3DObject*,CRenderer*);
    static bool DrawGrid(CI3DObjectInventoryItems*,CRenderer*);
    static bool DrawSpells(class CI3DObjectSpellSlots*,CRenderer*);
    static bool SpellMouse(class CI3DObjectSpellSlots*,POINT,int&,CItem*&);
    static void SpellCurrent(class CI3DObjectSpellSlots*,int);
    static void Cards(CI3DScreenCamp*,CRenderer*);
    static void CardText(CI3DScreenCamp*);
    static bool CardDraw(class CI3DObjectItemInfo*,CRenderer*);
    static bool CardTextDraw(class CI3DObjectItemInfo*);
    static bool Blit(class CI3DSurface*);
    static int LearnedRows(CI3DObject*);
    static void Hover(class CI3DObjectItemInfo*,bool);
    static bool Draw2D(CI3DObject*);
    static int Hit(CI3DObject*,POINT);
    static bool TipInside(CI3DObject*,int,POINT,bool&);
    static void BeginInput();
    static void PrepareGrid(CI3DObjectInventoryItems*);
    static bool Inside(CI3DObject*,POINT,bool&);
    static bool MouseItem(CI3DObjectInventoryItems*,POINT,int&,CItem*&);
    static bool Idle(CI3DObjectInventory*);
    static bool SetToItem(CI3DObjectInventoryItems*,int target);
    static bool ItemPlacement(CI3DItemScreen*,const SDrawItem&,owned::camp::Composition&);
    static std::uint32_t PreviewRGB(CI3DItemScreen*,CItem*);
    static OwnedRender::E4::Scroll Scroll(void*,OwnedRender::E4::List);
    static void ScrollTo(void*,OwnedRender::E4::List,int);
    static owned::camp::Placement Area(CI3DObject*,OwnedRender::E4::Role,int);
    static int First(CI3DObjectInventoryItems*);
};

namespace OwnedRender::E4 {
bool OwnsDraw() noexcept;
void RegisterTextures(bool high_resolution) noexcept;
struct Icon {
    Icon* previous;
    CI3DFigure* figure;
    std::shared_ptr<const owned::PreparedCampIcon> value;
    Icon(CI3DFigure*, std::shared_ptr<const owned::PreparedCampIcon>);
    ~Icon();
};
bool BindIcon(CI3DFigure*, CRenderer*);
std::shared_ptr<const owned::PreparedCampIcon> BoundIcon(CI3DFigure*);
}
