#pragma once

class CWorldServer;
class CGSVariableSet;

namespace ModAnimationIdSave {
bool PrepareSave(CWorldServer &world, CGSVariableSet &partyVars);
void AfterSuccessfulLoad(CWorldServer &world);
}
