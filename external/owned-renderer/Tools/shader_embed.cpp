// Build step: compiles the renderer's fixed shader program list (every
// feature's append_*_shader_programs, from the same production sources as the
// DLL) and writes the table the DLL embeds as the EID3D11_SHADERS resource
// (Source/shader_cache.h), plus the one-line .rc that names it.
//
//   shader_embed <output.bin> [--preset-cache] [--dxil-sources <directory>] [--prepare-keys]
//
// (--preset-worker <list> <counter> is this tool's own child process for the
// --preset-cache compiles; see run_preset_workers.)
//
// Optional EID3D11_SHADER_STORE=<folder>: compiled programs shared across
// worktrees and commits (see store_file below), keyed by the preprocessed
// program, so an edit recompiles only the programs whose code it reaches.
//
// The previous output is the incremental cache: a program whose key it
// already holds is reused, so only edited programs are compiled again; stale
// entries are simply not written. A compile error fails the build.
//
// --dxil-sources writes the same program list for the x64 host's DXIL table
// (Host\host.mak compiles it with the SDK's dxc.exe, one .dxil a program):
// each distinct composed source once as src\<source hash>.hlsl, an nmake
// fragment dxil.mak with one rule a program, and dxil.rc naming every .dxil
// as an RCDATA resource. A file is rewritten only when its content changed,
// so nmake recompiles only programs whose source changed.
#include "renderer.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_triangle_shader.h"
#endif
#include "dxil_table.h"

#include <d3d11shader.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace owned {
// Proxy-side symbols the renderer sources reference; this tool never draws.
void window_sampler_present() {}
bool proxy_completed_visual_surface(IDirectDrawSurface7*, std::shared_ptr<const OwnedVisualSurface>&) noexcept {
    return false;
}
void proxy_retire_camp_texture(std::uint64_t, std::uint64_t) noexcept {}
bool proxy_publish_camp_texture(IDirectDrawSurface7*, const PreparedCampIcon&,
    std::shared_ptr<const OwnedVisualSurface>&) noexcept { return false; }
HRESULT proxy_fade_modal_backdrop(IDirectDrawSurface7*) noexcept { return E_NOTIMPL; }
HRESULT proxy_queue_modal_thumbnail(IDirectDrawSurface7*, unsigned, unsigned,
    std::function<HRESULT(std::uint16_t*)>&) noexcept { return E_NOTIMPL; }
} // namespace owned

using namespace owned;

namespace {

// Writes text to path unless the file already holds exactly that (keeps its
// timestamp for nmake).
bool write_if_changed(const std::string& path, const std::string& text) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (old == text) return true;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// A define name or value as a dxc argument: plain identifier characters only.
bool plain_argument(const char* text) {
    if (!text) return true;
    for (const char* p = text; *p; ++p)
        if (!std::isalnum(static_cast<unsigned char>(*p)) && !std::strchr("_.-", *p)) return false;
    return true;
}

// A program's define value ("" without one), null when it lacks the define.
const char* define_value(const ShaderProgram& program, const char* name) {
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define)
        if (!std::strcmp(define->Name, name)) return define->Definition ? define->Definition : "";
    return nullptr;
}

