#pragma once
class CWorldClient;
class CPlot3D;
namespace ModWorldBackground {
void BeforeDraw(CWorldClient&,const CPlot3D& logicalCamera);
void DrawTransparent();
void Unload();
}
bool InspectWorldBackgroundCommandLine(int &result);
