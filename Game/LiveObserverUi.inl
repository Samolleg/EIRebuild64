// Request-driven UI observations and ordinary in-process input dispatch.
// Included in LiveObserver's private namespace, after UnitDetail.
size_t GetLimit(const json& params,size_t maximum,size_t fallback);
std::string UiId(CI3DScreen* screen,size_t object,int area) {
    return "ui:"+std::to_string(LiveUiAccess::Serial(screen))+":"+
        std::to_string(LiveUiAccess::Context(screen))+":"+std::to_string(object)+":"+std::to_string(area);
}
json UiSummary() {
    auto* top=theInterface.GetCurrentScreen();const size_t depth=LiveUiAccess::Depth();
    json result={{"screen",LiveUiAccess::Name(top)},{"screen_id",LiveUiAccess::Serial(top)},
        {"context",LiveUiAccess::Context(top)},{"mode",LiveUiAccess::Mode(top)},{"kind",LiveUiAccess::Kind(top)},
        {"waiting_for_input",LiveUiAccess::Waiting(top)},{"modal",depth>1},{"transition_pending",LiveUiAccess::Pending()},
        {"cutscene",LiveUiAccess::CutsceneRunning()},{"cutscene_skippable",LiveUiAccess::Cutscene(top)&&!LiveUiAccess::Pending()},
        {"saved_background",LiveUiAccess::SavedBackground()},
        {"width",theInterface.GetWidth()},{"height",theInterface.GetHeight()},
        {"coordinate_space","native_canvas_pixels"},{"windows",json::array()},{"windows_truncated",depth>16},{"truncated",depth>16}};
    const size_t first=depth>16?depth-16:0;
    for(size_t i=first;i<depth;++i) {
        auto* screen=LiveUiAccess::Screen(i);
        result["windows"].push_back({{"id",LiveUiAccess::Serial(screen)},{"screen",LiveUiAccess::Name(screen)},
            {"kind",LiveUiAccess::Kind(screen)},{"mode",LiveUiAccess::Mode(screen)},
            {"title",Text(LiveUiAccess::Title(screen),512)},{"text",Text(LiveUiAccess::WindowText(screen),1024)},
            {"modal",i>0},{"active",i+1==depth}});
    }
    return result;
}
void AttachUiStatus(json& result) {
    auto ui=UiSummary();
    result["world_screen"]=result.value("screen",std::string("none"));
    result["screen"]=ui["screen"];result["cutscene"]=ui["cutscene"];
    result["window_waiting"]=LiveUiAccess::Waiting(theInterface.GetCurrentScreen());
    result["ui"]=std::move(ui);result["time_scale"]=1;result["time_scale_supported_max"]=1;
    auto blockers=json::array();
    if(LiveUiAccess::Depth()>1)blockers.push_back({{"reason","modal_window"}});
    if(LiveUiAccess::Pending())blockers.push_back({{"reason","ui_transition_pending"}});
    if(result.value("paused",false))blockers.push_back({{"reason","world_paused"}});
    auto* player=theWorldServer.bMapLoaded?theWorldServer.GetPlayer(0):nullptr;
    if(player)for(int i=0;i<player->party.people.GetSize() && i<16;++i) {
        const int id=player->party.people[i].nID;auto* unit=theWorldServer.GetUnit(id);
        if(!unit){blockers.push_back({{"id",id},{"reason","party_member_missing_in_mission"}});continue;}
        if(unit->IsDead())blockers.push_back({{"id",id},{"reason","unit_dead"}});
        if(unit->CheckFlag(UNIT_BLOCKED))blockers.push_back({{"id",id},{"reason","unit_blocked"}});
        if(unit->CheckFlag(UNIT_STUN))blockers.push_back({{"id",id},{"reason","unit_stunned"}});
        auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
        if(!server)continue;
        const auto& command=server->GetScriptCommand();
        if(command.cmd==UC_PLAYANIM || command.cmd==UC_PLAYANIMCYCLE)
            blockers.push_back({{"id",id},{"reason","scripted_animation"},{"command",int(command.cmd)},
                {"command_name",command.cmd==UC_PLAYANIM?"play_animation":"play_animation_cycle"},
                {"animation",command.dwParam},{"interruptible",LiveSnapshotAccess::Interruptible(*server)}});
        else if(!LiveSnapshotAccess::Interruptible(*server))
            blockers.push_back({{"id",id},{"reason",LiveSnapshotAccess::Suffering(*server)?"hurt_animation":"unit_animation"},
                {"command",int(command.cmd)}});
    }
    result["squad_order_blockers"]=std::move(blockers);
    result["squad_order_blockers_truncated"]=player && player->party.people.GetSize()>16;
}
bool UiState(Job& job,Clock::time_point deadline) {
    const auto& params=job.request.at("params");const size_t limit=GetLimit(params,200,100);
    auto* screen=theInterface.GetCurrentScreen();
    if(!job.phase) {
        job.phase=1;job.uiScreen=LiveUiAccess::Serial(screen);
        job.uiContext=LiveUiAccess::Context(screen);job.uiWidth=theInterface.GetWidth();job.uiHeight=theInterface.GetHeight();
        job.response=UiSummary();job.response["ok"]=true;job.response["buttons"]=json::array();job.response["limit"]=limit;
        job.response["consistency"]="same_screen_context_and_canvas; availability_rechecked_at_click";
    }
    if(job.uiScreen!=LiveUiAccess::Serial(screen) || job.uiContext!=LiveUiAccess::Context(screen) ||
       job.uiWidth!=theInterface.GetWidth() || job.uiHeight!=theInterface.GetHeight()) {
        job.response=Error("ui_changed_during_scan; request_ui_state_again");return true;
    }
    while(job.bucket<size_t(LiveUiAccess::ObjectCount(screen)) && Clock::now()<deadline) {
        auto* object=LiveUiAccess::Object(screen,job.bucket);
        if(job.item>=size_t(LiveUiAccess::AreaCount(object))){++job.bucket;job.item=0;continue;}
        const int index=int(job.item++);LiveUiAccess::Area area;
        if(!LiveUiAccess::ReadArea(screen,job.bucket,index,area))continue;
        json row={{"id",UiId(screen,job.bucket,index)},{"caption",Text(area.caption,512)},
            {"enabled",area.enabled},{"availability_basis",area.exactAvailability?"screen_action_predicate":"visible_hit_region"},
            {"rect",{area.rect.left,area.rect.top,area.rect.right,area.rect.bottom}},
            {"object",job.bucket},{"area",index}};
        row["point"]=area.hasPoint?json::array({area.point.x,area.point.y}):json(nullptr);
        job.response["buttons"].push_back(std::move(row));
        if(job.response["buttons"].size()>=limit){job.response["truncated"]=true;return true;}
    }
    return job.bucket>=size_t(LiveUiAccess::ObjectCount(screen));
}
std::pair<UINT,UINT> UiKey(const json& value) {
    if(!value.is_string())throw std::runtime_error("key must be a named key, letter or digit");
    auto key=value.get<std::string>();
    if(key.empty() || key.size()>16)throw std::runtime_error("invalid UI key");
    for(char& c:key)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    UINT code=0;
    if(key=="ESC"||key=="ESCAPE")code=VK_ESCAPE;
    else if(key=="ENTER"||key=="RETURN")code=VK_RETURN;
    else if(key=="SPACE")code=VK_SPACE;
    else if(key=="TAB")code=VK_TAB;
    else if(key=="BACKSPACE")code=VK_BACK;
    else if(key=="DELETE")code=VK_DELETE;
    else if(key=="LEFT")code=VK_LEFT;
    else if(key=="RIGHT")code=VK_RIGHT;
    else if(key=="UP")code=VK_UP;
    else if(key=="DOWN")code=VK_DOWN;
    else if(key=="HOME")code=VK_HOME;
    else if(key=="END")code=VK_END;
    else if(key=="PAGEUP")code=VK_PRIOR;
    else if(key=="PAGEDOWN")code=VK_NEXT;
    else if(key.size()==1 && ((key[0]>='A'&&key[0]<='Z')||(key[0]>='0'&&key[0]<='9')))code=UINT(key[0]);
    else if(key[0]=='F' && key.size()<=3) {
        unsigned n=0;for(size_t i=1;i<key.size();++i){if(key[i]<'0'||key[i]>'9')throw std::runtime_error("invalid function key");n=n*10+unsigned(key[i]-'0');}
        if(n>=1&&n<=12)code=VK_F1+n-1;
    }
    if(!code)throw std::runtime_error("unsupported UI key; modifiers and text entry are not supported");
    const UINT mapped=MapVirtualKeyW(code,MAPVK_VK_TO_VSC_EX);
    if(!mapped)throw std::runtime_error("UI key has no scan code");
    return {code,(mapped&0xffu)|((mapped&0xff00u)?0x100u:0u)};
}
void CleanupUiInput(Job& job) noexcept {
    try {
        if(job.uiMouseDown){job.uiMouseDown=false;theInterface.OnLButtonUp(0,CPoint(job.uiX,job.uiY));}
        if(job.uiKeyDown){job.uiKeyDown=false;theInterface.OnKeyUp(job.uiKey,1,job.uiScan|0x8000u);}
    }catch(...){ChannelError("ui_input_release",0);}
}
bool UiInput(Job& job,Clock::time_point deadline) {
    const auto& params=job.request.at("params");const auto& method=job.method;
    if(!job.phase) {
        if(LiveUiAccess::Pending())throw std::runtime_error("ui_transition_pending; wait_for_ui_state");
        if(!theInterface.GetCurrentScreen())throw std::runtime_error("no_active_ui_screen");
        job.uiScreen=LiveUiAccess::Serial(theInterface.GetCurrentScreen());
        job.uiContext=LiveUiAccess::Context(theInterface.GetCurrentScreen());
        job.uiWidth=theInterface.GetWidth();job.uiHeight=theInterface.GetHeight();
        job.response={{"ok",true},{"status","dispatched"},{"path","CInterface3D_player_input"},{"truncated",false}};
        if(method=="ui_key" || method=="cutscene_skip") {
            if(method=="cutscene_skip" && !LiveUiAccess::Cutscene(theInterface.GetCurrentScreen()))
                throw std::runtime_error("no_skippable_cutscene; scripted_unit_animation_is_not_a_skip_key");
            const auto key=UiKey(method=="cutscene_skip"?json("ESCAPE"):params.at("key"));
            job.uiKey=key.first;job.uiScan=key.second;job.phase=20;
        } else if(params.contains("id")) {
            if(method!="ui_click" || params.contains("x") || params.contains("y") || !params.at("id").is_string())
                throw std::runtime_error("supply an id or both x and y");
            const auto id=params.at("id").get<std::string>();
            if(id.size()>128)throw std::runtime_error("invalid UI id");
            job.phase=1;
        } else {
            const auto coordinate=[&](const char* key,int bound) {
                const auto& value=params.at(key);
                if(!value.is_number_integer() || value<0 || value>=bound)throw std::runtime_error("UI coordinates must be integer native-canvas pixels");
                return value.get<int>();
            };
            job.uiX=coordinate("x",theInterface.GetWidth());job.uiY=coordinate("y",theInterface.GetHeight());job.phase=10;
        }
        return false;
    }
    auto* screen=theInterface.GetCurrentScreen();
    if(job.phase<=20 && job.phase!=12 && (job.uiScreen!=LiveUiAccess::Serial(screen) || job.uiContext!=LiveUiAccess::Context(screen) ||
       job.uiWidth!=theInterface.GetWidth() || job.uiHeight!=theInterface.GetHeight()))
        throw std::runtime_error("ui_changed_before_input; request_ui_state_again");
    if(job.phase==1) {
        const auto id=params.at("id").get<std::string>();
        while(job.bucket<size_t(LiveUiAccess::ObjectCount(screen)) && Clock::now()<deadline) {
            auto* object=LiveUiAccess::Object(screen,job.bucket);
            if(job.item>=size_t(LiveUiAccess::AreaCount(object))){++job.bucket;job.item=0;continue;}
            const int index=int(job.item++);
            if(UiId(screen,job.bucket,index)!=id)continue;
            LiveUiAccess::Area area;
            if(!LiveUiAccess::ReadArea(screen,job.bucket,index,area) || !area.enabled)
                throw std::runtime_error("UI button is hidden, disabled or has no native hit point");
            job.uiX=area.point.x;job.uiY=area.point.y;job.response["id"]=id;job.phase=10;return false;
        }
        if(job.bucket>=size_t(LiveUiAccess::ObjectCount(screen)))throw std::runtime_error("unknown_or_stale_ui_id; request_ui_state_again");
        return false;
    }
    if(job.phase==10) {
        syntheticCursorActive=true;WriteUiCursor(job.uiX,job.uiY);LiveUiAccess::Cursor(CPoint(job.uiX,job.uiY));
        E4CampAccess::BeginInput();theInterface.OnMouseMove(0,CPoint(job.uiX,job.uiY));
        job.response["point"]={job.uiX,job.uiY};
        if(method=="ui_hover"){job.response["status"]="done";return true;}
        job.phase=11;return false;
    }
    if(job.phase==11) {
        if(job.uiScreen!=LiveUiAccess::Serial(screen) || job.uiContext!=LiveUiAccess::Context(screen))
            throw std::runtime_error("ui_changed_before_click; command_not_retried");
        E4CampAccess::BeginInput();job.uiMouseDown=true;theInterface.OnLButtonDown(MK_LBUTTON,CPoint(job.uiX,job.uiY));
        job.phase=12;return false;
    }
    if(job.phase==12){CleanupUiInput(job);job.response["status"]="done";job.response["ui"]=UiSummary();return true;}
    if(job.phase==20) {
        if(job.uiScreen!=LiveUiAccess::Serial(screen) || job.uiContext!=LiveUiAccess::Context(screen))
            throw std::runtime_error("ui_changed_before_key; command_not_retried");
        if(!syntheticCursorActive){syntheticCursorActive=true;const auto p=theCursor.GetPos();WriteUiCursor(p.x,p.y);}
        job.uiKeyDown=true;theInterface.OnKeyDown(job.uiKey,1,job.uiScan);job.phase=21;return false;
    }
    CleanupUiInput(job);job.response["status"]="done";job.response["ui"]=UiSummary();return true;
}
bool TimeScale(Job& job) {
    const auto& value=job.request.at("params").at("k");
    if(!value.is_number_integer() || value<1 || value>16)throw std::runtime_error("k must be an integer from 1 to 16");
    if(!AgentBackground::Enabled())throw std::runtime_error("time_scale requires --background");
    if(value==1)job.response={{"ok",true},{"status","done"},{"time_scale",1},{"supported_max",1},{"truncated",false}};
    else {
        job.response=Error("time_scale_unavailable_without_logic_changes");job.response["time_scale"]=1;
        job.response["reason"]="CountTime client pacing is frame-dependent; UI, speech and ambient decisions also consume real time";
        job.response["supported_max"]=1;
    }
    return true;
}