// Removes files matching pattern in directory that are not in wanted (names).
void remove_stale(const std::string& directory, const char* pattern, const std::set<std::string>& wanted) {
    WIN32_FIND_DATAA found{};
    const HANDLE search = FindFirstFileA((directory + "\\" + pattern).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !wanted.count(found.cFileName))
            DeleteFileA((directory + "\\" + found.cFileName).c_str());
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

// --dxil-sources: see the file comment. dxc's output and log names are the
// program's dxil_name; a rejected program leaves an empty .dxil (the build
// continues; Host\build.cmd lists them) so nmake does not retry it until its
// source changes.
int write_dxil_sources(const std::string& directory, const std::vector<ShaderProgram>& programs) {
    const std::string sources = directory + "\\src", logs = directory + "\\log";
    CreateDirectoryA(directory.c_str(), nullptr);
    CreateDirectoryA(sources.c_str(), nullptr);
    CreateDirectoryA(logs.c_str(), nullptr);
    std::map<std::string, std::string> source_files;  // source hash -> text
    std::set<std::string> names, source_names, dxil_names, log_names;
    // nmake runs one command at a time: Host\build.cmd builds the parts
    // dxil_part_0..N-1 with one nmake each in parallel before the host link.
    constexpr unsigned kParts = 16;
    std::ostringstream rules, list, rc, parts[kParts];
    rc << "// Generated by shader_embed --dxil-sources; one RCDATA a program (Source/dxil_table.h).\n";
    list << "DXIL_PROGRAMS =";
    for (unsigned part = 0; part < kParts; ++part) parts[part] << "DXIL_PART_" << part << " =";
    std::size_t rule_count = 0;  // spreads the programs over the parts
    for (const ShaderProgram& program : programs) {
        char profile[7];
        if (!dxil_profile(program.profile, profile)) {
            std::fprintf(stderr, "shader_embed: %s: profile %s has no SM 6.6 equivalent\n", program.entry, program.profile);
            return 1;
        }
        if (program.flags & ~UINT(D3DCOMPILE_OPTIMIZATION_LEVEL3)) {
            std::fprintf(stderr, "shader_embed: %s: compile flags 0x%x have no dxc mapping here\n", program.entry, program.flags);
            return 1;
        }
        const std::string name = dxil_name(program);
        if (!names.insert(name).second) continue;  // a program in two groups
        shader_cache_detail::Hasher h;
        h.bytes(program.source.data(), program.source.size());
        char hash[40];
        std::snprintf(hash, sizeof(hash), "%016llX%016llX", static_cast<unsigned long long>(h.a),
                      static_cast<unsigned long long>(h.b));
        source_files.emplace(hash, std::string(program.source));
        source_names.insert(std::string(hash) + ".hlsl");
        dxil_names.insert(name + ".dxil");
        log_names.insert(name + ".txt");
        std::string defines;
        for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
            if (!plain_argument(define->Name) || !plain_argument(define->Definition)) {
                std::fprintf(stderr, "shader_embed: %s: define %s is not a plain dxc argument\n", program.entry, define->Name);
                return 1;
            }
            defines += std::string(" -D ") + define->Name + (define->Definition ? std::string("=") + define->Definition : "");
        }
        if (!plain_argument(program.entry)) {
            std::fprintf(stderr, "shader_embed: entry %s is not a plain dxc argument\n", program.entry);
            return 1;
        }
        // The native shader's own programs also get an EID3D12_HAZARDS variant:
        // the GPU report (native_shader.h, EI_RENDERER_D3D12_HAZARDS) compiles
        // only there. Ground cover, critters and fog prepend the same source,
        // but their entry points reach no report call (their DXIL declared no
        // report UAV), so they have the production variant only.
        // Of the material instance programs (NATIVE_DOMAIN) only the check ones:
        // the hazards build draws the others without the report
        // (d3d12_frame.cpp program_dxil_name); the check programs count
        // disagreeing pixels in it (report id 200 + the wrong bits).
        // Nor the Ray Reconstruction guide twins (NATIVE_RR_GUIDES): the
        // hazards build draws them without the report as well.
        const bool hazards_variant = program.source == owned_native_shader_source() && !define_value(program, "NATIVE_RR_GUIDES") &&
            (!(program.defines && program.defines->Name && !std::strcmp(program.defines->Name, "NATIVE_DOMAIN")) ||
             define_value(program, "NATIVE_DOMAIN_CHECK"));
        for (int variant = 0; variant < (hazards_variant ? 2 : 1); ++variant) {
            const std::string out = variant ? dxil_name(program, true) : name;
            if (variant) {
                dxil_names.insert(out + ".dxil");
                log_names.insert(out + ".txt");
            }
            list << " \\\n $(DXIL)\\" << out << ".dxil";
            parts[rule_count++ % kParts] << " \\\n $(DXIL)\\" << out << ".dxil";
            // -HV 2018: the language rules FXC and DXC 1.6 used (HLSL 2021,
            // DXC 1.7+'s default, rejects the shared HLSL's vector ternaries).
            // EID3D12: the shared HLSL's D3D12-only parts (bindless reads); FXC
            // never gets it, so the DXBC table is unchanged.
            const std::string arguments = std::string(" -nologo -HV 2018 -T ") + profile + " -E " + program.entry +
                ((program.flags & D3DCOMPILE_OPTIMIZATION_LEVEL3) ? " -O3" : "") + " -D EID3D12=1" +
                (variant ? " -D EID3D12_HAZARDS=1" : "") + defines;
            // A program that includes the NVIDIA RTXDI library's headers (its
            // RTXDI_SDK define): they are read in place from the SDK folder at
            // build time (Host/build.cmd RTXDI_SDK), never from this tree.
            const bool rtxdi = define_value(program, "RTXDI_SDK") != nullptr;
            const std::string compile = "\"$(DXC)\"" + arguments + (rtxdi ? " -I \"$(RTXDI_SDK)\\Include\"" : "") +
                " -Qstrip_debug -Qstrip_reflect -Fo $@ $(DXIL)\\src\\" + hash + ".hlsl > $(DXIL)\\log\\" + out + ".txt 2>&1";
            rules << "$(DXIL)\\" << out << ".dxil: $(DXIL)\\src\\" << hash << ".hlsl\n";
            if (rtxdi) {
                // Its headers are outside the source text: no store entry.
                rules << "\t@" << compile << " || type nul > $@\n";
            } else {
                // DXIL_STORE (Host/build.cmd: a folder per dxc version): the
                // compiled program keyed by its exact source and arguments, so
                // a fresh worktree copies what another build compiled. Only a
                // program dxc accepted is stored; the copy goes through a
                // temporary name so a concurrent build never reads half a file.
                shader_cache_detail::Hasher k;
                k.text("dxil store 1");
                k.bytes(program.source.data(), program.source.size());
                k.text(arguments.c_str());
                char key[40];
                std::snprintf(key, sizeof(key), "%016llX%016llX", static_cast<unsigned long long>(k.a),
                              static_cast<unsigned long long>(k.b));
                const std::string stored = std::string("\"$(DXIL_STORE)\\") + key + ".dxil\"";
                const std::string temporary = std::string("\"$(DXIL_STORE)\\") + key + "." + out + ".tmp\"";
                // (copy /b stored+nul: a combined copy takes the current time, so
                // nmake sees the .dxil newer than its source.)
                rules << "\t@if exist " << stored << " (copy /y /b " << stored << "+nul $@ >nul) else (" << compile << " && (copy /y $@ "
                      << temporary << " >nul 2>&1 && move /y " << temporary << " " << stored << " >nul 2>&1 & ver >nul) || type nul > $@)\n";
            }
            rc << out << " RCDATA \"" << out << ".dxil\"\n";
        }
    }
    list << "\n\n";
    for (unsigned part = 0; part < kParts; ++part)
        list << parts[part].str() << "\n\ndxil_part_" << part << ": $(DXIL_PART_" << part << ")\n\n";
    for (const auto& [hash, text] : source_files)
        if (!write_if_changed(sources + "\\" + hash + ".hlsl", text)) {
            std::fprintf(stderr, "shader_embed: cannot write %s\\%s.hlsl\n", sources.c_str(), hash.c_str());
            return 1;
        }
    if (!write_if_changed(directory + "\\dxil.mak",
                          "# Generated by shader_embed --dxil-sources; included by Host\\host.mak.\n" + list.str() + rules.str()) ||
        !write_if_changed(directory + "\\dxil.rc", rc.str())) {
        std::fprintf(stderr, "shader_embed: cannot write %s\\dxil.mak or dxil.rc\n", directory.c_str());
        return 1;
    }
    remove_stale(sources, "*.hlsl", source_names);
    remove_stale(directory, "X*.dxil", dxil_names);
    remove_stale(logs, "X*.txt", log_names);
    std::printf("shader_embed: %zu DXIL programs from %zu sources in %s\n", dxil_names.size(), source_files.size(),
                directory.c_str());
    return 0;
}

