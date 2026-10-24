#pragma once
#include "SaveSchema.h"
/////////////////////////////////////////////////////////////////////////////////////
#include "SaveLoader.h"
/////////////////////////////////////////////////////////////////////////////////////
class CTerrain;
class CSave;
/////////////////////////////////////////////////////////////////////////////////////
// класс для описания высот различных объектов террейна, таких как озера итп
class CWaterLevels
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
private:
    struct SLevel
    {
        int nObjectID;
        int nSegments;
        float fpH;
        float fpDH;

        void Load(CSave &file) { file.Read(nObjectID); file.Read(nSegments); file.Read(fpH); file.Read(fpDH); }
        void Store(CSave &file) { file.Write(nObjectID); file.Write(nSegments); file.Write(fpH); file.Write(fpDH); }
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
