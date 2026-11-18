// Selected once at startup. Do not change CountTime or DW_SEGMENT_TIME for MCP.
static bool CountAgentTime()
{
    CorePerf::Scope coreTiming(CorePerf::CountTime);
    using AgentClock=std::chrono::steady_clock;
    static auto last=AgentClock::now();
    static auto epoch=owned::bridge_current_world_epoch();
    const auto now=AgentClock::now();
    double elapsed=std::chrono::duration<double,std::milli>(now-last).count();last=now;
    elapsed=(std::max)(0.0,elapsed-AgentTime::excludedMilliseconds);AgentTime::excludedMilliseconds=0;
    const auto currentEpoch=owned::bridge_current_world_epoch();
    if(currentEpoch!=epoch) {
        epoch=currentEpoch;elapsed=0;
        AgentTime::debtMilliseconds=AgentTime::clientDebtMilliseconds=0;
    }
    FrameDiagnostics::RawDelta(static_cast<DWORD>((std::min)(elapsed,double(MAXDWORD))));
    if(OwnedRender::E3EditorPaused())return true;
    // Previously admitted debt is retained on a rate change, never rescaled.
    AgentTime::debtMilliseconds+=elapsed*AgentTime::scale;
    AgentTime::clientDebtMilliseconds+=elapsed*AgentTime::scale;
    theCamera.UpdateFieldCampCamera();OwnedRender::E3RefreshInterface();
    CameraTerrainCollision::BeginFrame(false,DW_SEGMENT_TIME,1.0f/static_cast<float>(DW_DEFAULT_SEGMENT_TIME));
    IWorld.SetActiveState(true);
    if(theWorld.IsFailedConnection())IWorldConnector.ReqGoMainMenu();
    const auto expires=AgentClock::now()+std::chrono::milliseconds(4);
    for(;;) {
        bool progressed=false;
        // Preserve the original server/client alternation and packet predicate.
        // The server must keep running during the local connection handshake.
        const DWORD interval=DW_SEGMENT_TIME;
        if(!interval)throw std::runtime_error("background logic interval is zero");
        if(AgentTime::debtMilliseconds>=interval) {
            AgentTime::debtMilliseconds-=interval;progressed=true;
            if(!theWorldServer.Segment())return true;
            if(owned::bridge_current_world_epoch()!=epoch)break;
        }
        const DWORD clientInterval=DW_SEGMENT_TIME;
        if(AgentTime::clientDebtMilliseconds>=clientInterval&&(theWorld.HasPacket()||!theWorld.IsConnected())) {
            AgentTime::clientDebtMilliseconds-=clientInterval;progressed=true;
            if(!theWorld.Segment())return true;
            if(owned::bridge_current_world_epoch()!=epoch)break;
        }
        if(!progressed||IWorldConnector.HasReq()||AgentClock::now()>=expires)break;
    }
    const auto transitionStart=AgentClock::now();
    IWorldConnector.DoGo();
    AgentTime::excludedMilliseconds+=std::chrono::duration<double,std::milli>(AgentClock::now()-transitionStart).count();
    if(owned::bridge_current_world_epoch()!=epoch) {
        epoch=owned::bridge_current_world_epoch();
        AgentTime::debtMilliseconds=AgentTime::clientDebtMilliseconds=0;
    }
    if(!theWorld.IsCombatPause())
        theWorld.SetTimeFraction(static_cast<float>((std::min)(AgentTime::clientDebtMilliseconds/double(DW_SEGMENT_TIME),1.0)));
    // Camera/input stay on the existing wall-clock convention; visual frames
    // and OnIdle are rendered once per host iteration, never once per debt tick.
    CameraTerrainCollision::AdvanceCamera(&theCamera,static_cast<float>(elapsed)/static_cast<float>(DW_DEFAULT_SEGMENT_TIME));
    if(!theWorld.IsCombatPause()) {
        if(theCamera.CheckFlag(CAM_SAVE_PLACEMENTS))theCamera.SaveCurrentCameraPlacement(theWorld.GetPreciseTime());
        if(theCamera.CheckFlag(CAM_USE_PLACEMENTS))theCamera.PlayCamera(theWorld.GetPreciseTime());
    }
    double remaining=DW_SEGMENT_TIME-AgentTime::debtMilliseconds;
    if(theWorld.HasPacket()||!theWorld.IsConnected())remaining=(std::min)(remaining,DW_SEGMENT_TIME-AgentTime::clientDebtMilliseconds);
    const auto wait=(std::max)(0.0,remaining/AgentTime::scale);
    idleLogicDue=OwnedRender::E2Clock(timeGetTime(),false)+static_cast<DWORD>(std::ceil(wait));
    return true;
}

// The ordinary NGame::ProcessMessages and CountTime are left intact. Startup
// selects this entire loop once; no per-frame background test is added there.
static bool AgentProcessMessages()
{
    MSG msg{};bool active=true;
    static auto lastRender=std::chrono::steady_clock::now();
    C2DScreenWait::Update();
    while(!PeekMessage(&msg,0,0,0,PM_NOREMOVE)) {
        RuntimeFrame::Begin();
        CorePerf::Frame coreFrame;
        GameJournal::MemoryFrame(GetTickCount());C2DScreenWait::Update();
        NGame::UpdateRenderEnable();
        const bool diagnostic=FrameDiagnostics::Begin(ModShadowSchedule::generationTicks);
        {FrameDiagnostics::Scope timing(FrameDiagnostics::Simulation);owned::frame_waits::PreparationInterval measuringPreparation;active=CountAgentTime();}
        if(active) {
            const bool behind=AgentTime::debtMilliseconds>=DW_SEGMENT_TIME||
                (AgentTime::clientDebtMilliseconds>=DW_SEGMENT_TIME&&(theWorld.HasPacket()||!theWorld.IsConnected()));
            // GetFrameTime is the last *rendered* interval. Reusing it on each
            // catch-up slice would multiply UI timers. Admit UI together with
            // render, at least once a second even when CPU cannot repay debt.
            if(!behind||std::chrono::steady_clock::now()-lastRender>=std::chrono::seconds(1)) {
                const auto idleStart=std::chrono::steady_clock::now();
                {FrameDiagnostics::Scope timing(FrameDiagnostics::Idle);CorePerf::Scope coreTiming(CorePerf::OnIdle);theInterface.OnIdle();}
                // A synchronous UI/movie wait does not age the paused world.
                AgentTime::excludedMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-idleStart).count();
                NGame::DrawFrame(NGame::bEnableFlip);lastRender=std::chrono::steady_clock::now();
            }
        }
        if(diagnostic) {
            const auto& anchor=theCamera.GetAnchorPoint();
            FrameDiagnostics::End(ModShadowSchedule::generationTicks,anchor.x,anchor.y);
        }
        LiveObserver::Frame();WaitForIdleWork();
    }
    CorePerf::Scope coreMessages(CorePerf::Messages);
    if(!GetMessage(&msg,0,0,0))return false;
    FrameDiagnostics::Message();TranslateMessage(&msg);
    if(active)NGame::WndProc(msg.message,msg.wParam,msg.lParam);
    else NGame::ShortWndProc(NGame::hWnd,msg.message,msg.wParam,msg.lParam);
    coreMessages.Finish();CorePerf::Flush();return true;
}