// EID3D11_SHADER_STORE (optional): a folder shared by every worktree's build,
// one DXBC file per program key. The previous output and the preset folder
// hold only the last commit's programs, so switching commits recompiled
// everything the other commit had; the store keeps both. Never pruned here:
// delete the folder to reclaim its space.
std::string g_store;
ShaderKey g_compiler_key;
// Hash the DLL actually providing D3DCompile, including servicing updates.
bool identify_compiler() {
    // &D3DCompile can name the EXE's import thunk, so resolve the export
    // before finding its module. Hashing the thunk's module hashes this tool
    // and invalidates unchanged shaders whenever its C++ code changes.
    const HMODULE compiler = GetModuleHandleW(D3DCOMPILER_DLL_W);
    const FARPROC compile = compiler ? GetProcAddress(compiler, "D3DCompile") : nullptr;
    HMODULE module = nullptr;
    if (!compile || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(compile), &module)) return false;
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    shader_cache_detail::Hasher h;
    char buffer[65536];
    while (in.read(buffer, sizeof(buffer)) || in.gcount()) h.bytes(buffer, static_cast<std::size_t>(in.gcount()));
    if (!in.eof()) return false;
    g_compiler_key = {h.a, h.b};
    return true;
}
std::string store_file(const ShaderKey& key) {
    std::string narrow;
    for (const wchar_t ch : native_spec_cache_file_name(key)) narrow += static_cast<char>(ch); // hex digits and ".dxbc"
    return g_store + "\\" + narrow;
}
bool store_read(const ShaderKey& key, std::string& code) {
    if (g_store.empty()) return false;
    std::ifstream in(store_file(key), std::ios::binary);
    if (!in) return false;
    code.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !code.empty();
}
// Written through a temporary name, so a concurrent build never reads a
// partial file; a failed write only costs a later recompile.
void store_write(const ShaderKey& key, const void* code, std::size_t size) {
    if (g_store.empty()) return;
    const std::string path = store_file(key), temporary = path + "." + std::to_string(GetCurrentThreadId()) + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(code), static_cast<std::streamsize>(size));
        if (!out) { out.close(); DeleteFileA(temporary.c_str()); return; }
    }
    if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileA(temporary.c_str());
}
// The store's key: the program as FXC sees it after preprocessing (its
// defines applied, comments and #if-excluded code gone), its entry, profile
// and flags, plus the loaded compiler DLL fingerprint. Includes and defines
// are resolved by D3DPreprocess; compilation uses the same null include handler.
// The table key (shader_key) hashes the composed source, which
// every program built from the native source shares, so before this any
// edit there recompiled all of them (~186 programs; a terrain edit reached
// the ground cover, fog and critter programs too). Preprocessing a program
// takes milliseconds; compiling a terrain variant takes tens of seconds.
// Hash tokens rather than FXC's whitespace and source-location presentation.
// Preserve token boundaries (a + +b differs from a++b), literal contents,
// and expanded __LINE__ values, which can affect actual shader behaviour.
void hash_preprocessed(shader_cache_detail::Hasher& h, const char* p, const char* end) {
    bool line_start = true;
    const auto identifier = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    while (p < end && *p) {
        if (std::isspace(static_cast<unsigned char>(*p))) {
            if (*p == '\n') line_start = true;
            ++p;
            continue;
        }
        if (line_start && *p == '#') {
            const char* directive = p + 1;
            while (directive < end && (*directive == ' ' || *directive == '\t')) ++directive;
            if ((end - directive >= 4 && !std::strncmp(directive, "line", 4) &&
                 (directive + 4 == end || std::isspace(static_cast<unsigned char>(directive[4])))) ||
                (directive < end && std::isdigit(static_cast<unsigned char>(*directive)))) {
                while (p < end && *p && *p != '\n') ++p;
                continue;
            }
        }
        line_start = false;
        const char* begin = p++;
        if (*begin == '"' || *begin == '\'') {
            while (p < end && *p) {
                if (*p == '\\' && p + 1 < end) { p += 2; continue; }
                if (*p++ == *begin) break;
            }
        } else if (std::isdigit(static_cast<unsigned char>(*begin)) ||
                   (*begin == '.' && p < end && std::isdigit(static_cast<unsigned char>(*p)))) {
            // Preprocessing numbers include hexadecimal and exponent signs.
            while (p < end && (identifier(*p) || *p == '.' ||
                   ((*p == '+' || *p == '-') && std::strchr("eEpP", p[-1])))) ++p;
        } else if (identifier(*begin)) {
            while (p < end && identifier(*p)) ++p;
        } else {
            static const char* const operators[] = {"<<=", ">>=", "...", "++", "--", "->", "&&", "||",
                "<=", ">=", "==", "!=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "::", "##"};
            for (const char* op : operators) {
                const std::size_t size = std::strlen(op);
                if (std::size_t(end - begin) >= size && !std::strncmp(begin, op, size)) {
                    p = begin + size;
                    break;
                }
            }
        }
        h.bytes(begin, std::size_t(p - begin));
        h.text(nullptr); // token separator
    }
}
bool compile_key(const ShaderProgram& program, ShaderKey& key, ShaderKey& legacy_key) {
    Microsoft::WRL::ComPtr<ID3DBlob> text, messages;
    if (FAILED(D3DPreprocess(program.source.data(), program.source.size(), program.name, program.defines, nullptr, &text,
                             &messages)))
        return false;
    const char* p = static_cast<const char*>(text->GetBufferPointer());
    const char* const end = p + text->GetBufferSize();
    shader_cache_detail::Hasher h;
    // Migration only: remove after the first complete store migration.
    // Reproduce v3 exactly, including its insignificant whitespace.
    shader_cache_detail::Hasher legacy;
    legacy.text("preprocessed 3");
    legacy.bytes(&g_compiler_key.a, sizeof(g_compiler_key.a));
    legacy.bytes(&g_compiler_key.b, sizeof(g_compiler_key.b));
    for (const char* line = p; line < end;) {
        const char* next = static_cast<const char*>(std::memchr(line, '\n', std::size_t(end - line)));
        next = next ? next + 1 : end;
        if (!(next - line >= 5 && !std::strncmp(line, "#line", 5))) legacy.bytes(line, std::size_t(next - line));
        line = next;
    }
    legacy.text(program.entry);
    legacy.text(program.profile);
    legacy.bytes(&program.flags, sizeof(program.flags));
    legacy_key = {legacy.a, legacy.b};
    h.text("preprocessed tokens 4");
    h.bytes(&g_compiler_key.a, sizeof(g_compiler_key.a));
    h.bytes(&g_compiler_key.b, sizeof(g_compiler_key.b));
    hash_preprocessed(h, p, end);
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
        h.text(define->Name);
        h.text(define->Definition);
    }
    h.text(nullptr); // end of macro fields
    h.text(program.entry);
    h.text(program.profile);
    h.bytes(&program.flags, sizeof(program.flags));
    key = {h.a, h.b};
    return true;
}
// "entry DEFINE=1 ..." for the compile-time list.
std::string program_label(const ShaderProgram& program) {
    std::string label = program.entry;
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
        label += ' ';
        label += define->Name;
        if (define->Definition && std::strcmp(define->Definition, "1")) label += std::string("=") + define->Definition;
    }
    return label;
}
// The shader-size gate (memory rule: slots and temps per variant are a merge
// gate): every native object pixel program's temps and instructions (DXBC
// reflection, fxc /O3) printed; a material instance program over its
// master's budget fails the build. Budgets: the domain's object program,
// its prepassed (NATIVE_SHADOW_MASK) program two temps lower.
int native_pixel_gate(const std::vector<ShaderProgram>& programs, const std::vector<std::pair<ShaderKey, std::string>>& table) {
    struct Budget { const char* name; UINT temps, instructions; };
    // NATIVE_DOMAIN 1..4 (renderer.h kNativeInstance*), without then with NATIVE_PROFILED.
    // Instructions +100 over the shader split's table (cand-1108): bugfix-1107's haze runs
    // fogHazeColorAt per pixel (sky-probe tap, chroma limit, lit-white fit),
    // +86 slots on every object program; temps unchanged. Hoisting it is a follow-up issue.
    // Instructions +140 more (bounce-seam-1108, director decision): the sun bounce reads the
    // world volume past the active map and blends the two over its last 8 m (nativeCaveBounce),
    // +117..+130 slots; temps unchanged; that work sits behind a dynamic branch.
    // Profiled unit +50 more (materials-1108): lamp-lit figures take the texture relief
    // normal in the lamp loop (~+95 slots, temps unchanged); director decision
    // after the bz14h camp A/B (fade only read plastic under lamps).
    static const Budget budgets[2][4] = {{{"unit", 30, 3740}, {"world", 40, 5840}, {"foliage", 40, 5740}, {"impostor", 43, 6640}},
                                         {{"unit", 40, 4490}, {"world", 42, 6240}, {"foliage", 44, 6040}, {"impostor", 45, 6940}}};
    int failed = 0;
    for (std::size_t i = 0; i < programs.size(); ++i) {
        const ShaderProgram& program = programs[i];
        const bool vertex = !std::strcmp(program.entry, "nativeVS") || !std::strcmp(program.entry, "nativePrepassVS");
        if ((std::strcmp(program.entry, "nativePS") && !vertex) || program.source != owned_native_shader_source() ||
            define_value(program, "NATIVE_DOMAIN_CHECK"))
            continue;
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        D3D11_SHADER_DESC desc{};
        if (FAILED(D3DReflect(table[i].second.data(), table[i].second.size(), IID_ID3D11ShaderReflection,
                              reinterpret_cast<void**>(reflection.GetAddressOf()))) || FAILED(reflection->GetDesc(&desc))) {
            std::fprintf(stderr, "shader_embed: gate: cannot reflect %s\n", program_label(program).c_str());
            ++failed;
            continue;
        }
        const char* domain = define_value(program, "NATIVE_DOMAIN");
        const bool masked = define_value(program, "NATIVE_SHADOW_MASK") != nullptr;
        const Budget* budget = nullptr;
        const char* profiled = define_value(program, "NATIVE_PROFILED");
        if (!vertex && domain && *domain >= '1' && *domain <= '4') budget = &budgets[profiled && !std::strcmp(profiled, "0") ? 0 : 1][*domain - '1'];
        // The crystal domains (5 world object, 6 unit; always profiled) keep their
        // base domain's profiled budget: the crystal shading costs instructions
        // (+184 world, +139 unit, crystals-1114), never temps.
        if (!vertex && domain && (*domain == '5' || *domain == '6')) budget = &budgets[1][*domain == '5' ? 1 : 0];
        const UINT temps = budget ? budget->temps - (masked ? 2u : 0u) : 0u;
        // The hard cap for every object pixel program (terrain is its own master):
        // 47 temps and up is the register cliff the RTX 3070 laptop fell off (2.7 ms).
        // Vertex programs are printed only (the instance vertex programs 56 / 41 temps).
        const bool capped = !vertex && !define_value(program, "NATIVE_TERRAIN_PS") && desc.TempRegisterCount >= 47;
        const bool over = capped || (budget && (desc.TempRegisterCount > temps || desc.InstructionCount > budget->instructions));
        std::printf("shader_embed: gate %-9s temps %3u instructions %5u %s%s\n", budget ? budget->name : vertex ? "vertex" : "general",
                    desc.TempRegisterCount, desc.InstructionCount, program_label(program).c_str(), over ? "  OVER BUDGET" : "");
        if (over) ++failed;
    }
    if (failed) std::fprintf(stderr, "shader_embed: %d native pixel program(s) over the size budget\n", failed);
    return failed ? 1 : 0;
}

