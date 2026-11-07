// Included once inside LiveObserver's command implementation. No frame polling outside a request.
bool GlobalMapCommand(Job& job, CPlayerClient* client) {
    if (!AgentBackground::Enabled())
        throw std::runtime_error("global_map requires an agent --background session");
    const auto& value = job.request.at("params").at("open");
    if (!value.is_boolean()) throw std::runtime_error("open must be a boolean");
    const bool open = value.get<bool>();
    const auto iface = client->GetPlayer()->curInterface;
    const auto native = theWorldServer.curInterface;
    const auto* zone = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    if (!zone || !theMap.GetAllodByName(zone->szAllodName))
        throw std::runtime_error("current zone has no global-map allod");
    const bool map = iface == IFACE_GLOBAL_MAP;
    const bool normal = iface == native && (native == IFACE_MISSION || native == IFACE_BASE);
    const char* screen = theInterface.GetCurrentScreen() ? theInterface.GetCurrentScreen()->GetTutorialScreenName() : "";
    const bool mapScreen = screen && (!std::strcmp(screen, "global_map") || !std::strcmp(screen, "global_map_nocamp"));
    if ((open ? map && mapScreen : normal) && theInterface.GetScreensNum() == 1) {
        job.response = {{"ok",true},{"status","done"},{"open",open},
                        {"path","client_to_server_interface"},{"truncated",false}};
        return true;
    }
    if (job.phase) return false; // Await the ordinary player/screen acknowledgement, without resending.
    if (theInterface.GetScreensNum() != 1 || IWorldConnector.HasReq())
        throw std::runtime_error("close the modal screen or wait for the current transition");
    if (open) {
        if (!normal || client->GetStatus() != CPlayerClient::CPS_ACTIVE)
            throw std::runtime_error("open requires the active mission or base screen");
        IWorldClient.GoCamp(IFACE_GLOBAL_MAP);
        if (client->GetStatus() != CPlayerClient::CPS_CAMP)
            throw std::runtime_error("the game refused to open the global map");
    } else {
        if (!map || !mapScreen || (native != IFACE_MISSION && native != IFACE_BASE) ||
            client->GetStatus() != CPlayerClient::CPS_CAMP)
            throw std::runtime_error("close requires a global map opened over a mission or base");
        IWorldClient.FinishConstructing();
    }
    job.phase = 1;
    return false;
}
