#pragma once
class CCamera;
class CWorldClient;
namespace CameraTerrainCollision {
void Reset();
void GuardEye(CWorldClient* world, CCamera* camera);
void ConstrainOrbit(CCamera* camera);
void ConstrainExtendedArm(CCamera* camera);
}