// The logical cores this process may run on (build-lock.ps1 gives builds 80%).
unsigned worker_count() {
    DWORD_PTR process = 0, system = 0;
    unsigned count = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system))
        for (; process; process &= process - 1) ++count;
    return count ? count : (std::max)(1u, std::thread::hardware_concurrency());
}

// fn(i) for every i < count on up to threads threads.
template <class Fn> void parallel_for(std::size_t count, unsigned threads, Fn fn) {
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads && t < count; ++t)
        workers.emplace_back([&] { for (std::size_t i = next++; i < count; i = next++) fn(i); });
    for (auto& worker : workers) worker.join();
}

// --preset-cache: the shipped presets' specialised programs and their folder.
std::string preset_directory(const std::string& output) {
    return output.substr(0, output.find_last_of("\\/") + 1) + "eid3d11_shader_cache";
}
std::string preset_file(const std::string& directory, const ShaderProgram& program) {
    std::string narrow;
    for (const wchar_t ch : native_spec_cache_file_name(shader_key(program))) narrow += static_cast<char>(ch); // hex digits and ".dxbc"
    return directory + "\\" + narrow;
}
bool preset_programs(std::vector<NativeSpecialisedPrograms>& lists, std::vector<const ShaderProgram*>& special) {
    for (const std::uint32_t key : native_shipped_specialisation_keys()) {
        lists.emplace_back();
        if (!native_specialised_programs(key, lists.back())) {
            std::fprintf(stderr, "shader_embed: specialised list of key 0x%03x does not match its table\n", key);
            return false;
        }
    }
    for (const auto& list : lists) for (const auto& program : list.programs) special.push_back(&program);
    return true;
}

