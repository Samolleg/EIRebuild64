#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <set>
class CGameBase;
class CBaseManager;

// A data contract, not an enable/disable switch. ASCII names avoid code-page
// ambiguity between the editor's UTF-8 export and the engine's ANSI lookup.
namespace SpellTextures {
struct Binding { std::string spell, original, replacement; };
inline std::string Lower(std::string s) {
    for (char& c : s) { if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; if (c == '\\') c = '/'; }
    return s;
}
inline bool Name(const std::string& s, bool texture) {
    if (s.empty() || s.size() > 120 || s.front() == '/' || s.back() == '/' || s.find("..") != std::string::npos) return false;
    for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || (texture && (c == '.' || c == '/')))) return false;
    return !texture || (s.size() > 4 && s.substr(s.size()-4) == ".mmp");
}
inline bool Parse(const std::string& data, std::vector<Binding>& output, std::string& error) {
    output.clear(); error.clear();
    if (data.size() > 262144) { error = "file too large"; return false; }
    std::istringstream input(data); std::string line;
    auto next = [&]() { if (!std::getline(input, line)) return false; if (!line.empty() && line.back() == '\r') line.pop_back(); return true; };
    if (!next() || line != "EISPELLTEXTURES1") { error = "unsupported header"; return false; }
    std::vector<Binding> parsed; std::set<std::string> keys;
    while (next()) {
        if (line.empty()) continue;
        const auto a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a+1);
        if (a == std::string::npos || b == std::string::npos || line.find('\t', b+1) != std::string::npos) { error = "expected three fields"; return false; }
        Binding row; row.spell = Lower(line.substr(0,a)); row.original = Lower(line.substr(a+1,b-a-1)); row.replacement = Lower(line.substr(b+1));
        if (!Name(row.spell,false) || !Name(row.original,true) || !Name(row.replacement,true) || row.original == row.replacement || !keys.insert(row.spell+"\t"+row.original).second || parsed.size() >= 512) {
            error = "invalid or duplicate binding"; return false;
        }
        parsed.push_back(row);
    }
    output.swap(parsed); return true;
}
inline Binding* FindBinding(std::vector<Binding>& rows, const char* code, const char* name) {
    if (!code || !*code || !name) return nullptr;
    const auto spell = Lower(code); auto original = Lower(name);
    if (original.size() < 4 || original.substr(original.size()-4) != ".mmp") original += ".mmp";
    for (auto& row : rows) if (row.spell == spell && row.original == original) return &row;
    return nullptr;
}
extern __declspec(thread) int currentSpell;
extern __declspec(thread) const char* currentCode;
inline const char* NativeCode(int id) {
    // Persistent CSpellEffect stores only the native effect ID, not the item
    // prototype index. These are the original engine's code-to-ID names.
    static const char* codes[] = { "arrow", "lightning", "acid_ray", "fireball", "inv_lit", "acid_column", "firewall", "litnwall", "acid_fog", "prot_fire", "prot_electro", "prot_acid", "eagle_sight", "infravision", "detect_life", "invisibility", "silence", "lichdom", "fireworks", "stench", "clairvoyence", "possession", "shapechange", "vision_fog", "healing", "stun", "antimagic", "link", "teleport", "strength", "weak", "regeneration", "feeblemind", "speed", "slow", "comprehend", "charm", "enlarge", "shrink", "campfire", "rick_magic", "curse_magic" };
    return id >= 0 && id < int(sizeof(codes)/sizeof(codes[0])) ? codes[id] : nullptr;
}
struct Scope {
    int previous;
    const char* previousCode;
    explicit Scope(int id, const char* code = nullptr) : previous(currentSpell), previousCode(currentCode) { currentSpell = id; currentCode = code ? code : NativeCode(id); }
    ~Scope() { currentSpell = previous; currentCode = previousCode; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
struct ClientScope {
    std::string code;
    Scope scope;
    explicit ClientScope(CGameBase* effect);
};
void Load(const std::string& directory);
void RecordOwner(CBaseManager*,CGameBase*,const char*);
std::string Resolve(const char* name);
void ResetWorld();
bool HasBindings();
}
