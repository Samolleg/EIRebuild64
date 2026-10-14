#pragma once
// Shader bytecode for the renderer's fixed program list (every feature's
// append_*_shader_programs). The build compiles the list once with the
// shader_embed tool (Tools/shader_embed.cpp) and links the table into the
// DLL as the EID3D11_SHADERS RCDATA resource; the renderer registers it with
// use_embedded_shaders before any pipeline is created.
//
// An entry is keyed by a 128-bit hash of the program (source text, source
// name, defines, entry, profile, flags) and the table format version, so a
// table built from other source can never serve a changed program: the
// lookup misses and the program is compiled at run time (developer edits,
// test executables without the resource), counted as an on-demand compile.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace owned {

// One compiled program. source, name and defines must outlive every use
// (static shader text and static define tables).
struct ShaderProgram {
    std::string_view source;
    const char* name = nullptr;               // D3DCompile source name, may be null
    const D3D_SHADER_MACRO* defines = nullptr; // null-terminated, may be null
    const char* entry = "";
    const char* profile = "";
    UINT flags = 0;
};

struct ShaderKey {
    std::uint64_t a = 0, b = 0;
    bool operator<(const ShaderKey& other) const noexcept { return a != other.a ? a < other.a : b < other.b; }
    bool operator==(const ShaderKey& other) const noexcept { return a == other.a && b == other.b; }
};

namespace shader_cache_detail {

constexpr std::uint32_t kMagic = 0x42534945u;  // "EISB"
constexpr std::uint32_t kVersion = 2;

// Table layout: header, entries sorted by key, then the bytecode.
struct TableHeader { std::uint32_t magic, version, count, reserved; };
struct TableEntry { std::uint64_t key_a, key_b; std::uint32_t offset, size; };

// Two FNV-1a style streams give a 128-bit key.
struct Hasher {
    std::uint64_t a = 14695981039346656037ull, b = 0x84222325cbf29ce4ull;
    void bytes(const void* data, std::size_t size) noexcept {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            a = (a ^ p[i]) * 1099511628211ull;
            b = (b ^ p[i]) * 0x9E3779B97F4A7C15ull;
            b ^= b >> 29;
        }
    }
    void text(const char* value) noexcept {
        if (value) bytes(value, std::strlen(value));
        const unsigned char end = 0xFF;  // field separator; never inside HLSL text
        bytes(&end, 1);
    }
};

struct Table { const unsigned char* data = nullptr; std::uint32_t count = 0; };
inline std::atomic<const Table*> table{nullptr};

// Programs a preparation had to compile (no table entry), taken once by the feature.
struct Prepared {
    std::mutex mutex;
    std::map<ShaderKey, Microsoft::WRL::ComPtr<ID3DBlob>> blobs;
};
inline Prepared& prepared() { static Prepared value; return value; }

// FXC runs outside a preparation (on the render thread or a feature worker).
inline std::atomic<std::uint32_t> on_demand_compiles{0};
inline std::atomic<const char*> last_on_demand_entry{nullptr};

inline bool key_less(const TableEntry& entry, const ShaderKey& key) noexcept {
    return entry.key_a != key.a ? entry.key_a < key.a : entry.key_b < key.b;
}

// The table bytecode for key, or false.
inline bool find(const ShaderKey& key, const void*& code, std::size_t& size) noexcept {
    const Table* t = table.load();
    if (!t) return false;
    const auto* first = reinterpret_cast<const TableEntry*>(t->data + sizeof(TableHeader));
    const auto* last = first + t->count;
    const auto* found = std::lower_bound(first, last, key, key_less);
    if (found == last || found->key_a != key.a || found->key_b != key.b) return false;
    code = t->data + found->offset;
    size = found->size;
    return true;
}

} // namespace shader_cache_detail

// The key's hash state after the version and a source: programs sharing one
// source continue from it (shader_key(state, program) equals
// shader_key(program) for a program of that source), so a long source is
// hashed once, not once a program.
inline shader_cache_detail::Hasher shader_source_state(std::string_view source) noexcept {
    shader_cache_detail::Hasher h;
    const std::uint32_t version = shader_cache_detail::kVersion;
    h.bytes(&version, sizeof(version));
    h.bytes(source.data(), source.size());
    return h;
}
inline ShaderKey shader_key(shader_cache_detail::Hasher h, const ShaderProgram& program) noexcept {
    h.text(nullptr);
    h.text(program.name);
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
        h.text(define->Name);
        h.text(define->Definition);
    }
    h.text(nullptr);
    h.text(program.entry);
    h.text(program.profile);
    h.bytes(&program.flags, sizeof(program.flags));
    return {h.a, h.b};
}
inline ShaderKey shader_key(const ShaderProgram& program) noexcept {
    shader_cache_detail::Hasher h;
    const std::uint32_t version = shader_cache_detail::kVersion;
    h.bytes(&version, sizeof(version));
    h.bytes(program.source.data(), program.source.size());
    h.text(nullptr);
    h.text(program.name);
    for (const D3D_SHADER_MACRO* define = program.defines; define && define->Name; ++define) {
        h.text(define->Name);
        h.text(define->Definition);
    }
    h.text(nullptr);
    h.text(program.entry);
    h.text(program.profile);
    h.bytes(&program.flags, sizeof(program.flags));
    return {h.a, h.b};
}