// FXC compiles a process may run at once: the four that always fit this
// 32-bit tool's 4 GB with the largest native pixel programs (twelve did not).
constexpr unsigned kPresetThreads = 4;

// Child process (--preset-worker <list> <counter>): compiles the specialised
// programs whose indices the list holds, taking the next one from the shared
// counter (a named mapping the parent made), into the preset folder and the store.
int preset_worker(const std::string& output, const std::string& list_path, const std::string& counter_name) {
    std::vector<NativeSpecialisedPrograms> lists;
    std::vector<const ShaderProgram*> special;
    if (!preset_programs(lists, special)) return 1;
    std::vector<std::size_t> indices;
    {
        std::ifstream in(list_path);
        for (std::size_t i; in >> i;) {
            if (i >= special.size()) { std::fprintf(stderr, "shader_embed: preset worker: bad index %zu\n", i); return 1; }
            indices.push_back(i);
        }
    }
    const HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, counter_name.c_str());
    volatile LONG* const counter = mapping ? static_cast<volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG))) : nullptr;
    if (!counter) { std::fprintf(stderr, "shader_embed: preset worker: no counter %s\n", counter_name.c_str()); return 1; }
    const std::string directory = preset_directory(output);
    std::atomic<bool> failed{false};
    std::atomic<long long> ticks{0};
    std::atomic<unsigned> compiled{0};
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < kPresetThreads; ++t)
        workers.emplace_back([&] {
            for (LONG n = InterlockedIncrement(counter) - 1; n >= 0 && std::size_t(n) < indices.size(); n = InterlockedIncrement(counter) - 1) {
                const ShaderProgram& program = *special[indices[std::size_t(n)]];
                const std::string wanted = preset_file(directory, program), temporary = wanted + ".tmp";
                LARGE_INTEGER start{}, stop{};
                QueryPerformanceCounter(&start);
                Microsoft::WRL::ComPtr<ID3DBlob> blob, messages;
                if (FAILED(D3DCompile(program.source.data(), program.source.size(), program.name, program.defines, nullptr,
                                      program.entry, program.profile, program.flags, 0, &blob, &messages))) {
                    std::fprintf(stderr, "shader_embed: %s: %s\n", program_label(program).c_str(),
                                 messages ? static_cast<const char*>(messages->GetBufferPointer()) : "compile failed");
                    failed = true;
                    continue;
                }
                QueryPerformanceCounter(&stop);
                ticks += stop.QuadPart - start.QuadPart;
                ++compiled;
                {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    out.write(static_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
                    if (!out) { std::fprintf(stderr, "shader_embed: cannot write %s\n", temporary.c_str()); failed = true; continue; }
                }
                if (!MoveFileExA(temporary.c_str(), wanted.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    std::fprintf(stderr, "shader_embed: cannot write %s\n", wanted.c_str());
                    failed = true;
                    continue;
                }
                ShaderKey key{}, legacy{};
                if (!g_store.empty() && compile_key(program, key, legacy)) store_write(key, blob->GetBufferPointer(), blob->GetBufferSize());
            }
        });
    for (auto& worker : workers) worker.join();
    std::printf("shader_embed: preset worker %lu: %u compiled, %.1f s of compiles (%u threads)\n", GetCurrentProcessId(),
                compiled.load(), double(ticks.load()) / double(frequency.QuadPart), kPresetThreads);
    UnmapViewOfFile(const_cast<LONG*>(counter));
    CloseHandle(mapping);
    return failed ? 1 : 0;
}

// Runs processes preset workers over compile (indices into the specialised
// list) and waits for them; nonzero when one failed.
int run_preset_workers(const std::string& output, const std::vector<std::size_t>& compile, unsigned processes) {
    const std::string list_path = output + ".preset-worker.txt";
    {
        std::ofstream out(list_path, std::ios::trunc);
        for (const std::size_t i : compile) out << i << '\n';
        if (!out) { std::fprintf(stderr, "shader_embed: cannot write %s\n", list_path.c_str()); return 1; }
    }
    const std::string counter_name = "Local\\eid3d11_shader_embed_" + std::to_string(GetCurrentProcessId());
    const HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG), counter_name.c_str());
    if (!mapping) { std::fprintf(stderr, "shader_embed: cannot create %s\n", counter_name.c_str()); return 1; }
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string command = std::string("\"") + exe + "\" \"" + output + "\" --preset-worker \"" + list_path + "\" " + counter_name;
    std::fflush(stdout);
    std::fflush(stderr);
    std::vector<HANDLE> children;
    int failed = 0;
    for (unsigned p = 0; p < processes; ++p) {
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION info{};
        std::vector<char> line(command.begin(), command.end());
        line.push_back('\0');
        if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) {
            std::fprintf(stderr, "shader_embed: cannot start a preset worker (error %lu)\n", GetLastError());
            failed = 1;
            break;
        }
        CloseHandle(info.hThread);
        children.push_back(info.hProcess);
    }
    for (const HANDLE child : children) {
        WaitForSingleObject(child, INFINITE);
        DWORD code = 1;
        if (!GetExitCodeProcess(child, &code) || code) failed = 1;
        CloseHandle(child);
    }
    CloseHandle(mapping);
    DeleteFileA(list_path.c_str());
    return failed;
}
} // namespace

