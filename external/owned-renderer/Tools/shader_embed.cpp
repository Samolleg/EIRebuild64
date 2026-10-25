// Build step: compiles the renderer's fixed shader program list (every
// feature's append_*_shader_programs, from the same production sources as the
// DLL) and writes the table the DLL embeds as the EID3D11_SHADERS resource
// (Source/shader_cache.h), plus the one-line .rc that names it.
//
//   shader_embed <output.bin> [--preset-cache]
//
// The previous output is the incremental cache: a program whose key it
// already holds is reused, so only edited programs are compiled again; stale
// entries are simply not written. A compile error fails the build.
#include "renderer.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_triangle_shader.h"
#endif

#include <cstdio>
#include <fstream>
#include <iterator>
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
} // namespace owned

using namespace owned;

int main(int argc, char** argv) {
    const bool preset_cache = argc == 3 && std::strcmp(argv[2], "--preset-cache") == 0;
    if (argc != 2 && !preset_cache) {
        std::fprintf(stderr, "usage: shader_embed <output.bin> [--preset-cache]\n");
        return 2;
    }
    const std::string output = argv[1];
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);

    std::vector<unsigned char> previous;
    {
        std::ifstream in(output, std::ios::binary);
        previous.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!validate_shader_table(previous.data(), previous.size())) previous.clear();
    if (!previous.empty()) use_embedded_shaders(previous.data(), previous.size());

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
    std::atomic<std::size_t> next{0}, compiled{0};
    const auto work = [&]() {
        for (std::size_t i = next++; i < programs.size(); i = next++) {
            const ShaderProgram& program = programs[i];
            table[i].first = shader_key(program);
            const void* code = nullptr;
            std::size_t size = 0;
            if (shader_cache_detail::find(table[i].first, code, size)) {
                table[i].second.assign(static_cast<const char*>(code), size);
                continue;
            }
            Microsoft::WRL::ComPtr<ID3DBlob> blob, messages;
            if (FAILED(D3DCompile(program.source.data(), program.source.size(), program.name, program.defines, nullptr,
                                  program.entry, program.profile, program.flags, 0, &blob, &messages))) {
                errors[i] = std::string(program.entry) + ": " +
                            (messages ? static_cast<const char*>(messages->GetBufferPointer()) : "compile failed");
                continue;
            }
            table[i].second.assign(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
            ++compiled;
        }
    };
    // FXC holds up to ~250 MB for the largest programs; four workers stay well
    // inside this 32-bit tool's address space and nativePS bounds the time anyway.
    const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back(work);
    for (auto& worker : workers) worker.join();

    bool failed = false;
    for (const auto& error : errors)
        if (!error.empty()) { std::fprintf(stderr, "shader_embed: %s\n", error.c_str()); failed = true; }
    if (failed) return 1;

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
    std::printf("shader_embed: %zu programs, %zu compiled, %zu reused, %zu bytes, %.1f s\n", programs.size(),
                compiled.load(), programs.size() - compiled.load(), bytes.size(),
                double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart));

    if (!preset_cache) return 0;
    // --preset-cache (build-local-x86.cmd with EID3D11_PRESET_CACHE=1, for
    // staged candidates and packages): the shipped presets' specialised
    // native pixel programs, one DXBC file a
    // program in eid3d11_shader_cache next to the table (staged next to the
    // DLL; the renderer loads them on demand, renderer.cpp). A file whose name
    // (its program key) exists is current; files of no current program are
    // removed, so the folder holds exactly the shipped sets.
    QueryPerformanceCounter(&begin);
    const std::string directory = output.substr(0, output.find_last_of("\\/") + 1) + "eid3d11_shader_cache";
    CreateDirectoryA(directory.c_str(), nullptr);
    std::vector<NativeSpecialisedPrograms> lists;
    for (const std::uint32_t key : native_shipped_specialisation_keys()) {
        lists.emplace_back();
        if (!native_specialised_programs(key, lists.back())) {
            std::fprintf(stderr, "shader_embed: specialised list of key 0x%03x does not match its table\n", key);
            return 1;
        }
    }
    std::vector<const ShaderProgram*> special;
    for (const auto& list : lists) for (const auto& program : list.programs) special.push_back(&program);
    const auto file_of = [&](const ShaderProgram& program) {
        const std::wstring name = native_spec_cache_file_name(shader_key(program));
        std::string narrow;
        for (const wchar_t ch : name) narrow += static_cast<char>(ch); // hex digits and ".dxbc"
        return directory + "\\" + narrow;
    };
    std::vector<std::string> wanted(special.size()), special_errors(special.size());
    std::atomic<std::size_t> special_next{0}, special_compiled{0};
    const auto special_work = [&]() {
        for (std::size_t i = special_next++; i < special.size(); i = special_next++) {
            const ShaderProgram& program = *special[i];
            wanted[i] = file_of(program);
            if (GetFileAttributesA(wanted[i].c_str()) != INVALID_FILE_ATTRIBUTES) continue;
            Microsoft::WRL::ComPtr<ID3DBlob> blob, messages;
            if (FAILED(D3DCompile(program.source.data(), program.source.size(), program.name, program.defines, nullptr,
                                  program.entry, program.profile, program.flags, 0, &blob, &messages))) {
                special_errors[i] = std::string(program.entry) + ": " +
                                    (messages ? static_cast<const char*>(messages->GetBufferPointer()) : "compile failed");
                continue;
            }
            const std::string temporary = wanted[i] + ".tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out.write(static_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
                if (!out) { special_errors[i] = "cannot write " + temporary; continue; }
            }
            if (!MoveFileExA(temporary.c_str(), wanted[i].c_str(), MOVEFILE_REPLACE_EXISTING)) {
                special_errors[i] = "cannot write " + wanted[i];
                continue;
            }
            ++special_compiled;
        }
    };
    special_next = 0;
    workers.clear();
    // Every hardware thread, up to 12: these programs are far smaller than the
    // general terrain ones, and 12 FXC instances stay inside this 32-bit
    // tool's 4 GB address space.
    const unsigned special_threads = std::clamp(std::thread::hardware_concurrency(), 1u, 12u);
    for (unsigned t = 0; t < special_threads; ++t) workers.emplace_back(special_work);
    for (auto& worker : workers) worker.join();
    for (const auto& error : special_errors)
        if (!error.empty()) { std::fprintf(stderr, "shader_embed: %s\n", error.c_str()); failed = true; }
    if (failed) return 1;
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
    std::printf("shader_embed: %zu preset keys, %zu specialised programs, %zu compiled, %zu removed, %.1f s\n", lists.size(),
                special.size(), special_compiled.load(), removed,
                double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart));
    return 0;
}