// Program count of a well-formed table (header, strictly sorted keys, payloads
// inside the data), else 0.
inline std::uint32_t validate_shader_table(const void* data, std::size_t size) noexcept {
    namespace detail = shader_cache_detail;
    if (!data || size < sizeof(detail::TableHeader)) return 0;
    detail::TableHeader header;
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != detail::kMagic || header.version != detail::kVersion ||
        header.count > (size - sizeof(header)) / sizeof(detail::TableEntry)) return 0;
    const auto* entries = reinterpret_cast<const detail::TableEntry*>(static_cast<const unsigned char*>(data) + sizeof(header));
    for (std::uint32_t i = 0; i < header.count; ++i) {
        const auto& e = entries[i];
        if (e.size == 0 || e.offset > size || e.size > size - e.offset) return 0;
        if (i && !detail::key_less(entries[i - 1], ShaderKey{e.key_a, e.key_b})) return 0;
    }
    return header.count;
}

// Registers the table compile_shader uses from now on; data must outlive every
// lookup (the DLL's resource). Returns its program count; a malformed table
// is rejected (0) and leaves no table.
inline std::uint32_t use_embedded_shaders(const void* data, std::size_t size) noexcept {
    namespace detail = shader_cache_detail;
    const std::uint32_t count = validate_shader_table(data, size);
    // Registered once per process by the renderer; a replaced descriptor is
    // left alive because a worker may still hold it.
    auto* registered = count ? new (std::nothrow) detail::Table{static_cast<const unsigned char*>(data), count} : nullptr;
    detail::table = registered;
    return registered ? count : 0;
}