int main(int argc, char** argv) {
    bool preset_cache = false, prepare_keys = false, usage = argc < 2;
    std::string dxil_directory, worker_list, worker_counter;
    for (int i = 2; i < argc && !usage; ++i) {
        if (!std::strcmp(argv[i], "--prepare-keys")) prepare_keys = true;
        else if (!std::strcmp(argv[i], "--preset-cache")) preset_cache = true;
        else if (!std::strcmp(argv[i], "--preset-worker") && i + 2 < argc) { worker_list = argv[++i]; worker_counter = argv[++i]; }
        else if (!std::strcmp(argv[i], "--dxil-sources") && i + 1 < argc) dxil_directory = argv[++i];
        else usage = true;
    }
    if (usage) {
        std::fprintf(stderr, "usage: shader_embed <output.bin> [--preset-cache] [--dxil-sources <directory>] [--prepare-keys]\n");
        return 2;
    }
    const std::string output = argv[1];
    if (const char* store = std::getenv("EID3D11_SHADER_STORE"); store && *store) {
        if (!identify_compiler()) {
            std::fprintf(stderr, "shader_embed: cannot identify shader compiler DLL\n");
            return 1;
        }
        g_store = store;
        CreateDirectoryA(g_store.c_str(), nullptr);
    }
    if (!worker_list.empty()) return preset_worker(output, worker_list, worker_counter);
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);

    std::vector<unsigned char> previous;
    {
        std::ifstream in(output, std::ios::binary);
        previous.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!validate_shader_table(previous.data(), previous.size())) previous.clear();
    // The previous table has no compiler identity. Only compiler-keyed store
    // entries can be reused when the store is enabled.
    if (!previous.empty() && g_store.empty()) use_embedded_shaders(previous.data(), previous.size());

    std::vector<ShaderProgram> programs;
    append_shader_groups(0xFFFFFFFFu, programs);
