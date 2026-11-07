// Included once inside LiveObserver's command implementation. No frame polling outside a request.
struct GlobalMapOperationState {
    ObjectId worldId=0;
    EInterface origin=IFACE_NONE;
};
GlobalMapOperationState globalMapOperation;

bool GlobalMapScreen(EInterface iface) {
    auto* screen=theInterface.GetCurrentScreen();
    if(theInterface.GetScreensNum()!=1||!screen)return false;
    if(iface==IFACE_GLOBAL_MAP)return dynamic_cast<CI3DScreenGlobalMap*>(screen)!=nullptr;
    if(iface==IFACE_MISSION)return dynamic_cast<CI3DScreenMission*>(screen)!=nullptr;
    if(iface==IFACE_BASE)return dynamic_cast<CI3DScreenBase*>(screen)!=nullptr;
    return false;
}

// A restored map is ready with an undeployed party, after the local handshake.
// Keep this admission separate from the unit/ownership checks for a mission.
bool LoadedGlobalMapReady() {
    auto& world=theWorldServer;
    if(!world.bMapLoaded||world.GetServerMode()!=SERV_MODE_LOCAL||IWorldConnector.HasReq()||
       (world.curInterface!=IFACE_MISSION&&world.curInterface!=IFACE_BASE)||
       world.GetCurrentZoneName()!=operation.expectedZone.c_str()||
       world.GetCurrentZoneName()!=IWorldClient.GetCurrentZoneName()||
       !GlobalMapScreen(IFACE_GLOBAL_MAP))return false;
    auto* client=theWorld.GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    auto* player=client?client->GetPlayer():nullptr;
    auto* server=serverPlayer?static_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific()):nullptr;
    if(!client||!player||!server||client->GetStatus()!=CPlayerClient::CPS_CAMP||
       server->GetStatus()!=CPlayerServer::PSS_CAMP||player->curInterface!=IFACE_GLOBAL_MAP||
       serverPlayer->curInterface!=IFACE_GLOBAL_MAP||
       player->party.people.GetSize()!=serverPlayer->party.people.GetSize())return false;
    for(int i=0;i<player->party.people.GetSize();++i)
        if(player->party.people[i].nID!=serverPlayer->party.people[i].nID)return false;
    return true;
}

// Called only for this operation, with operationMutex held by UpdateOperationReadiness.
bool GlobalMapOperationReady() {
    auto& world=theWorldServer;
    if(!world.bMapLoaded||world.GetServerMode()!=SERV_MODE_LOCAL||
       world.GetRTID()!=globalMapOperation.worldId||world.curInterface!=globalMapOperation.origin||
       world.GetCurrentZoneName()!=operation.expectedZone.c_str()||
       world.GetCurrentZoneName()!=IWorldClient.GetCurrentZoneName()) {
        operation.error="global_map lost its original local world or zone";
        operation.state="error";operation.stage="failed";operation.inFlight=false;return false;
    }
    auto* client=theWorld.GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    auto* player=client?client->GetPlayer():nullptr;
    auto* server=serverPlayer?static_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific()):nullptr;
    if(!client||!player||!server||IWorldConnector.HasReq())return false;
    const bool open=operation.parameter=="open";
    const auto target=open?IFACE_GLOBAL_MAP:globalMapOperation.origin;
    return player->curInterface==target&&serverPlayer->curInterface==target&&
        client->GetStatus()==(open?CPlayerClient::CPS_CAMP:CPlayerClient::CPS_ACTIVE)&&
        server->GetStatus()==(open?CPlayerServer::PSS_CAMP:CPlayerServer::PSS_ACTIVE)&&
        GlobalMapScreen(target);
}

bool GlobalMapCommand(Job& job) {
    if (!AgentBackground::Enabled())
        throw std::runtime_error("global_map requires an agent --background session");
    const auto& value = job.request.at("params").at("open");
    if (!value.is_boolean()) throw std::runtime_error("open must be a boolean");
    const bool open = value.get<bool>();
    auto& world=theWorldServer;
    auto* client=theWorld.GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    auto* player=client?client->GetPlayer():nullptr;
    auto* server=serverPlayer?static_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific()):nullptr;
    if(!world.bMapLoaded||world.GetServerMode()!=SERV_MODE_LOCAL||!client||!player||!server)
        throw std::runtime_error("global_map requires a ready local mission or base");
    const auto iface = player->curInterface;
    const auto native = world.curInterface;
    if(native!=IFACE_MISSION&&native!=IFACE_BASE)
        throw std::runtime_error("global_map requires a mission or base; it cannot select a travel zone");
    const auto* zone = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    if (!zone || !theMap.GetAllodByName(zone->szAllodName)||
        world.GetCurrentZoneName()!=IWorldClient.GetCurrentZoneName())
        throw std::runtime_error("current zone has no global-map allod");
    const bool normal=iface==native&&serverPlayer->curInterface==native&&theWorld.IsConnected()&&
        client->GetStatus()==CPlayerClient::CPS_ACTIVE&&server->GetStatus()==CPlayerServer::PSS_ACTIVE&&
        GlobalMapScreen(native);
    const bool map=iface==IFACE_GLOBAL_MAP&&serverPlayer->curInterface==IFACE_GLOBAL_MAP&&
        client->GetStatus()==CPlayerClient::CPS_CAMP&&server->GetStatus()==CPlayerServer::PSS_CAMP&&
        GlobalMapScreen(IFACE_GLOBAL_MAP);
    if((!normal&&!map)||IWorldConnector.HasReq())
        throw std::runtime_error("wait for the local mission, base or preview map; close any modal screen");
    const auto id=BeginOperation(job.method,open?"open":"close");job.operationId=id;
    globalMapOperation={world.GetRTID(),native};
    OperationExecuting("global_map");
    if (open&&normal) {
        IWorldClient.GoCamp(IFACE_GLOBAL_MAP);
        if (client->GetStatus() != CPlayerClient::CPS_CAMP)
            throw std::runtime_error("the game refused to open the global map");
    } else if(!open&&map) {
        IWorldClient.FinishConstructing();
    }
    OperationApplied("global_map",true);
    job.response={{"ok",true},{"status","accepted"},{"operation_id",id},{"open",open},
                  {"path","client_to_server_interface"},{"truncated",false}};
    return true;
}
