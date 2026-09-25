#include "ModSpellTextures.h"
#include "BaseManager.h"
#include "TextureManager.h"
#include <map>
#include <mutex>
namespace SpellTextures {
__declspec(thread) int currentSpell = -1;
__declspec(thread) const char* currentCode = nullptr;
static std::vector<Binding> bindings;

struct EffectOwner { std::string code; CGameBase* server; ETypeIDs type; };
static std::map<int, EffectOwner> owners;
static std::mutex ownerMutex;

bool HasBindings() { return !bindings.empty(); }
void ResetWorld() {
    std::lock_guard<std::mutex> lock(ownerMutex);
    owners.clear();
}
static bool VisualType(ETypeIDs type) {
    return type == ETypeIDs(HPC_EFFECTPARTICLE) || type == ETypeIDs(HPC_EFFECTWALL) ||
        type == ETypeIDs(HPC_EFFECTARROW) || type == ETypeIDs(HPC_EFFECTLIGHTNING);
}
void RecordOwner(CBaseManager* manager, CGameBase* effect, const char* code) {
    if (!effect || !VisualType(effect->GetTypeID())) return;
    std::lock_guard<std::mutex> lock(ownerMutex);
    owners.erase(effect->GetID()); // also invalidate reused IDs for non-spell effects
    if (!code || !*code) return;
    const auto normalizedCode = Lower(code);
    bool configured = false;
    for (const auto& binding : bindings) if (binding.spell == normalizedCode) { configured = true; break; }
    if (!configured) return;
    // Prune using the authoritative server registry only on the server callback.
    if (owners.size() >= 256)
        for (auto it = owners.begin(); it != owners.end(); )
            if (manager->Find(it->first) != it->second.server) it = owners.erase(it); else ++it;
    if (owners.size() >= 4096) return;
    owners.emplace(effect->GetID(), EffectOwner{Lower(code), effect, effect->GetTypeID()});
}
static std::string ClientCode(CGameBase* effect) {
    if (currentCode && *currentCode) return currentCode; // embedded particle inherits its arrow
    std::lock_guard<std::mutex> lock(ownerMutex);
    auto found = owners.find(effect->GetID());
    if (found != owners.end() && found->second.type == effect->GetTypeID()) return found->second.code;
    // An arrow owns an embedded CEffectParticle; retain the arrow's outer scope.
    return currentCode ? currentCode : "";
}
ClientScope::ClientScope(CGameBase* effect) : code(ClientCode(effect)), scope(currentSpell, code.c_str()) {}

void Load(const std::string& directory){
 ResetWorld();bindings.clear();std::ifstream file(directory+"/config/spell-textures.tsv",std::ios::binary|std::ios::ate);
 if(!file)return;auto length=file.tellg();if(length<0||length>262144)return;
 std::string data(static_cast<size_t>(length),'\0'),error;file.seekg(0);if(!data.empty())file.read(&data[0],data.size());
 if(!file||!Parse(data,bindings,error))bindings.clear();
}
std::string Resolve(const char*name){
 if(!name||!currentCode||bindings.empty())return std::string();
 auto*row=FindBinding(bindings,currentCode,name);if(!row)return std::string();
 std::string target=row->replacement;for(char&c:target)if(c=='/')c='\\';
 auto*manager=theWorld.GetTextureManager();if(!manager||!manager->GetMMPData(target.c_str()))return std::string();
 target.resize(target.size()-4);return target;
}
}
