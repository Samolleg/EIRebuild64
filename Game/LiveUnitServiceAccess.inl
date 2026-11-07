// Only the MCP service-teleport path may cancel this transient animation owner.
// A gameplay stun is deliberately retained. Nothing here changes normal AI.
struct LiveUnitServiceAccess {
    static bool BeforeTeleport(CUnitServer& unit) {
        const bool wasSuffering=unit.bL1PlayingSuffer;
        // InitializeUnit replaces the finite hurt clip with a cyclic idle.
        // Leaving its old owner set would wait for that cycle forever.
        if(wasSuffering) {
            unit.bL1PlayingSuffer=false;unit.sufferAnimationMem.Clear();unit.level0Animation.Clear();
        }
        unit.nL1SufferDirection=-1;
        return wasSuffering;
    }
};
