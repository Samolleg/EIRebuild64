#pragma once
class CMapObject;
class CScript;
namespace ModArenaVisual {
void Arm(CMapObject* carrier,float time,CScript* owner);
void Draw();
void Reset() noexcept;
}