#ifdef EID3D11_EMBEDDED
    // The embedded renderer's native specialisations may differ from the
    // standalone DDraw cache (for example, the host triangle transform). Keep
    // their exact bytecode in the EXE resource table so it needs no sidecar.
    std::vector<NativeSpecialisedPrograms> embeddedSpecialisations;
    const auto keys = native_shipped_specialisation_keys();
    embeddedSpecialisations.reserve(keys.size());
    for (const std::uint32_t key : keys) {
        embeddedSpecialisations.emplace_back();
        auto& special = embeddedSpecialisations.back();
        if (!native_specialised_programs(key, special)) {
            std::fprintf(stderr, "shader_embed: specialised list of key 0x%03x does not match its table\n", key);
            return 1;
        }
        programs.insert(programs.end(), special.programs.begin(), special.programs.end());
    }
#endif
#ifdef EID3D11_EMBEDDED
    // The tool and embedded runtime use the same shader producers and
    // homogeneous native source. Keep the borrowed views alive.
    std::vector<std::string> embeddedSources;
    embeddedSources.reserve(programs.size());
    for (auto& program : programs) {
        embeddedSources.emplace_back(program.source);
        embedded_triangle_shader(embeddedSources.back());
        program.source = embeddedSources.back();
    }
#endif
    std::vector<std::pair<ShaderKey, std::string>> table(programs.size());
    std::vector<std::string> errors(programs.size());
    std::vector<double> seconds(programs.size(), 0.0);  // compile time of each compiled program
    std::atomic<std::size_t> next{0}, compiled{0}, stored{0}, migrated{0}, missing{0};
    // The slowest programs first (terrain pixel variants ~60 s, then the other
    // native pixel programs ~5-15 s, then everything else), so no worker
    // starts a minute-long compile at the end of the queue.
    std::vector<std::size_t> queue_order(programs.size());
    for (std::size_t i = 0; i < queue_order.size(); ++i) queue_order[i] = i;
    const auto weight = [&](std::size_t i) {
        const ShaderProgram& program = programs[i];
        if (!std::strcmp(program.entry, "nativePS")) return define_value(program, "NATIVE_TERRAIN_PS") ? 0 : 1;
        return std::strstr(program.entry, "PS") || std::strstr(program.entry, "CS") ? 2 : 3;
    };
    std::stable_sort(queue_order.begin(), queue_order.end(), [&](std::size_t a, std::size_t b) { return weight(a) < weight(b); });
    const auto work = [&]() {
        for (std::size_t n = next++; n < programs.size(); n = next++) {
            const std::size_t i = queue_order[n];
            const ShaderProgram& program = programs[i];
            table[i].first = shader_key(program);
            ShaderKey stored_key{}, legacy_key{};
            const bool keyed = !g_store.empty() && compile_key(program, stored_key, legacy_key);
            const void* code = nullptr;
            std::size_t size = 0;
            if (!prepare_keys && shader_cache_detail::find(table[i].first, code, size)) {
                table[i].second.assign(static_cast<const char*>(code), size);
                continue;
            }
            if (keyed && store_read(stored_key, table[i].second)) { ++stored; continue; }
            if (keyed && store_read(legacy_key, table[i].second)) {
                store_write(stored_key, table[i].second.data(), table[i].second.size());
                std::string verified;
                if (!store_read(stored_key, verified) || verified != table[i].second)
                    errors[i] = program_label(program) + ": store migration write failed";
                else ++migrated;
                continue;
            }
            if (prepare_keys) { ++missing; continue; }
            LARGE_INTEGER start{}, stop{};
            QueryPerformanceCounter(&start);
            Microsoft::WRL::ComPtr<ID3DBlob> blob, messages;
            if (FAILED(D3DCompile(program.source.data(), program.source.size(), program.name, program.defines, nullptr,
                                  program.entry, program.profile, program.flags, 0, &blob, &messages))) {
                errors[i] = std::string(program.entry) + ": " +
                            (messages ? static_cast<const char*>(messages->GetBufferPointer()) : "compile failed");
                continue;
            }
            QueryPerformanceCounter(&stop);
            seconds[i] = double(stop.QuadPart - start.QuadPart) / double(frequency.QuadPart);
            table[i].second.assign(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
            if (keyed) store_write(stored_key, blob->GetBufferPointer(), blob->GetBufferSize());
            ++compiled;
        }
    };
    // This tool is built for x64; use the available logical processors.
    const unsigned threads = std::max(std::thread::hardware_concurrency(), 1u);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back(work);
    for (auto& worker : workers) worker.join();

    bool failed = false;
    for (const auto& error : errors)
        if (!error.empty()) { std::fprintf(stderr, "shader_embed: %s\n", error.c_str()); failed = true; }
    if (failed) return 1;

    std::printf("shader_embed: %zu programs, %zu migrated, %zu already stored, %zu missing (key preparation)\n",
                programs.size(), migrated.load(), stored.load(), missing.load());
    if (prepare_keys) return missing ? 1 : 0;
    if (native_pixel_gate(programs, table)) return 1;
    const std::vector<unsigned char> bytes = build_shader_table(std::move(table));
    if (bytes != previous) {
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) { std::fprintf(stderr, "shader_embed: cannot write %s\n", output.c_str()); return 1; }
    }
    const std::string name = output.substr(output.find_last_of("\\/") + 1);
    const std::string rc = output.substr(0, output.size() - (output.size() >= 4 ? 4 : 0)) + ".rc";
    {
        std::ofstream out(rc, std::ios::trunc);
        out << "EID3D11_SHADERS RCDATA \"" << name << "\"\n";
        if (!out) { std::fprintf(stderr, "shader_embed: cannot write %s\n", rc.c_str()); return 1; }
    }
    QueryPerformanceCounter(&end);
    std::printf("shader_embed: %zu programs, %zu compiled, %zu reused (%zu from the store), %zu bytes, %.1f s\n",
                programs.size(), compiled.load(), programs.size() - compiled.load(), stored.load(), bytes.size(),
                double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart));
    {
        // The slowest compiles (the build-time budget's owners).
        std::vector<std::size_t> order;
        double total = 0;
        for (std::size_t i = 0; i < programs.size(); ++i) if (seconds[i] > 0) { order.push_back(i); total += seconds[i]; }
        std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return seconds[x] > seconds[y]; });
        if (!order.empty())
            std::printf("shader_embed: compile time %.1f s summed over %zu programs (%u workers); slowest:\n", total, order.size(), threads);
        for (std::size_t n = 0; n < order.size() && n < 10; ++n)
            std::printf("shader_embed:   %6.1f s %s\n", seconds[order[n]], program_label(programs[order[n]]).c_str());
    }

    append_d3d12_shader_programs(programs);  // the DXIL table only
    if (!dxil_directory.empty() && write_dxil_sources(dxil_directory, programs)) return 1;
    if (!preset_cache) return 0;
    // --preset-cache (build-local-x86.cmd with EID3D11_PRESET_CACHE=1, for
    // staged candidates and packages): the shipped presets' specialised
    // native pixel programs, one DXBC file a
    // program in eid3d11_shader_cache next to the table (staged next to the
    // DLL; the renderer loads them on demand, renderer.cpp). A file whose name
    // (its program key) exists is current; files of no current program are
    // removed, so the folder holds exactly the shipped sets.
    QueryPerformanceCounter(&begin);
    const std::string directory = preset_directory(output);
    CreateDirectoryA(directory.c_str(), nullptr);
    std::vector<NativeSpecialisedPrograms> lists;
    std::vector<const ShaderProgram*> special;
    if (!preset_programs(lists, special)) return 1;
    const std::size_t count = special.size();
    std::vector<std::string> wanted(count);
    std::vector<ShaderKey> preset_keys(count), legacy_keys(count);
    std::vector<char> keyed(count, 0);
    parallel_for(count, worker_count(), [&](std::size_t i) {
        wanted[i] = preset_file(directory, *special[i]);
        keyed[i] = compile_key(*special[i], preset_keys[i], legacy_keys[i]);
    });
    // One compile per distinct program: the presets share many specialised
    // programs that preprocess to the same text (cand-1128's 1209 programs
    // were 767 distinct DXBC files), and the same input compiles to the same
    // DXBC (the store relies on that too). The others copy their first's file.
    std::vector<std::size_t> first(count), distinct;
    {
        std::map<ShaderKey, std::size_t> seen;
        for (std::size_t i = 0; i < count; ++i) {
            first[i] = keyed[i] ? seen.emplace(preset_keys[i], i).first->second : i;
            if (first[i] == i) distinct.push_back(i);
        }
    }
    std::atomic<std::size_t> special_stored{0};
    std::vector<char> needed(count, 0);
    parallel_for(distinct.size(), worker_count(), [&](std::size_t n) {
        const std::size_t i = distinct[n];
        const bool stored = !g_store.empty() && keyed[i];
        if (!stored && GetFileAttributesA(wanted[i].c_str()) != INVALID_FILE_ATTRIBUTES) return;
        if (stored && CopyFileA(store_file(preset_keys[i]).c_str(), wanted[i].c_str(), FALSE)) { ++special_stored; return; }
        std::string legacy;
        if (stored && store_read(legacy_keys[i], legacy)) {
            store_write(preset_keys[i], legacy.data(), legacy.size());
            if (CopyFileA(store_file(preset_keys[i]).c_str(), wanted[i].c_str(), FALSE)) { ++special_stored; return; }
        }
        needed[i] = 1;
    });
    std::vector<std::size_t> compile;
    for (const std::size_t i : distinct) if (needed[i]) compile.push_back(i);
    unsigned processes = 0;
    if (!compile.empty()) {
        // The compiles run in child processes of this tool (--preset-worker),
        // kPresetThreads each: FXC holds up to ~250 MB for these programs and
        // this 32-bit tool has 4 GB, so one process cannot use every core
        // (four workers in one process took a full preset build 47 min).
        const auto ceil_div = [](std::size_t x, std::size_t y) { return unsigned((x + y - 1) / y); };
        processes = (std::max)(1u, (std::min)(ceil_div(worker_count(), kPresetThreads), ceil_div(compile.size(), kPresetThreads)));
        if (run_preset_workers(output, compile, processes)) return 1;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (first[i] == i || wanted[first[i]] == wanted[i]) continue;
        if (!CopyFileA(wanted[first[i]].c_str(), wanted[i].c_str(), FALSE)) {
            std::fprintf(stderr, "shader_embed: cannot write %s\n", wanted[i].c_str());
            return 1;
        }
    }
    std::size_t removed = 0;
    WIN32_FIND_DATAA found{};
    const HANDLE search = FindFirstFileA((directory + "\\*").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::string path = directory + "\\" + found.cFileName;
            if (std::find(wanted.begin(), wanted.end(), path) == wanted.end() && DeleteFileA(path.c_str())) ++removed;
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    QueryPerformanceCounter(&end);
    std::printf("shader_embed: %zu preset keys, %zu specialised programs (%zu distinct), %zu compiled (%u processes), %zu from the store, %zu removed, %.1f s\n",
                lists.size(), special.size(), distinct.size(), compile.size(), processes, special_stored.load(), removed,
                double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart));
    return 0;
}
