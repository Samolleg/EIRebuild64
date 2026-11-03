#pragma once

class CWorldServer;
class CGSVariableSet;

namespace ZoneIce {
// A non-null arriving party is supplied only by the real zone-entry loader.
// Saved worlds use their own party and never evaluate the deadline.
bool Load(CWorldServer& world, CGSVariableSet* arriving = nullptr);
void Sync();
void Freeze();
void CopyState(CGSVariableSet& destination, CGSVariableSet& source);
}
