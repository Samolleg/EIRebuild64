// Invoked only by an explicit status/command request. Keep the existing
// readiness predicate intact; a failed connection is never reported as ready.
std::string ReadinessFailure() {
    auto& world=theWorldServer;auto* client=theWorld.GetPlayer();
    if(!world.bMapLoaded)return "world_not_loaded";
    if(world.GetServerMode()!=SERV_MODE_LOCAL)return "not_a_local_server";
    if(!theWorld.IsConnected())return "client_not_connected";
    if(!client)return "client_player_missing";
    if(IWorldConnector.HasReq())return "world_transition_pending";
    if(client->GetStatus()!=CPlayerClient::CPS_ACTIVE && client->GetStatus()!=CPlayerClient::CPS_CAMP)
        return "client_handshake_pending status="+std::to_string(int(client->GetStatus()));
    auto* player=client->GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    if(!player||!serverPlayer)return "party_player_missing";
    if(player->curInterface==IFACE_NONE || player->curInterface!=serverPlayer->curInterface || world.curInterface!=serverPlayer->curInterface)
        return "client_server_interfaces_differ";
    if(world.GetCurrentZoneName()!=operation.expectedZone.c_str())return "unexpected_zone";
    auto* server=dynamic_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific());
    if(!server || (server->GetStatus()!=CPlayerServer::PSS_ACTIVE && server->GetStatus()!=CPlayerServer::PSS_CAMP))
        return "server_handshake_pending";
    if(player->curInterface==IFACE_MISSION || player->curInterface==IFACE_BASE) {
        if(player->party.people.GetSize()!=serverPlayer->party.people.GetSize())return "party_sizes_differ";
        for(int i=0;i<player->party.people.GetSize();++i) {
            const int id=player->party.people[i].nID;
            const auto suffix=" index="+std::to_string(i)+" id="+std::to_string(id);
            if(id!=serverPlayer->party.people[i].nID)return "party_member_ids_differ"+suffix;
            auto* unit=world.GetUnit(id);auto* visible=theWorld.GetUnit(id);
            if(!unit)return "party_member_missing_in_mission"+suffix;
            if(unit->IsInvalid()||unit->IsUnreferenced())return "party_member_invalid_in_mission"+suffix;
            if(unit->IsDead())continue;
            if(!visible || visible->IsInvalid() || visible->IsUnreferenced())return "party_member_missing_on_client"+suffix;
            if(!Squad(unit,serverPlayer) || unit->GetOwner()!=serverPlayer || visible->GetOwner()!=player)
                return "party_member_ownership_not_synchronized"+suffix;
            auto* specific=unit->GetUnitSpecific();
            if(!specific || specific->WhoAmI()!=HPC_UNITSERVERSPECIFIC)return "party_member_has_no_server_state"+suffix;
        }
    }
    return {};
}
bool ExpireOperationReadiness() {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(!operation.inFlight || !operation.applied || operation.executing || !operation.error.empty())return false;
    operation.readinessReason=ReadinessFailure();
    if(operation.readinessReason.empty() || Clock::now()-operation.appliedAt<std::chrono::seconds(30))return false;
    operation.error="client_readiness_timeout: "+operation.readinessReason;
    operation.state="error";operation.stage="client_readiness_timeout";operation.inFlight=false;
    return true;
}
