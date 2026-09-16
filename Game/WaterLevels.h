#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "SaveLoader.h"
/////////////////////////////////////////////////////////////////////////////////////
class CTerrain;
class CSave;
/////////////////////////////////////////////////////////////////////////////////////
// класс для описания высот различных объектов террейна, таких как озера итп
class CWaterLevels
{
    struct SLevel
    {
        int nObjectID;
        int nSegments;
        float fpH;
        float fpDH;

        void Load(CSave &file) { file.Read(this, sizeof(*this)); }
        void Store(CSave &file) { file.Write(this, sizeof(*this)); }
    };
    CArray<SLevel> levels;

public:
    void Clear();
    void SetWaterLevel(int nObjectID, float fpFinalH, int nTime);
    bool Segment(); // return true if something changed
    void UpdateTo(CSave &pkt);
    void UpdateFrom(CSave &pkt, CTerrain *pTerrain);
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
