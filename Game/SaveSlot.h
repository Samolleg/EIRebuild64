#pragma once
#include "GameSave.h"
#include "DynamicSave.h"
#include <memory>

class CWorldServer;
class CGlobalHat;
class CParty;
class CCamera;
class CAssortmentGenerator;
struct SSaveSlotDesc;
class CDXGISurface;
class CGSVariableSet;

namespace SaveSchema {
// A single publication boundary for the scenario and all visited zone documents.
// The envelope owns the exact bytes read; no second read during adoption.
class Slot
{
public:
    static bool Save(const char *name);
    static bool Load(const char *name);
    static void NewGame();
    static void Shutdown();
    static void StoreWorld(CSave &, CWorldServer &);
    static bool LoadWorld(CSave &, CParty* arriving = nullptr);
    static void CopyParty(CParty &, const CParty &);
    static void StoreParty(CSave &, const CParty &);
    static void LoadParty(CSave &, CParty &);
    static bool StoreZone(const char *name);
    static bool LoadZone(const char *name, CParty& arriving);
    static bool HasZone(const char *name);
    static CString Directory();
    static void RecoverHistory(const char *name);
    static void SetPresentation(const SSaveSlotDesc &, CDXGISurface *);
    static bool Presentation(const char *name, SSaveSlotDesc &, Bytes *shot = nullptr);
    static void ApplyCamera();
private:
    struct Document;
    struct State;
    struct PartyOwner;
    static std::unique_ptr<State> active;
    static std::map<std::string, Bytes> visited;
    static Bytes presentation, screenshot;
    static void Generator(Writer &, const CAssortmentGenerator &);
    static void Generator(Reader &, CAssortmentGenerator &);
    static void Scenario(Writer &, const CGlobalHat &);
    static void Scenario(Reader &, CGlobalHat &);
    static void World(Writer &, const CWorldServer &);
    static void World(Reader &, CWorldServer &);
    static void BindWorld(Document &, std::shared_ptr<CWorldServer>, bool zoneOnly = false);
    static void BindParty(Document &, CParty &, std::shared_ptr<void>);
    static void Describe(Writer &, const Document &);
    static void Describe(Reader &, Document &);
    static Bytes Encode(Document &, const std::function<void(Writer &)> &);
    static void Decode(Document &, const std::function<void(Reader &)> &);
    static Document ReadDocument(const Bytes &);
    static void PrepareWorld(Document &);
    static void FinalizeWorld(Document &, CGSVariableSet* arriving = nullptr);
    static void PrepareScenario(Document &);
    static void Commit(std::unique_ptr<State>);
    static void CameraAdopt(const CCamera &);
};
}