// Serialises programs into the table format (shader_embed and the regression).
inline std::vector<unsigned char> build_shader_table(std::vector<std::pair<ShaderKey, std::string>> programs) {
    namespace detail = shader_cache_detail;
    std::sort(programs.begin(), programs.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
    programs.erase(std::unique(programs.begin(), programs.end(),
                               [](const auto& l, const auto& r) { return l.first == r.first; }), programs.end());
    const auto padded = [](std::size_t value) { return (value + 15) & ~std::size_t(15); };
    const std::size_t head = padded(sizeof(detail::TableHeader) + programs.size() * sizeof(detail::TableEntry));
    std::size_t total = head;
    for (const auto& program : programs) total += padded(program.second.size());
    std::vector<unsigned char> out(total, 0);
    const detail::TableHeader header{detail::kMagic, detail::kVersion, static_cast<std::uint32_t>(programs.size()), 0};
    std::memcpy(out.data(), &header, sizeof(header));
    std::size_t at = head;
    for (std::size_t i = 0; i < programs.size(); ++i) {
        const detail::TableEntry entry{programs[i].first.a, programs[i].first.b, static_cast<std::uint32_t>(at),
                                       static_cast<std::uint32_t>(programs[i].second.size())};
        std::memcpy(out.data() + sizeof(header) + i * sizeof(entry), &entry, sizeof(entry));
        std::memcpy(out.data() + at, programs[i].second.data(), programs[i].second.size());
        at += padded(programs[i].second.size());
    }
    return out;
}

// D3DCompile through the embedded table: a table entry, else a program a
// preparation compiled, else FXC (counted as on demand).
inline HRESULT compile_shader(const ShaderProgram& program, ID3DBlob** code, ID3DBlob** errors = nullptr) noexcept try {
    namespace detail = shader_cache_detail;
    if (!code) return E_POINTER;
    *code = nullptr;
    if (errors) *errors = nullptr;
    const ShaderKey key = shader_key(program);
    const void* embedded = nullptr;
    std::size_t size = 0;
    if (detail::find(key, embedded, size)) {
        Microsoft::WRL::ComPtr<ID3DBlob> blob;
        const HRESULT hr = D3DCreateBlob(size, &blob);
        if (FAILED(hr)) return hr;
        std::memcpy(blob->GetBufferPointer(), embedded, size);
        *code = blob.Detach();
        return S_OK;
    }
    {
        std::lock_guard<std::mutex> lock(detail::prepared().mutex);
        const auto found = detail::prepared().blobs.find(key);
        if (found != detail::prepared().blobs.end()) {
            *code = found->second.Detach();
            detail::prepared().blobs.erase(found);
            return S_OK;
        }
    }
    Microsoft::WRL::ComPtr<ID3DBlob> blob, messages;
    const HRESULT hr = D3DCompile(program.source.data(), program.source.size(), program.name, program.defines, nullptr,
                                  program.entry, program.profile, program.flags, 0, &blob, &messages);
    ++detail::on_demand_compiles;
    detail::last_on_demand_entry = program.entry;
    if (errors) *errors = messages.Detach();
    if (FAILED(hr)) return hr;
    *code = blob.Detach();
    return hr;
} catch (...) {
    return E_OUTOFMEMORY;
}

// Creates and releases the D3D shader object of every program on worker
// threads, so the driver has translated the bytecode before the features ask
// for it. A program without a table entry is compiled first and kept for its
// feature. Without a device only that compile step runs. The destructor
// waits for the workers.
class ShaderPreparation {
public:
    ShaderPreparation(std::vector<ShaderProgram> programs, unsigned threads, ID3D11Device* device = nullptr)
        : programs_(std::move(programs)), device_(device) {
        threads = (std::max)(1u, (std::min)(threads, static_cast<unsigned>(programs_.size())));
        try {
            for (unsigned i = 0; i < threads && !programs_.empty(); ++i) workers_.emplace_back([this] { run(); });
        } catch (...) {
            if (workers_.empty()) run();  // no threads available: prepare inline
        }
    }
    ~ShaderPreparation() {
        for (auto& worker : workers_) worker.join();
    }
    ShaderPreparation(const ShaderPreparation&) = delete;
    ShaderPreparation& operator=(const ShaderPreparation&) = delete;

    std::size_t total() const noexcept { return programs_.size(); }
    std::size_t done() const noexcept { return done_.load(); }
    std::size_t compiled() const noexcept { return compiled_.load(); }  // had no table entry
    std::size_t created() const noexcept { return created_.load(); }
    std::size_t failed() const noexcept { return failed_.load(); }
    // True once every program is prepared.
    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return finished_.wait_for(lock, timeout, [this] { return done_.load() == programs_.size(); });
    }

private:
    // The stage follows the profile prefix (vs_, ps_, cs_, hs_, ds_, gs_).
    bool create(const char* profile, const void* code, std::size_t size) noexcept {
        if (!device_) return true;
        HRESULT hr = E_INVALIDARG;
        switch (profile[0]) {
        case 'v': { Microsoft::WRL::ComPtr<ID3D11VertexShader> s; hr = device_->CreateVertexShader(code, size, nullptr, &s); break; }
        case 'p': { Microsoft::WRL::ComPtr<ID3D11PixelShader> s; hr = device_->CreatePixelShader(code, size, nullptr, &s); break; }
        case 'c': { Microsoft::WRL::ComPtr<ID3D11ComputeShader> s; hr = device_->CreateComputeShader(code, size, nullptr, &s); break; }
        case 'h': { Microsoft::WRL::ComPtr<ID3D11HullShader> s; hr = device_->CreateHullShader(code, size, nullptr, &s); break; }
        case 'd': { Microsoft::WRL::ComPtr<ID3D11DomainShader> s; hr = device_->CreateDomainShader(code, size, nullptr, &s); break; }
        case 'g': { Microsoft::WRL::ComPtr<ID3D11GeometryShader> s; hr = device_->CreateGeometryShader(code, size, nullptr, &s); break; }
        default: break;
        }
        return SUCCEEDED(hr);
    }

    void run() noexcept {
        namespace detail = shader_cache_detail;
        for (std::size_t index = next_++; index < programs_.size(); index = next_++) {
            const ShaderProgram& program = programs_[index];
            const ShaderKey key = shader_key(program);
            const void* code = nullptr;
            std::size_t size = 0;
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            bool ok = detail::find(key, code, size);
            if (!ok) {
                Microsoft::WRL::ComPtr<ID3DBlob> messages;
                ok = SUCCEEDED(D3DCompile(program.source.data(), program.source.size(), program.name, program.defines,
                                          nullptr, program.entry, program.profile, program.flags, 0, &blob, &messages));
                if (ok) {
                    ++compiled_;
                    code = blob->GetBufferPointer();
                    size = blob->GetBufferSize();
                    try {
                        std::lock_guard<std::mutex> lock(detail::prepared().mutex);
                        detail::prepared().blobs[key] = blob;
                    } catch (...) {}
                } else if (messages) {
                    // The feature's own compile reports the error and omits it.
                    OutputDebugStringA(static_cast<const char*>(messages->GetBufferPointer()));
                }
            }
            if (ok && create(program.profile, code, size)) ++created_;
            else ++failed_;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++done_;
            }
            finished_.notify_all();
        }
    }

    std::vector<ShaderProgram> programs_;
    ID3D11Device* device_ = nullptr;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> next_{0}, done_{0}, compiled_{0}, created_{0}, failed_{0};
    std::mutex mutex_;
    std::condition_variable finished_;
};

} // namespace owned
