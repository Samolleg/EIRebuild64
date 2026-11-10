#include <DirectXTex.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
using namespace DirectX;
namespace fs = std::filesystem;
using Pixel = std::array<float, 4>;
struct Surface
{
    size_t w, h;
    std::vector<Pixel> p;
};
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void check(HRESULT hr)
{
    if (FAILED(hr))
    {
        std::cerr << "HRESULT 0x" << std::hex << hr << '\n';
        throw std::runtime_error("DirectXTex operation failed");
    }
}
// BC blocks are independent. Partition on block-row boundaries, preserving
// the default-quality DirectXTex result without an OpenMP DLL dependency.
void compress_rows(const ScratchImage& source, DXGI_FORMAT format, ScratchImage& output)
{
    const auto& metadata = source.GetMetadata();
    check(output.Initialize2D(format, metadata.width, metadata.height, 1, metadata.mipLevels));
    struct Job { size_t mip, row, height; };
    std::vector<Job> jobs;
    for (size_t mip=0; mip<metadata.mipLevels; ++mip) {
        const auto& image=*source.GetImage(mip,0,0);
        for (size_t row=0; row<image.height; row+=16)
            jobs.push_back({mip,row,std::min(size_t(16),image.height-row)});
    }
    std::atomic<size_t> next{0};
    std::atomic<HRESULT> failure{S_OK};
    const auto work=[&] {
        for (size_t index=next++; index<jobs.size() && SUCCEEDED(failure.load()); index=next++) {
            const auto job=jobs[index];
            Image strip=*source.GetImage(job.mip,0,0);
            strip.pixels+=strip.rowPitch*job.row;strip.height=job.height;
            strip.slicePitch=strip.rowPitch*strip.height;
            ScratchImage compressed;
            const HRESULT hr=Compress(strip,format,TEX_COMPRESS_DEFAULT,1,compressed);
            if (FAILED(hr)) { failure.store(hr);break; }
            const auto& src=*compressed.GetImage(0,0,0);
            const auto& dst=*output.GetImage(job.mip,0,0);
            for (size_t row=0;row<(job.height+3)/4;++row)
                std::memcpy(dst.pixels+(job.row/4+row)*dst.rowPitch,
                            src.pixels+row*src.rowPitch,src.rowPitch);
        }
    };
    std::vector<std::thread> workers;
    try {
        const auto count=std::clamp(std::thread::hardware_concurrency(),1u,4u);
        for(unsigned i=0;i<count;++i)workers.emplace_back(work);
    } catch (...) {
        failure.store(E_OUTOFMEMORY);
        for(auto& worker:workers)worker.join();
        throw;
    }
    for(auto& worker:workers)worker.join();
    check(failure.load());
}
float number(const std::wstring &s)
{
    size_t end;
    float f = std::stof(s, &end);
    require(end == s.size() && std::isfinite(f), "invalid finite number");
    return f;
}
size_t dimension(const std::wstring &s)
{
    float f = number(s);
    require(f >= 1 && f <= 16384 && std::floor(f) == f, "dimension must be integer 1..16384");
    return size_t(f);
}
float unit(const std::wstring &s)
{
    float f = number(s);
    require(f >= 0 && f <= 1, "value outside [0,1]");
    return f;
}
float linear(float x)
{
    return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
float srgb(float x)
{
    return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.f / 2.4f) - .055f;
}
Surface load(const fs::path &path, size_t w, size_t h)
{
    std::ifstream probe(path, std::ios::binary);
    require(bool(probe), "cannot open input");
    char head[43]{};
    probe.read(head, 42);
    require(std::string(head).find("version https://git-lfs") != 0,
            "input is LFS pointer, pixels missing");
    if (path.extension() == L".rgb")
    {
        require(w && h, "RGB8 requires --width and --height");
        require(fs::file_size(path) == w * h * 3, "RGB8 byte count mismatch");
        probe.clear();
        probe.seekg(0);
        std::vector<unsigned char> bytes(w * h * 3);
        probe.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        Surface s{ w, h, std::vector<Pixel>(w * h) };
        for (size_t i = 0; i < w * h; ++i)
            s.p[i] = { bytes[3 * i] / 255.f, bytes[3 * i + 1] / 255.f, bytes[3 * i + 2] / 255.f,
                       1 };
        return s;
    }
    ScratchImage image, rgba;
    if (path.extension() == L".tga")
        check(LoadFromTGAFile(path.c_str(), TGA_FLAGS_IGNORE_SRGB, nullptr, image));
    else
    {
        require(path.extension() == L".png", "only PNG/TGA/RGB8 accepted");
        check(LoadFromWICFile(path.c_str(), WIC_FLAGS_IGNORE_SRGB, nullptr, image));
    }
    check(Convert(*image.GetImage(0, 0, 0), DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, 0,
                  rgba));
    const Image &im = *rgba.GetImage(0, 0, 0);
    require((!w || w == im.width) && (!h || h == im.height), "explicit dimensions mismatch");
    Surface s{ im.width, im.height, std::vector<Pixel>(im.width * im.height) };
    for (size_t y = 0; y < s.h; ++y)
        std::copy_n(reinterpret_cast<const Pixel *>(im.pixels + y * im.rowPitch), s.w,
                    s.p.data() + y * s.w);
    // Single-channel grayscale images must supply all RGB components, rather
    // than the GPU's default (R,0,0,1) interpretation of an R-only format.
    auto sourceFormat = image.GetMetadata().format;
    if (sourceFormat == DXGI_FORMAT_R8_UNORM || sourceFormat == DXGI_FORMAT_R16_UNORM ||
        sourceFormat == DXGI_FORMAT_R16_FLOAT || sourceFormat == DXGI_FORMAT_R32_FLOAT)
        for (auto &p : s.p)
            p[1] = p[2] = p[0];
    return s;
}
void normalize(Pixel &p)
{
    float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    require(std::isfinite(len) && len > .01f, "empty/degenerate normal");
    for (int c = 0; c < 3; ++c)
        p[c] /= len;
    require(p[2] >= 0, "BC5 reconstructed Z requires positive hemisphere");
}
// Mask: RGB island IDs, black = outside UVs. Propagate nearest occupied
// texels into empty space at EVERY level; alpha remains the unpadded coverage.
void pad(Surface &s, const std::vector<unsigned> &ids)
{
    if (ids.empty())
        return;
    std::vector<int> source(ids.size(), -1);
    std::queue<size_t> q;
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i])
        {
            source[i] = int(i);
            q.push(i);
        }
    require(!q.empty(), "empty island mask");
    while (!q.empty())
    {
        size_t i = q.front();
        q.pop();
        size_t x = i % s.w, y = i / s.w;
        size_t neighbors[4] = { x ? i - 1 : i, x + 1 < s.w ? i + 1 : i, y ? i - s.w : i,
                                y + 1 < s.h ? i + s.w : i };
        for (auto n : neighbors)
            if (source[n] < 0)
            {
                source[n] = source[i];
                q.push(n);
            }
    }
    for (size_t i = 0; i < ids.size(); ++i)
        if (!ids[i])
            for (int c = 0; c < 3; ++c)
                s.p[i][c] = s.p[source[i]][c];
}
float coverage(const Surface &s, float cutoff, float scale = 1)
{
    size_t n = 0;
    for (const auto &p : s.p)
        if (std::lround(std::min(1.f, p[3] * scale) * 255) / 255.f >= cutoff)
            ++n;
    return float(n) / float(s.p.size());
}
int wmain(int argc, wchar_t **argv)
{
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try
    {
        check(com);
        std::map<std::wstring, std::wstring> a;
        const std::vector<std::wstring> keys = { L"role",     L"space",           L"input",
                                                 L"output",   L"width",           L"height",
                                                 L"ao",       L"roughness",       L"metallic",
                                                 L"ao-value", L"roughness-value", L"metallic-value",
                                                 L"cutoff",   L"islands" };
        for (int i = 1; i < argc; i += 2)
        {
            std::wstring k = argv[i];
            require(k.substr(0, 2) == L"--" && i + 1 < argc, "expected --key value");
            k = k.substr(2);
            require(std::find(keys.begin(), keys.end(), k) != keys.end() && !a.count(k),
                    "unknown/duplicate option");
            a[k] = argv[i + 1];
        }
        auto role = a[L"role"];
        bool color = role == L"color" || role == L"emission", normal = role == L"normal",
             orm = role == L"orm";
        require(color || normal || orm, "--role color|normal|orm|emission required");
        require(a[L"space"] == (color ? L"srgb" : L"linear"), "--space does not match role");
        require(!a[L"output"].empty(), "--output required");
        size_t w = a.count(L"width") ? dimension(a[L"width"]) : 0,
               h = a.count(L"height") ? dimension(a[L"height"]) : 0;
        require(bool(w) == bool(h), "provide both dimensions");
        Surface s;
        if (orm)
        {
            require(a[L"input"].empty(), "ORM uses separate maps/constants");
            std::wstring names[3] = { L"ao", L"roughness", L"metallic" };
            std::array<Surface, 3> maps;
            for (int c = 0; c < 3; ++c)
                if (a.count(names[c]))
                {
                    maps[c] = load(a[names[c]], w, h);
                    w = maps[c].w;
                    h = maps[c].h;
                }
            require(w && h, "constant ORM requires dimensions");
            s = { w, h, std::vector<Pixel>(w * h, Pixel{ 0, 0, 0, 1 }) };
            for (int c = 0; c < 3; ++c)
            {
                auto key = names[c] + L"-value";
                require(maps[c].p.empty() == bool(a.count(key)),
                        "provide exactly one map or constant per ORM channel");
                float v = maps[c].p.empty() ? unit(a[key]) : 0;
                for (size_t i = 0; i < w * h; ++i)
                {
                    if (!maps[c].p.empty())
                    {
                        auto p = maps[c].p[i];
                        require(p[0] == p[1] && p[0] == p[2], "ORM source must be grayscale");
                        v = p[0];
                    }
                    s.p[i][c] = v;
                }
            }
        }
        else
        {
            require(!a[L"input"].empty(), "--input required");
            s = load(a[L"input"], w, h);
        }
        w = s.w;
        h = s.h;
        auto valid = [](size_t n) { return (n & (n - 1)) == 0 || n % 4 == 0; };
        require(valid(w) && valid(h), "BC dimensions must be power of two or multiple of four");
        for (auto &p : s.p)
        {
            for (float v : p)
                require(std::isfinite(v) && v >= 0 && v <= 1, "invalid pixel value");
            if (normal)
            {
                for (int c = 0; c < 3; ++c)
                    p[c] = p[c] * 2 - 1;
                normalize(p);
            }
            else if (color)
                for (int c = 0; c < 3; ++c)
                    p[c] = linear(p[c]);
        }
        std::vector<unsigned> ids;
        if (a.count(L"islands"))
        {
            auto mask = load(a[L"islands"], w, h);
            ids.resize(w * h);
            for (size_t i = 0; i < ids.size(); ++i)
                ids[i] = unsigned(std::lround(mask.p[i][0] * 255)) |
                         unsigned(std::lround(mask.p[i][1] * 255)) << 8 |
                         unsigned(std::lround(mask.p[i][2] * 255)) << 16;
        }
        float cutoff = a.count(L"cutoff") ? unit(a[L"cutoff"]) : 0;
        require(!a.count(L"cutoff") || (color && cutoff > 0),
                "cutoff requires color/emission and threshold >0");
        float target = coverage(s, cutoff);
        size_t levels = 1;
        for (size_t x = w, y = h; x > 1 || y > 1;
             x = std::max(size_t(1), x / 2), y = std::max(size_t(1), y / 2))
            ++levels;
        ScratchImage chain;
        auto fmt = color ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        check(chain.Initialize2D(fmt, w, h, 1, levels));
        for (size_t level = 0; level < levels; ++level)
        {
            pad(s, ids);
            const auto &im = *chain.GetImage(level, 0, 0);
            for (size_t y = 0; y < s.h; ++y)
                for (size_t x = 0; x < s.w; ++x)
                {
                    auto p = s.p[y * s.w + x];
                    for (int c = 0; c < 3; ++c)
                        p[c] = normal ? p[c] * .5f + .5f : color ? srgb(p[c]) : p[c];
                    for (int c = 0; c < 4; ++c)
                        im.pixels[y * im.rowPitch + x * 4 + c] = uint8_t(
                            std::lround(std::clamp(p[c], 0.f, 1.f) * 255));
                }
            if (level + 1 == levels)
                break;
            Surface next{ std::max(size_t(1), s.w / 2), std::max(size_t(1), s.h / 2), {} };
            next.p.resize(next.w * next.h);
            std::vector<unsigned> nextIds(ids.empty() ? 0 : next.w * next.h);
            for (size_t y = 0; y < next.h; ++y)
                for (size_t x = 0; x < next.w; ++x)
                {
                    Pixel p{};
                    size_t n = 0;
                    std::map<unsigned, size_t> votes;
                    for (size_t yy = y * s.h / next.h; yy < (y + 1) * s.h / next.h; ++yy)
                        for (size_t xx = x * s.w / next.w; xx < (x + 1) * s.w / next.w; ++xx)
                        {
                            size_t i = yy * s.w + xx;
                            for (int c = 0; c < 4; ++c)
                                p[c] += s.p[i][c];
                            ++n;
                            if (!ids.empty() && ids[i])
                                ++votes[ids[i]];
                        }
                    for (float &v : p)
                        v /= float(n);
                    if (normal)
                        normalize(p);
                    next.p[y * next.w + x] = p;
                    if (!nextIds.empty() && !votes.empty())
                        nextIds[y * next.w + x] =
                            std::max_element(votes.begin(), votes.end(), [](auto l, auto r) {
                                return l.second < r.second;
                            })->first;
                }
            if (cutoff)
            {
                float lo = 0, hi = 65536, best = 1, error = 2;
                for (int k = 0; k < 48; ++k)
                {
                    float mid = (lo + hi) / 2, cov = coverage(next, cutoff, mid),
                          e = std::abs(cov - target);
                    if (e < error)
                    {
                        error = e;
                        best = mid;
                    }
                    if (cov < target)
                        lo = mid;
                    else
                        hi = mid;
                }
                for (auto &p : next.p)
                    p[3] = std::min(1.f, p[3] * best);
            }
            s = std::move(next);
            ids = std::move(nextIds);
        }
        ScratchImage packed;
        compress_rows(chain,
                       normal  ? DXGI_FORMAT_BC5_UNORM
                       : color ? DXGI_FORMAT_BC7_UNORM_SRGB
                               : DXGI_FORMAT_BC7_UNORM,
                       packed);
        ScratchImage decoded;
        check(Decompress(*packed.GetImage(0, 0, 0), color ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, decoded));
        double mse[3]{};
        const auto &original = *chain.GetImage(0, 0, 0);
        const auto &d = *decoded.GetImage(0, 0, 0);
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c)
                {
                    double diff = double(original.pixels[y * original.rowPitch + 4 * x + c]) -
                                  d.pixels[y * d.rowPitch + 4 * x + c];
                    mse[c] += diff * diff;
                }
        double psnr = 0;
        for (double e : mse)
            psnr += e ? 10 * std::log10(255. * 255. * double(w * h) / e) : INFINITY;
        fs::path out = a[L"output"];
        require(!fs::exists(out), "output exists; refusing overwrite");
        check(SaveToDDSFile(packed.GetImages(), packed.GetImageCount(), packed.GetMetadata(),
                            DDS_FLAGS_FORCE_DX10_EXT, out.c_str()));
        std::cout << "width=" << w << " height=" << h << " mips=" << levels
                  << " bytes=" << fs::file_size(out) << " mean_rgb_psnr_db=" << psnr / 3 << '\n';
        CoUninitialize();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "skn_texpack: " << e.what() << '\n';
        if (SUCCEEDED(com))
            CoUninitialize();
        return 1;
    }
}
