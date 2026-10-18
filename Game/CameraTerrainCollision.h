#pragma once
class CCamera;
class CWorldClient;
namespace CameraTerrainCollision {
void Reset();
void BeginFrame(bool active, DWORD quantum, float coefficient);
void AdvanceCamera(CCamera* camera, float nativeStep);
void SaveCampState(CWorldClient* world, CCamera* camera);
void RestoreCampState(CWorldClient* world, CCamera* camera);
void DiscardCampState();
void GuardEye(CWorldClient* world, CCamera* camera);
void ConstrainOrbit(CCamera* camera);
void ConstrainExtendedArm(CCamera* camera);
}
