// Fish schools of the ambient critters (issue #114); included by
// renderer_critters.cpp inside its unnamed namespace. Look and behaviour are
// the approved offline previews (claude/fish-boids Prototypes/fish:
// fishmesh2.py meshes, fishlib.py paint, flee.py school and flee rules).
//
// A fish is one procedural mesh at its real size (near and far level of
// detail; fins single-sided, rooted just inside the hull, drawn without
// culling) on a painted 128 px slice over the base game's dragon skin scale
// grain. Its body wave, C-start bend and sculling paired fins run in the
// vertex shader (kCritterShader mode 3) from per-instance values the school
// simulation steps on game time. Schools swim in the water column between
// the liquid surface (the shared height grid) and the bed.

// ---------------------------------------------------------------- shape and paint table
enum FishPattern : std::uint8_t { fp_plain = 0, fp_bars, fp_beans, fp_spots, fp_darkspots, fp_marble, fp_speckle, fp_stripe1, fp_stripes };
struct FishShape {
    float H, W, hmax, belly;                 // body height and width (x length), station of the deepest section, belly share
    float back[3], flank[3], under[3], fins[3], fins2[3], iris[3];
    float dorsal[3], dorsal2[3], anal[3];    // u0, u1, height (dorsal2 height 0: none)
    bool fork; float tail_h;
    FishPattern pattern;
    float snout; bool flathead, white_edge;
    std::uint8_t barbels; float barbel[3][2];   // angle around the head from the chin, length (x body length)
    float accent[3];                            // the pattern's own colour (spots, the side stripe); 0: the default
};
const FishShape& fish_shape(Fish f) {
    static const FishShape shapes[fish_count] = {
        // roach
        {.27f, .11f, .40f, .95f, {52, 66, 74}, {178, 184, 182}, {222, 220, 208}, {120, 112, 100}, {196, 92, 54}, {196, 60, 40},
         {.42f, .58f, .55f}, {}, {.62f, .76f, .30f}, true, .62f, fp_plain, 0, false, false, 0, {}},
        // perch
        {.30f, .13f, .40f, .85f, {58, 72, 40}, {150, 156, 92}, {222, 214, 184}, {104, 100, 82}, {214, 98, 40}, {222, 176, 52},
         {.24f, .46f, .75f}, {.50f, .66f, .55f}, {.64f, .75f, .32f}, true, .55f, fp_bars, 0, false, false, 0, {}},
        // bream
        {.40f, .10f, .42f, .95f, {58, 60, 50}, {170, 162, 126}, {212, 202, 170}, {78, 76, 68}, {96, 88, 78}, {200, 190, 150},
         {.42f, .55f, .60f}, {}, {.52f, .84f, .22f}, true, .62f, fp_plain, 0, false, false, 0, {}},
        // rudd
        {.30f, .11f, .42f, .95f, {70, 76, 48}, {198, 176, 108}, {226, 214, 170}, {156, 78, 48}, {214, 58, 38}, {214, 150, 40},
         {.50f, .62f, .50f}, {}, {.62f, .75f, .30f}, true, .60f, fp_plain, 0, false, false, 0, {}},
        // tench
        {.27f, .14f, .45f, .95f, {40, 50, 26}, {108, 108, 52}, {170, 150, 84}, {52, 54, 34}, {60, 60, 36}, {200, 70, 40},
         {.40f, .56f, .55f}, {}, {.62f, .75f, .40f}, false, .55f, fp_plain, 0, false, false, 0, {}},
        // pike
        {.17f, .10f, .55f, .9f, {46, 58, 34}, {104, 118, 62}, {214, 210, 176}, {110, 84, 50}, {150, 92, 52}, {220, 180, 60},
         {.66f, .80f, .38f}, {}, {.68f, .80f, .30f}, true, .40f, fp_beans, 1, false, false, 0, {}},
        // char
        {.24f, .12f, .42f, .95f, {48, 58, 62}, {104, 116, 112}, {214, 120, 64}, {70, 72, 70}, {200, 90, 52}, {170, 160, 120},
         {.40f, .55f, .55f}, {}, {.64f, .76f, .32f}, true, .55f, fp_spots, 0, false, true, 0, {}},
        // grayling
        {.22f, .11f, .40f, .95f, {58, 64, 72}, {150, 156, 160}, {212, 208, 202}, {104, 70, 100}, {160, 110, 120}, {190, 180, 140},
         {.30f, .58f, 1.35f}, {}, {.66f, .76f, .30f}, true, .55f, fp_darkspots, 0, false, false, 0, {}},
        // whitefish
        {.24f, .11f, .40f, .95f, {70, 84, 94}, {192, 198, 200}, {232, 232, 228}, {130, 134, 136}, {170, 170, 166}, {200, 200, 190},
         {.40f, .52f, .55f}, {}, {.64f, .75f, .30f}, true, .62f, fp_plain, 0, false, false, 0, {}},
        // burbot
        {.15f, .13f, .30f, .8f, {60, 56, 34}, {118, 108, 64}, {196, 186, 150}, {80, 72, 48}, {100, 92, 64}, {180, 160, 90},
         {.30f, .40f, .30f}, {.42f, .84f, .32f}, {.46f, .84f, .30f}, false, .42f, fp_marble, 0, true, false, 1, {{0, .10f}}},
        // tilapia
        {.40f, .14f, .40f, .95f, {60, 70, 62}, {140, 150, 134}, {202, 200, 180}, {88, 96, 90}, {156, 88, 66}, {204, 140, 60},
         {.25f, .82f, .45f}, {}, {.62f, .82f, .35f}, false, .60f, fp_bars, 0, false, false, 0, {}},
        // killifish
        {.22f, .12f, .45f, .95f, {70, 82, 70}, {172, 184, 178}, {226, 222, 206}, {192, 160, 78}, {214, 168, 66}, {220, 210, 170},
         {.60f, .74f, .45f}, {}, {.60f, .76f, .40f}, false, .55f, fp_stripe1, 0, false, false, 0, {}},
        // catfish
        {.16f, .16f, .30f, .7f, {50, 48, 40}, {96, 90, 72}, {206, 200, 180}, {66, 62, 52}, {80, 74, 60}, {170, 160, 110},
         {.30f, .38f, .60f}, {}, {.48f, .84f, .30f}, true, .45f, fp_marble, 0, true, false, 3, {{.55f, .36f}, {1.2f, .18f}, {2.2f, .14f}}},
        // barbel
        {.20f, .12f, .40f, .8f, {84, 74, 44}, {162, 140, 88}, {214, 200, 160}, {140, 94, 58}, {178, 98, 58}, {200, 170, 90},
         {.42f, .55f, .60f}, {}, {.66f, .76f, .30f}, true, .55f, fp_speckle, 0, false, false, 2, {{1.6f, .12f}, {2.0f, .10f}}},
        // mullet
        {.21f, .12f, .40f, .85f, {62, 74, 82}, {170, 176, 176}, {226, 226, 220}, {120, 124, 124}, {150, 150, 146}, {210, 196, 150},
         {.36f, .44f, .45f}, {.58f, .68f, .40f}, {.62f, .74f, .30f}, true, .62f, fp_stripes, 0, false, false, 0, {}},
        // wels: the catfish's build and barbels, darker and greener for Gipat's water
        {.16f, .16f, .30f, .7f, {44, 50, 36}, {88, 94, 66}, {198, 196, 170}, {60, 64, 48}, {74, 76, 56}, {170, 160, 110},
         {.30f, .38f, .60f}, {}, {.48f, .84f, .30f}, true, .45f, fp_marble, 0, true, false, 3, {{.55f, .36f}, {1.2f, .18f}, {2.2f, .14f}}},
        // jewel cichlid: orange-red with pale blue spots, red fins
        {.38f, .15f, .40f, .95f, {124, 62, 40}, {208, 98, 52}, {232, 150, 84}, {186, 84, 46}, {214, 66, 40}, {226, 60, 40},
         {.26f, .80f, .42f}, {}, {.62f, .82f, .34f}, false, .60f, fp_spots, 0, false, false, 0, {}, {150, 212, 222}},
        // rosy barb: rose-gold flanks, red fins
        {.30f, .12f, .42f, .95f, {104, 104, 60}, {222, 158, 104}, {238, 214, 176}, {206, 92, 52}, {228, 66, 42}, {230, 190, 60},
         {.44f, .58f, .55f}, {}, {.64f, .76f, .32f}, true, .62f, fp_plain, 0, false, false, 0, {}, {}},
        // blue panchax: blue-green with a red stripe, yellow and red fins set far back
        {.21f, .13f, .46f, .95f, {60, 104, 108}, {92, 176, 178}, {206, 226, 208}, {214, 176, 66}, {214, 78, 48}, {226, 214, 120},
         {.64f, .78f, .48f}, {}, {.58f, .78f, .44f}, false, .58f, fp_stripe1, 0, false, false, 0, {}, {206, 72, 50}},
    };
    return shapes[f < fish_count ? f : 0];
}
// The slice's layout: body rows (u along the body, v around it from the
// back) over the upper three quarters, fins below (left half plain fins,
// right half the coloured ones; root at the top of the band).
constexpr float kFishBodyRows = .75f;
constexpr float kFishPeduncle = .84f, kFishInset = .85f;

// ---------------------------------------------------------------- mesh
// Rest mesh in the critter frame (x right, y forward, z up; metres, the body
// centred on the origin). CrVertex: s the body coordinate (0 nose .. 1 tail
// tip), bone the kind (0 body, 1 median fin, 2 pectoral, 3 pelvic) with the
// fin coordinate (0 root .. 255 tip) in bits 8-15, flags the smooth normal.
std::vector<CrVertex> fish_mesh(Fish f, bool far_lod) {
    const FishShape& s = fish_shape(f);
    const float L = fish_rule(f).length, H = s.H * L / 2, W = s.W * L / 2;
    int rings = far_lod ? 6 : 12;
    const int around = far_lod ? 6 : 8, fr = far_lod ? 0 : 1;
    if (L > .3f) rings += far_lod ? 2 : 4;   // the large pike and wels are seen at twice the size: more stations for their wave
    struct Vert { V3 p; float uv[2]; float u; int kind; float ft; };
    std::vector<Vert> verts;
    std::vector<std::array<int, 3>> tris;
    // prototype axes (x forward, y left, z up) to the critter frame
    const auto add = [&](float x, float y, float z, float tu, float tv, float u, int kind, float ft) {
        verts.push_back({{-y, x, z}, {tu, tv}, u, kind, ft});
        return int(verts.size()) - 1;
    };
    const auto prof = [&](float u) {
        if (u <= s.hmax) return std::pow(std::sin(PI / 2 * u / s.hmax), .55f + .35f * s.snout);
        const float x = (u - s.hmax) / (kFishPeduncle - s.hmax);
        return .30f + .70f * (.5f + .5f * std::cos(PI * std::min(x, 1.f)));
    };
    // Hull point (y left, z up) at body coordinate u and angle a (0 back, pi belly).
    const auto surf = [&](float u, float a, float& yy, float& zz) {
        u = std::clamp(u, 0.f, kFishPeduncle);
        const float p = prof(u);
        float top = H * p, bot = H * p * s.belly, wid = W * (u < .5f ? std::pow(p, .85f) : p);
        if (s.snout > 0 && u < .18f) { top *= .75f + .25f * u / .18f; bot *= .80f + .20f * u / .18f; }
        if (s.flathead && u < .32f) { const float k = 1 - u / .32f; top *= 1 - .35f * k; wid *= 1 + .25f * k; }
        const float c = std::cos(a), sn = std::sin(a);
        zz = (c > 0 ? top : bot) * (c > 0 ? 1.f : c < 0 ? -1.f : 0.f) * std::pow(std::abs(c), .8f);
        yy = wid * (sn > 0 ? 1.f : sn < 0 ? -1.f : 0.f) * std::pow(std::abs(sn), .8f);
    };
    // stations: half uniform, half cosine (dense at nose and peduncle, even along the wave)
    std::vector<std::vector<int>> ring(rings + 1);
    for (int i = 0; i <= rings; ++i) {
        const float u = ((.5f * i / rings) + .5f * (.5f - .5f * std::cos(PI * i / rings))) * kFishPeduncle, x = (.5f - u) * L;
        if (!i) { ring[i].assign(around + 1, add(x, 0, 0, 0, .5f * kFishBodyRows, 0, 0, 0)); continue; }
        for (int j = 0; j <= around; ++j) {
            float yy, zz; surf(u, TAU * j / around, yy, zz);
            ring[i].push_back(add(x, yy, zz, u, (float(j) / around) * kFishBodyRows, u, 0, 0));
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < around; ++j) {
            const int a = ring[i][j], b = ring[i][j + 1], c = ring[i + 1][j], d = ring[i + 1][j + 1];
            if (!i) tris.push_back({a, c, d});
            else { tris.push_back({a, c, b}); tris.push_back({b, c, d}); }
        }
    {
        const int centre = add((.5f - kFishPeduncle) * L, 0, 0, kFishPeduncle, .5f * kFishBodyRows, kFishPeduncle, 0, 0);
        for (int j = 0; j < around; ++j) tris.push_back({ring[rings][j], centre, ring[rings][j + 1]});
    }
    // A fin: a grid over (sc along its root, t root to tip) from an outline returning the prototype point and u.
    const auto fin = [&](int ns, int nt, bool colour, int kind, const auto& outline) {
        std::vector<int> index(std::size_t(ns + 1) * (nt + 1));
        for (int a = 0; a <= ns; ++a)
            for (int b = 0; b <= nt; ++b) {
                const float sc = float(a) / ns, t = float(b) / nt;
                float x, y, z, u; outline(sc, t, x, y, z, u);
                index[std::size_t(a) * (nt + 1) + b] = add(x, y, z, (colour ? .5f : 0.f) + (.04f + .92f * sc) * .5f,
                    kFishBodyRows + (.02f + .96f * t) * (1 - kFishBodyRows), u, kind, t);
            }
        for (int a = 0; a < ns; ++a)
            for (int b = 0; b < nt; ++b) {
                const int p = index[std::size_t(a) * (nt + 1) + b], q = index[std::size_t(a + 1) * (nt + 1) + b];
                const int r = index[std::size_t(a) * (nt + 1) + b + 1], w = index[std::size_t(a + 1) * (nt + 1) + b + 1];
                tris.push_back({p, q, r}); tris.push_back({q, w, r});
            }
    };
    for (int k = 0; k < 2; ++k) {
        const float* d = k ? s.dorsal2 : s.dorsal;
        if (!(d[2] > 0)) continue;
        const bool spiny = !k && s.dorsal2[2] > 0;
        const float u0 = d[0], u1 = d[1], hgt = d[2] * H * 2 * .8f;
        fin(2 + fr, 1, false, 1, [&](float sc, float t, float& x, float& y, float& z, float& u) {
            const float lean = spiny ? .25f : .55f, ub = u0 + (u1 - u0) * sc;
            const float hh = spiny ? hgt * std::pow(std::sin(PI * std::min(1.f, sc * 1.1f + .05f)), .6f) : hgt * (1 - sc * .75f);
            u = ub + lean * (u1 - u0) * t * .6f;
            float yy, top; surf(ub, 0, yy, top);
            x = (.5f - u) * L; y = 0; z = top * kFishInset + (top * (1 - kFishInset) + hh) * t;
        });
    }
    fin(1 + fr, 1, true, 1, [&](float sc, float t, float& x, float& y, float& z, float& u) {
        const float u0 = s.anal[0], u1 = s.anal[1], ub = u0 + (u1 - u0) * sc, hh = s.anal[2] * H * 2 * (1 - sc * .6f);
        u = ub + .4f * (u1 - u0) * t;
        float yy, bot; surf(ub, PI, yy, bot);
        x = (.5f - u) * L; y = 0; z = bot * kFishInset + (bot * (1 - kFishInset) - hh) * t;
    });
    fin(2 + 2 * fr, 1 + fr, false, 1, [&](float sc, float t, float& x, float& y, float& z, float& u) {
        const float th = s.tail_h * H * 2, ru = kFishPeduncle - .03f, a = sc * 2 - 1;   // the root overlaps the peduncle
        float yy, edge; surf(ru, a >= 0 ? 0.f : PI, yy, edge);
        const float root_z = a * std::abs(edge) * kFishInset;
        const float reach = s.fork ? .15f + .06f * std::pow(std::abs(a), 1.5f) - .05f * std::pow(1 - std::abs(a), 3.f) : .15f - .02f * a * a;
        u = ru + reach * t;
        const float zz = root_z * (1 - t) + a * th * std::pow(t, .9f);
        // a fixed 14 degree roll of the blade away from the root: from above
        // the tail reads as a narrow wedge instead of an edge-on stick
        x = (.5f - u) * L; y = zz * .2419219f * t; z = zz * (1 - (1 - .9702957f) * t);
    });
    for (const float side : {1.f, -1.f}) {
        fin(1, 1, false, 2, [&](float sc, float t, float& x, float& y, float& z, float& u) {
            const float ur = .20f + .05f * sc;
            float ry, rz; surf(ur, side * PI * .62f, ry, rz);
            u = ur + .13f * t; x = (.5f - u) * L; y = ry * kFishInset + side * .55f * H * t; z = rz * kFishInset - .38f * H * t;
        });
        fin(1, 1, true, 3, [&](float sc, float t, float& x, float& y, float& z, float& u) {
            const float ur = .42f + .05f * sc;
            float ry, rz; surf(ur, side * PI * .85f, ry, rz);
            u = ur + .09f * t; x = (.5f - u) * L; y = ry * kFishInset + side * .22f * H * t; z = rz * kFishInset - .42f * H * t;
        });
    }
    // barbels: thin ribbons trailing back, out and down from the mouth
    for (unsigned k = 0; k < s.barbels; ++k) {
        const float ang = s.barbel[k][0], blen = s.barbel[k][1];
        for (const float side : {1.f, -1.f}) {
            if (side < 0 && !(ang > 0)) continue;
            fin(1, 1, false, 1, [&](float sc, float t, float& x, float& y, float& z, float& u) {
                float ry, rz; surf(.05f, PI - side * ang, ry, rz);
                const float w = .006f * L / .25f;
                u = .05f + blen * .55f * t;
                x = (.5f - u) * L;
                y = ry * kFishInset + side * blen * L * .45f * t * (.4f + .6f * std::sin(std::min(ang, 1.5f))) + (sc - .5f) * w;
                z = rz * kFishInset - blen * L * .35f * t + (sc - .5f) * w * .5f;
            });
        }
    }
    // Smooth normals: area weighted, body vertices joined by position (the
    // ring seam and the nose), fin vertices by index.
    std::map<std::array<long long, 3>, V3> body;
    std::vector<V3> own(verts.size());
    const auto position = [&](int i) {
        return std::array<long long, 3>{std::llround(verts[i].p.x * 1e6), std::llround(verts[i].p.y * 1e6), std::llround(verts[i].p.z * 1e6)};
    };
    for (const auto& t : tris) {
        const V3 n = cross3(verts[t[1]].p - verts[t[0]].p, verts[t[2]].p - verts[t[0]].p);
        for (const int i : t) { if (verts[i].kind) own[i] = own[i] + n; else { V3& sum = body[position(i)]; sum = sum + n; } }
    }
    std::vector<CrVertex> out;
    out.reserve(tris.size() * 3);
    for (const auto& t : tris)
        for (const int i : t) {
            const Vert& v = verts[i];
            const std::uint32_t packed = std::uint32_t(v.kind) | (std::uint32_t(std::lround(std::clamp(v.ft, 0.f, 1.f) * 255)) << 8);
            out.push_back({{v.p.x, v.p.y, v.p.z}, packed, {v.uv[0], v.uv[1]}, v.u, pack_normal(v.kind ? own[i] : body[position(i)])});
        }
    return out;
}

// ---------------------------------------------------------------- paint
// Scale grain: the luminance of the scale field on the base game's dragon
// skin flank, high-passed (divided by a wide local mean) so tiling shows
// scale cells instead of light and dark bands; values .75-1.25 around 1.
struct FishGrain { unsigned w = 0, h = 0; float scale = 1; std::vector<float> l; };
FishGrain fish_grain(const Image& dragon) {
    FishGrain g;
    const unsigned n = dragon.w, x0 = unsigned(.508f * n), x1 = unsigned(.625f * n), y0 = unsigned(.146f * n), y1 = unsigned(.586f * n);
    g.w = x1 - x0; g.h = y1 - y0; g.scale = float(n) / 1024.f;
    if (!g.w || !g.h) return FishGrain{};
    std::vector<float> l(std::size_t(g.w) * g.h);
    for (unsigned y = 0; y < g.h; ++y)
        for (unsigned x = 0; x < g.w; ++x) {
            const std::uint8_t* p = &dragon.rgb[(std::size_t(y0 + y) * dragon.w + x0 + x) * 3];
            l[std::size_t(y) * g.w + x] = (p[0] + p[1] + p[2]) / 3.f;
        }
    const auto blur = [&](const std::vector<float>& in, int step) {   // five taps, wrapping
        std::vector<float> o(in.size());
        const int w = int(g.w), h = int(g.h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto at = [&](int ax, int ay) { return in[std::size_t((ay + h) % h) * w + (ax + w) % w]; };
                o[std::size_t(y) * w + x] = (at(x, y) + at(x - step, y) + at(x + step, y) + at(x, y - step) + at(x, y + step)) / 5;
            }
        return o;
    };
    l = blur(l, 1);
    std::vector<float> low = l;
    for (int k = 0; k < 6; ++k) low = blur(low, 2);
    double mean = 0;
    for (std::size_t i = 0; i < l.size(); ++i) { l[i] /= std::max(low[i], 1e-3f); mean += l[i]; }
    mean = std::max(mean / double(l.size()), 1e-6);
    for (float& v : l) v = std::clamp(v / float(mean), .75f, 1.25f);
    g.l = std::move(l);
    return g;
}
float fish_step(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }

Image paint_fish(Fish f, const FishGrain& grain) {
    const FishShape& s = fish_shape(f);
    const float L = fish_rule(f).length;
    // painted at kPaint (128 px), as approved on the fish branch (where kSkin was 128)
    const unsigned T = kPaint, rows = unsigned(kPaint * kFishBodyRows);
    Image img; img.w = img.h = T; img.rgb.assign(std::size_t(T) * T * 3, 0);
    const std::uint64_t seed = key(fish_rule(f).name);
    // Pattern marks: centre (u, v), radii.
    struct Mark { float u, v, ru, rv; };
    std::vector<Mark> marks;
    const auto scatter = [&](unsigned count, float u0, float u1, float v0, float v1, float ru0, float ru1, float aspect) {
        for (unsigned k = 0; k < count; ++k) {
            const float r = ru0 + (ru1 - ru0) * hash01(seed, k, key("r"));
            marks.push_back({u0 + (u1 - u0) * hash01(seed, k, key("u")), v0 + (v1 - v0) * hash01(seed, k, key("v")), r, r * aspect});
        }
    };
    float falloff = 1;
    switch (s.pattern) {
    case fp_beans: scatter(150, .15f, .9f, .08f, .42f, .022f, .022f, .016f / .022f); falloff = 2.2f; break;
    case fp_spots: scatter(110, .18f, .85f, .06f, .40f, .011f, .011f, .010f / .011f); falloff = 1.8f; break;
    case fp_darkspots: scatter(40, .18f, .45f, .12f, .40f, .008f, .008f, 1); break;
    case fp_marble: scatter(160, .05f, .92f, 0, .46f, .02f, .045f, .8f); break;
    case fp_speckle: scatter(260, .05f, .92f, 0, .46f, .006f, .006f, .8f); break;
    default: break;
    }
    const float girth = PI * (s.H + s.W) / 2;   // x body length: the hull's circumference
    const float kx = 4 * L / .2f * grain.scale * 256.f / float(T), ky = kx * girth * float(T) / float(rows);
    const float eye_aspect = (float(T) / float(rows)) / girth, eye_u = f == fish_pike ? .085f : .075f;
    for (unsigned y = 0; y < rows; ++y)
        for (unsigned x = 0; x < T; ++x) {
            const float U = (x + .5f) / T, V = (y + .5f) / rows, phi = TAU * V, up = std::cos(phi), side = std::abs(std::sin(phi));
            const float t_back = fish_step(.15f, .75f, up), t_under = fish_step(-.25f, -.85f, up);
            float col[3];
            for (int k = 0; k < 3; ++k) {
                col[k] = s.flank[k] * (1 - t_back) + s.back[k] * t_back;
                col[k] = col[k] * (1 - t_under) + s.under[k] * t_under;
            }
            const auto shade = [&](float keep) { for (float& c : col) c *= keep; };
            const auto mix = [&](const float rgb[3], float g) { for (int k = 0; k < 3; ++k) col[k] = col[k] * (1 - g) + rgb[k] * g; };
            float g = 0;
            for (const Mark& m : marks)
                for (const float cv : {m.v, 1 - m.v}) {
                    const float du = (U - m.u) / m.ru, dv = (V - cv) / m.rv, d = du * du + dv * dv;
                    if (d < 12) g = std::max(g, std::exp(-d * falloff));
                }
            switch (s.pattern) {
            case fp_bars: {
                float bar = .5f + .5f * std::cos(TAU * (U * 6 * 1.25f - .15f) + .25f * std::sin(phi * 3));
                bar = fish_step(.55f, .85f, bar) * fish_step(-.35f, .25f, up) * fish_step(.18f, .30f, U) * (1 - fish_step(.78f, .9f, U));
                shade(1 - .62f * bar);
                break;
            }
            case fp_beans: { const float rgb[3]{196, 196, 140}; mix(rgb, g * fish_step(-.3f, .2f, up)); break; }
            case fp_spots: { const float pale[3]{214, 196, 160}; mix(s.accent[0] + s.accent[1] + s.accent[2] > 0 ? s.accent : pale, .8f * g * fish_step(-.4f, .1f, up)); break; }
            case fp_darkspots: shade(1 - .7f * g); break;
            case fp_marble: shade(1 - .45f * g * fish_step(-.5f, .1f, up)); break;
            case fp_speckle: shade(1 - .6f * g * fish_step(-.5f, .1f, up)); break;
            case fp_stripe1: {
                float st = 0;
                for (const float cv : {.25f, .75f}) st = std::max(st, std::exp(-(V - cv) * (V - cv) / (.022f * .022f)));
                st *= fish_step(.08f, .2f, U) * (1 - fish_step(.82f, .9f, U));
                const float blue[3]{60, 110, 160}; mix(s.accent[0] + s.accent[1] + s.accent[2] > 0 ? s.accent : blue, .5f * st);
                break;
            }
            case fp_stripes: {
                float st = 0;
                for (int k = 0; k < 5; ++k)
                    for (const float cv : {.10f + .055f * k, .90f - .055f * k}) st = std::max(st, std::exp(-(V - cv) * (V - cv) / (.010f * .010f)));
                shade(1 - .35f * st * fish_step(.12f, .25f, U) * (1 - fish_step(.80f, .88f, U)));
                break;
            }
            default: break;
            }
            // lateral line, gill cover, darker head
            const float line = (std::abs(V - .5f) - .25f + .012f * std::sin(U * 5)) / .005f;
            shade(1 - .22f * std::exp(-line * line) * fish_step(.2f, .3f, U) * (1 - fish_step(.82f, .9f, U)));
            const float c2 = std::cos(phi * 2), gill_u = f == fish_pike ? .23f + .05f * c2 * c2 : .19f + .045f * c2 * c2;
            shade(1 - .4f * std::exp(-(U - gill_u) * (U - gill_u) / (.007f * .007f)) * side);
            shade(1 - .12f * (1 - fish_step(.1f, .2f, U)));
            // scale grain, isotropic on the body: the mean over this texel's footprint
            if (!grain.l.empty()) {
                const int nx = std::max(1, int(std::lround(kx))), ny = std::max(1, int(std::lround(ky)));
                float sum = 0;
                for (int b = 0; b < ny; ++b)
                    for (int a = 0; a < nx; ++a)
                        sum += grain.l[std::size_t((unsigned(y * ky) + b) % grain.h) * grain.w + (unsigned(x * kx) + a) % grain.w];
                const float cell = sum / float(nx * ny);
                shade(1 + (cell - 1) * (.38f * fish_step(.16f, .26f, U) * (1 - fish_step(.85f, .95f, U))));
            }
            // eyes on the upper forward sides
            for (const float ev : {.25f - .055f, .75f + .055f}) {
                const float dv = (V - ev) / eye_aspect, d = std::sqrt((U - eye_u) * (U - eye_u) + dv * dv) / .022f;
                if (d < 1) mix(s.iris, .8f);
                if (d < .55f) { col[0] = 12; col[1] = 12; col[2] = 14; }
                const float hv = (V - ev + .006f * (ev < .5f ? 1.f : -1.f)) / eye_aspect;
                if (std::sqrt((U - eye_u + .006f) * (U - eye_u + .006f) + hv * hv) / .022f < .18f) { col[0] = 230; col[1] = 230; col[2] = 225; }
            }
            for (int k = 0; k < 3; ++k) img.rgb[(std::size_t(y) * T + x) * 3 + k] = std::uint8_t(std::clamp(col[k], 0.f, 255.f));
        }
    // fins: rays along the chord, darker toward the tip
    for (unsigned y = rows; y < T; ++y)
        for (unsigned x = 0; x < T; ++x) {
            const bool second = x >= T / 2;
            const float cu = ((x + .5f) / T - (second ? .5f : 0.f)) * 2, fv = (y - rows + .5f) / float(T - rows);
            const float* base = second ? s.fins2 : s.fins;
            const float rays = .5f + .5f * std::cos(TAU * cu * 14);
            const float keep = (1 - .28f * fish_step(.4f, 1, rays)) * (1 - .25f * fv);
            const float edge = s.white_edge && second ? 1 - fish_step(0, .12f, cu) : 0.f;
            const float white[3]{235, 235, 228};
            for (int k = 0; k < 3; ++k)
                img.rgb[(std::size_t(y) * T + x) * 3 + k] = std::uint8_t(std::clamp(base[k] * keep * (1 - edge) + white[k] * edge, 0.f, 255.f));
        }
    return img;
}

// ---------------------------------------------------------------- water
// The water a school swims in, on the shared 1 m vertex grid: the liquid
// surface (the shared height grid's liquid heights, carried one vertex onto
// the shore: the shared grid holds the higher ground there, which would lift
// a fish beside the bank out of the water), the swimmable depth below it (0
// on land, in lava and on falls and rapids, where the surface steps between
// vertices) and the water kind per 2 m tile.
struct WaterField {
    const float* surface = nullptr; const float* swim = nullptr; const std::uint8_t* kind = nullptr;
    std::uint32_t verts_x = 0, verts_y = 0, tiles_x = 0, tiles_y = 0;
    std::uint8_t own = water_fresh | water_swamp | water_sea;   // the water kinds that count as water (a school's own)
    // The current of level river reaches (owned_terrain_liquid_outflow: per
    // vertex the downstream direction times its strength, in 1/127); lakes,
    // sea, ponds and swamp have none.
    const std::vector<std::array<std::int8_t, 5>>* flow = nullptr;
    void current(float px, float py, float& cx, float& cy) const {
        cx = cy = 0;
        const int x = int(std::lround(px)), y = int(std::lround(py));
        if (!flow || x < 0 || y < 0 || std::uint32_t(x) >= verts_x || std::uint32_t(y) >= verts_y) return;
        const std::size_t i = std::size_t(y) * verts_x + x;
        if (i >= flow->size()) return;
        cx = float((*flow)[i][0]) * (kFishCurrent / 127.f); cy = float((*flow)[i][1]) * (kFishCurrent / 127.f);
    }
    float sample(const float* grid, float px, float py) const {
        if (!grid || verts_x < 2 || verts_y < 2) return 0;
        const float gx = std::clamp(px, 0.f, float(verts_x - 1) - .001f), gy = std::clamp(py, 0.f, float(verts_y - 1) - .001f);
        const int ix = int(gx), iy = int(gy);
        const float fx = gx - ix, fy = gy - iy;
        const float* r0 = grid + std::size_t(iy) * verts_x + ix, * r1 = r0 + verts_x;
        return (r0[0] * (1 - fx) + r0[1] * fx) * (1 - fy) + (r1[0] * (1 - fx) + r1[1] * fx) * fy;
    }
    float depth(float px, float py) const { return kind_at(px, py) & own ? sample(swim, px, py) : 0.f; }
    float level(float px, float py) const { return sample(surface, px, py); }
    std::uint8_t kind_at(float px, float py) const {
        const int tx = int(std::floor(px * .5f)), ty = int(std::floor(py * .5f));
        return !kind || tx < 0 || ty < 0 || std::uint32_t(tx) >= tiles_x || std::uint32_t(ty) >= tiles_y ? std::uint8_t(0) : kind[std::size_t(ty) * tiles_x + tx];
    }
};
// The swim depth and water kind grids of a map at its current liquid level.
void build_swim_grids(const std::vector<float>& height, const std::vector<float>& ground, const std::vector<std::uint8_t>& area,
                      const std::vector<std::uint8_t>& sea, std::uint32_t tiles_x, std::uint32_t tiles_y,
                      std::vector<float>& swim, std::vector<float>& surface, std::vector<std::uint8_t>& kind) {
    const std::uint32_t vx = tiles_x * 2 + 1, vy = tiles_y * 2 + 1;
    swim.assign(std::size_t(vx) * vy, 0.f); kind.assign(std::size_t(tiles_x) * tiles_y, 0);
    surface = height;
    if (height.size() != swim.size() || ground.size() != swim.size() || area.size() != kind.size()) return;
    for (std::size_t i = 0; i < kind.size(); ++i) {
        const unsigned a = area[i] & 15u;
        kind[i] = a == ambient::area_swamp ? water_swamp : a != ambient::area_water ? water_none : i < sea.size() && sea[i] ? water_sea : water_fresh;
    }
    std::vector<float> wet(swim.size(), 0.f);
    for (std::uint32_t y = 0; y < vy; ++y)
        for (std::uint32_t x = 0; x < vx; ++x) {
            bool water = false;
            for (int ty = (int(y) - 1) / 2; ty <= int(y) / 2 && !water; ++ty)
                for (int tx = (int(x) - 1) / 2; tx <= int(x) / 2 && !water; ++tx)
                    water = y && x && tx >= 0 && ty >= 0 && std::uint32_t(tx) < tiles_x && std::uint32_t(ty) < tiles_y && kind[std::size_t(ty) * tiles_x + tx];
            const std::size_t i = std::size_t(y) * vx + x;
            if (water && height[i] - ground[i] > .05f) wet[i] = height[i] - ground[i];
        }
    for (std::uint32_t y = 1; y + 1 < vy; ++y)
        for (std::uint32_t x = 1; x + 1 < vx; ++x) {
            const std::size_t i = std::size_t(y) * vx + x;
            if (!(wet[i] > 0)) continue;
            bool level = true;
            for (const std::size_t n : {i - 1, i + 1, i - vx, i + vx}) level = level && (!(wet[n] > 0) || std::abs(height[n] - height[i]) < .1f);
            if (level) swim[i] = wet[i];
        }
    for (std::uint32_t y = 1; y + 1 < vy; ++y)
        for (std::uint32_t x = 1; x + 1 < vx; ++x) {
            const std::size_t i = std::size_t(y) * vx + x;
            if (wet[i] > 0) continue;
            float sum = 0; unsigned count = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const std::size_t n = std::size_t(int(i) + oy * int(vx) + ox);
                    if (wet[n] > 0) { sum += height[n]; ++count; }
                }
            if (count) surface[i] = sum / float(count);
        }
}

// ---------------------------------------------------------------- schools
enum FishState : std::uint8_t { fs_calm = 0, fs_react, fs_burst, fs_regroup };
struct FishAgent {
    FishState state = fs_calm;
    float x = 0, y = 0, depth = 0, vx = 0, vy = 0, vz = 0;
    float pref = 0, wander_w = 0, wander_p = 0, wander_a = 0;   // its own depth and slow vertical wander
    float delay = 0, since = 0, bend = 0;                        // reaction delay, state time, C-start curl (left turn > 0)
    float phase = 0, amp = .085f, dart = 0, scull = 0;           // body wave (cycles), its amplitude, burst share, fin time
    // Heading (clockwise from +y; a hovering fish has no velocity to take it
    // from), the heading it turns to, its station in the school, its own pace,
    // the time to its next own move at rest and the metres left of a scoot.
    float yaw = 0, yaw_to = 0, slot_x = 0, slot_y = 0, pace = 1, act = 0, scoot = 0;
    // Forward speed along the heading (never negative), the smoothed turn rate
    // (rad/s, clockwise; bounded by the speed) and the seconds left of a slow
    // glide along an arc. vx, vy are the heading times the speed.
    float speed = 0, omega = 0, glide = 0;
    bool out = false;   // found outside its water (a changed level): dropped, never drawn
};
enum SchoolMode : std::uint8_t { sm_rest = 0, sm_cruise, sm_mill };
struct School {
    bool active = false, leaving = false;
    Fish kind = fish_roach;
    std::uint8_t count = 0;
    std::uint64_t key = 0;
    float home[2]{}, exit[2]{0, 1}, target[2]{};
    SchoolMode mode = sm_rest;
    float mode_t = 0;             // game seconds left of the calm mode
    float course = 0;             // the station's own heading while cruising (it swings to the target: an arc)
    std::uint8_t kinds = 0;       // the water kinds it lives in (its species'; any in the showcase)
    float origin[2]{};            // where it started: stations and cruise targets stay within kFishLeash of it
    std::uint32_t rolls = 0;      // random draws taken
    float quiet = 0, scattered = 0, alpha = 0, seen = 0;
    double t = 0;
    FishAgent fish[kSchoolFish];
};
constexpr float kFishFade = 1.5f;         // game seconds a school fades in or out (screen-door)
constexpr float kFishBurstSpeed = 3.f;    // m/s at most in a burst (the pike's 11 lengths/s would be 6)
constexpr float kFishFarPixels = 32.f;    // body px under which the far mesh is drawn
float fish_height(Fish f) { return fish_shape(f).H * fish_rule(f).length; }
void school_centroid(const School& s, float& x, float& y) {
    x = y = 0;
    for (unsigned i = 0; i < s.count; ++i) { x += s.fish[i].x; y += s.fish[i].y; }
    if (s.count) { x /= float(s.count); y /= float(s.count); }
}
// Deep enough for a school with room around it.
bool school_water(const WaterField& w, float x, float y, std::uint8_t kinds, float min_depth) {
    if (!(w.kind_at(x, y) & kinds) || w.depth(x, y) < min_depth) return false;
    for (const auto& o : {std::array<float, 2>{1, 0}, std::array<float, 2>{-1, 0}, std::array<float, 2>{0, 1}, std::array<float, 2>{0, -1}})
        if (w.depth(x + o[0], y + o[1]) < kFishMinDepth) return false;
    return true;
}
void school_init(School& s, Fish kind, unsigned count, float x, float y, std::uint64_t seed, const WaterField& field, std::uint8_t kinds) {
    s = School{};
    WaterField w = field; w.own = kinds;
    s.kinds = kinds; s.origin[0] = x; s.origin[1] = y;
    const FishRule& rule = fish_rule(kind);
    const float L = rule.length;
    s.active = true; s.kind = kind; s.count = std::uint8_t(std::clamp(count, 1u, kSchoolFish)); s.key = seed;
    s.home[0] = x; s.home[1] = y; s.target[0] = x; s.target[1] = y;
    s.mode = sm_rest; s.mode_t = 2 + 4 * hash01(seed, key("first rest"));
    for (unsigned i = 0; i < s.count; ++i) {
        FishAgent& a = s.fish[i];
        const float r = L * 2 * std::sqrt(-2 * std::log(std::max(hash01(seed, i, key("r")), 1e-4f))), t = TAU * hash01(seed, i, key("t"));
        a.x = x + r * std::cos(t); a.y = y + r * std::sin(t);
        if (w.depth(a.x, a.y) < kFishMinDepth) { a.x = x; a.y = y; }
        a.slot_x = a.x - x; a.slot_y = a.y - y;
        a.yaw = a.yaw_to = TAU * hash01(seed, i, key("h"));
        a.pace = .85f + .3f * hash01(seed, i, key("pace")); a.act = .5f + 5.5f * hash01(seed, i, key("act"));
        a.pref = rule.bottom ? .03f + .07f * hash01(seed, i, key("p")) : rule.depth_lo + (rule.depth_hi - rule.depth_lo) * hash01(seed, i, key("p"));
        a.wander_w = .25f + .2f * hash01(seed, i, key("ww")); a.wander_p = TAU * hash01(seed, i, key("wp"));
        a.wander_a = .04f + .05f * hash01(seed, i, key("wa"));
        a.phase = hash01(seed, i, key("ph")); a.scull = hash01(seed, i, key("sc"));
        const float half = fish_height(kind) * .5f, bed = w.depth(a.x, a.y);
        const float lo = .06f + half, hi = std::max(bed - .05f - half, lo);
        a.depth = std::clamp(rule.bottom ? bed - half - a.pref : a.pref, lo, hi);
    }
}
// One school on game time (frozen while paused). Threats are the retained
// units, by the bird flocks' law: a unit whose threat_reach covers a fish.
void school_update(School& s, float dt, const WaterField& field, const std::vector<std::array<float, 4>>& units,
                   const std::vector<GroundUnitDetail>& details) {
    if (!s.active || !(dt > 0)) return;
    WaterField w = field; w.own = s.kinds;   // only its own kind of water is water to it
    const bool slides = fish_slides(fish_rule(s.kind));
    const FishRule& rule = fish_rule(s.kind);
    const float L = rule.length, half = fish_height(s.kind) * .5f;
    const unsigned n = s.count;
    const int steps = std::clamp(int(std::ceil(dt * 30)), 1, 8);
    const float h = dt / float(steps);
    for (int step = 0; step < steps; ++step) {
        s.t += h;
        s.alpha = std::clamp(s.alpha + (s.leaving ? -h : h) / kFishFade, 0.f, 1.f);
        if (s.leaving && !(s.alpha > 0)) { s.active = false; return; }
        // The nearest unit whose reach covers a fish.
        bool threatened = false; float ux = 0, uy = 0, reach = 0, nearest = 1e9f;
        for (std::size_t k = 0; k < units.size() && k < details.size(); ++k) {
            const auto& d = details[k];
            if (d.dead) continue;
            float r = threat_reach(rule.flee_r, UnitSenses{d.visibility, d.loudness, d.pose, d.moving, d.stance == stance_attack});
            if (d.undead) r = std::max(r, 12.f);
            for (unsigned i = 0; i < n; ++i) {
                const float dist = std::hypot(s.fish[i].x - units[k][0], s.fish[i].y - units[k][1]);
                if (dist < r && dist < nearest) { nearest = dist; threatened = true; ux = units[k][0]; uy = units[k][1]; reach = r; }
            }
        }
        bool all_calm = true, all_regroup = true;
        for (unsigned i = 0; i < n; ++i) { all_calm = all_calm && s.fish[i].state == fs_calm; all_regroup = all_regroup && s.fish[i].state == fs_regroup; }
        if (threatened && (all_calm || all_regroup)) {
            float cx, cy; school_centroid(s, cx, cy);
            const float ex = cx - ux, ey = cy - uy, en = std::hypot(ex, ey) + 1e-9f;
            s.exit[0] = ex / en; s.exit[1] = ey / en;
            // regroup beyond the reach along the exit, at the nearest deep enough water
            const float wx = cx + s.exit[0] * (reach + 2.5f), wy = cy + s.exit[1] * (reach + 2.5f);
            float best = 1e9f;
            for (int oy = -12; oy <= 12; ++oy)
                for (int ox = -12; ox <= 12; ++ox) {
                    const float px = std::floor(wx) + .5f + ox, py = std::floor(wy) + .5f + oy, d2 = (px - wx) * (px - wx) + (py - wy) * (py - wy);
                    if (d2 >= best || std::hypot(px - ux, py - uy) <= reach + 1 || std::hypot(px - s.origin[0], py - s.origin[1]) > kFishLeash || !school_water(w, px, py, s.kinds, kFishMinDepth + .2f)) continue;
                    best = d2; s.home[0] = px; s.home[1] = py;
                }
            for (unsigned i = 0; i < n; ++i) {
                FishAgent& a = s.fish[i];
                a.state = fs_react; a.since = 0;
                a.delay = std::min(kFishReactCap, kFishReactK * std::hypot(a.x - ux, a.y - uy)) + kFishReactJitter * hash01(s.key, i, std::uint64_t(s.t * 1000));
            }
        }
        { unsigned left = 0; for (unsigned i = 0; i < n; ++i) left += s.fish[i].out ? 0u : 1u; if (!left) { s.active = false; return; } }
        if (threatened) { s.quiet = 0; s.scattered += h; }
        else { s.quiet += h; if (all_calm) s.scattered = 0; }
        if (s.scattered > kFishLeave) s.leaving = true;
        // Calm: the school rests on station, cruises to a new place in its
        // water (the station travels there in a straight line; the fish
        // follow it, no orbit) or mills for a while. After a scare it rests.
        const auto roll = [&](std::uint64_t what) { return hash01(s.key, ++s.rolls, what); };
        const auto rest = [&] {
            s.mode = sm_rest;
            s.mode_t = (kFishRest[0] + (kFishRest[1] - kFishRest[0]) * roll(key("rest"))) * (slides ? 3.f : rule.bottom ? 1.8f : rule.group_hi <= 2 ? 1.4f : 1.f);
            for (unsigned i = 0; i < n; ++i) { s.fish[i].slot_x = s.fish[i].x - s.home[0]; s.fish[i].slot_y = s.fish[i].y - s.home[1]; }
            // a fish that trailed far behind takes a station near the others
            for (unsigned i = 0; i < n; ++i) {
                FishAgent& a = s.fish[i];
                const float d = std::hypot(a.slot_x, a.slot_y), keep = L * 5;
                if (d > keep) { a.slot_x *= keep / d; a.slot_y *= keep / d; }
            }
        };
        if (!all_calm) { s.mode = sm_rest; s.mode_t = std::max(s.mode_t, 4.f); }
        else if (!s.leaving) {
            s.mode_t -= h;
            if (s.mode == sm_cruise) {
                const float dx = s.target[0] - s.home[0], dy = s.target[1] - s.home[1], d = std::hypot(dx, dy);
                if (d < .4f) rest();
                else {
                    const float v = fish_cruise_speed(rule), swing = v / ((slides ? 2 * kFishSlideTurn : 4.f) * L) * h * (d < 8 * L ? 4.f : 1.f), step = std::min(v * h, d);
                    s.course += std::clamp(std::remainder(std::atan2(dx, dy) - s.course, TAU), -swing, swing);
                    const float hx = s.home[0] + std::sin(s.course) * step, hy = s.home[1] + std::cos(s.course) * step;
                    // the station never leaves its water or its leash: the cruise ends there
                    if (school_water(w, hx, hy, s.kinds, kFishMinDepth + .1f) && std::hypot(hx - s.origin[0], hy - s.origin[1]) <= kFishLeash + 1) { s.home[0] = hx; s.home[1] = hy; }
                    else rest();
                }
            }
            if (s.mode_t <= 0) {
                if (s.mode != sm_rest) rest();
                else if (!slides && roll(key("mill")) < kFishMillChance) { s.mode = sm_mill; s.mode_t = kFishMill[0] + (kFishMill[1] - kFishMill[0]) * roll(key("mill time")); }
                else {
                    s.mode_t = 2;   // no way found: rest a little longer
                    for (unsigned k = 0; k < 16; ++k) {
                        const float a = TAU * roll(key("ca")), r = slides ? L * (3 + 5 * roll(key("cr"))) :   // a slide is a few body lengths
                            kFishCruiseReach[0] + (kFishCruiseReach[1] - kFishCruiseReach[0]) * roll(key("cr"));
                        const float tx = s.home[0] + r * std::cos(a), ty = s.home[1] + r * std::sin(a);
                        bool open = std::hypot(tx - s.origin[0], ty - s.origin[1]) <= kFishLeash && school_water(w, tx, ty, s.kinds, kFishMinDepth + .2f);
                        for (int q = 1; q < 6 && open; ++q)   // water all the way
                            open = w.depth(s.home[0] + (tx - s.home[0]) * q / 6.f, s.home[1] + (ty - s.home[1]) * q / 6.f) >= kFishMinDepth + .1f;
                        if (!open) continue;
                        s.target[0] = tx; s.target[1] = ty; s.mode = sm_cruise;
                        float hs = 0, hc = 0;   // it sets off the way the school faces
                        for (unsigned i = 0; i < n; ++i) { hs += std::sin(s.fish[i].yaw); hc += std::cos(s.fish[i].yaw); }
                        s.course = std::atan2(hs, hc);
                        s.mode_t = kFishCruise[0] + (kFishCruise[1] - kFishCruise[0]) * roll(key("cruise time"));
                        break;
                    }
                }
            }
        }
        const bool resting = s.mode == sm_rest;
        float ax[kSchoolFish]{}, ay[kSchoolFish]{}, az[kSchoolFish]{};
        for (unsigned i = 0; i < n; ++i) {
            FishAgent& a = s.fish[i];
            // boids in 3D: the school is a volume, not a plane
            float px = 0, py = 0, pvx = 0, pvy = 0, pd = 0, pvz = 0; unsigned mates = 0;
            for (unsigned j = 0; j < n; ++j) {
                if (j == i) continue;
                const FishAgent& b = s.fish[j];
                const float dx = b.x - a.x, dy = b.y - a.y, dz = b.depth - a.depth, d = std::sqrt(dx * dx + dy * dy + dz * dz) + 1e-9f;
                if (d < L * 6) { px += b.x; py += b.y; pvx += b.vx; pvy += b.vy; pd += b.depth; pvz += b.vz; ++mates; }
                if (d < L * 1.5f) { ax[i] -= dx / (d * d) * L * 1.2f; ay[i] -= dy / (d * d) * L * 1.2f; az[i] -= dz / (d * d) * L * .6f; }
            }
            if (mates) {
                const float k = 1.f / float(mates);
                if ((a.state == fs_calm && !resting) || a.state == fs_regroup) {
                    ax[i] += (px * k - a.x) * .8f + (pvx * k - a.vx); ay[i] += (py * k - a.y) * .8f + (pvy * k - a.vy);
                }
                az[i] += (pd * k - a.depth) * .5f + (pvz * k - a.vz) * .6f;
            }
            float awx = a.x - ux, awy = a.y - uy;
            { const float an = std::hypot(awx, awy) + 1e-9f; awx /= an; awy /= an; }
            switch (a.state) {
            case fs_react:
                a.since += h;
                if (a.since >= a.delay) {
                    a.state = fs_burst; a.since = 0;
                    float wx = awx * .5f + s.exit[0] * .5f, wy = awy * .5f + s.exit[1] * .5f;
                    { const float wn = std::hypot(wx, wy) + 1e-9f; wx /= wn; wy /= wn; }
                    // C-start: the body curls toward the new heading first
                    const float turn = std::remainder(std::atan2(wy, wx) - (PI / 2 - a.yaw), TAU);   // from its heading: a hovering fish has no velocity
                    a.bend = slides ? 0.f : std::clamp(turn / 1.6f, -1.f, 1.f);   // a slider lunges, it does not curl
                    const float v = slides ? L * kFishLunge : std::min(L * 11, kFishBurstSpeed);
                    a.vx = wx * v; a.vy = wy * v;
                }
                break;
            case fs_burst:
                a.since += h;
                ax[i] += awx * L * 25 + (s.home[0] - a.x) * .6f; ay[i] += awy * L * 25 + (s.home[1] - a.y) * .6f;
                if (a.since > kFishBurst) { a.state = fs_regroup; a.since = 0; }
                break;
            case fs_regroup:
                ax[i] += (s.home[0] - a.x) * 1.2f; ay[i] += (s.home[1] - a.y) * 1.2f;
                if (s.quiet > kFishQuiet) a.state = fs_calm;
                break;
            default:
                if (resting) {
                    // Hover: the fish holds still, its heading only drifting. Now
                    // and then, each at its own time, it makes a short scoot that
                    // curves (back toward its station when it has drifted) or,
                    // for a larger change of heading, a slow glide along an arc.
                    const float sx = s.home[0] + a.slot_x - a.x, sy = s.home[1] + a.slot_y - a.y, away = std::hypot(sx, sy);
                    a.act -= h;
                    if (!slides && a.act <= 0 && !(a.scoot > 0) && !(a.glide > 0)) {
                        a.act = 2 + 6 * roll(key("act"));
                        if (away > .3f) {   // crept off its station: back along a curve
                            a.yaw_to = std::atan2(sx, sy) + .8f * (roll(key("spread")) - .5f);
                            if (roll(key("which")) < .5f) a.scoot = std::clamp(away, .2f, .5f); else a.glide = 1.5f + 1.5f * roll(key("long"));
                        }
                        else if (roll(key("which")) < .5f) { a.yaw_to = a.yaw + 1.2f * (roll(key("by")) - .5f); a.scoot = .2f + .3f * roll(key("far")); }
                        else { a.yaw_to = a.yaw + (.6f + roll(key("by"))) * (roll(key("side")) < .5f ? -1.f : 1.f); a.glide = 1.5f + 1.5f * roll(key("long")); }
                    }
                } else {
                    const float pull = s.mode == sm_cruise ? 1.f : .25f, slot = s.mode == sm_cruise ? 1.f : 0.f;
                    ax[i] += (s.home[0] + a.slot_x * slot - a.x) * pull; ay[i] += (s.home[1] + a.slot_y * slot - a.y) * pull;
                }
                break;
            }
            if (a.state != fs_react) a.bend *= std::exp(-h / .12f);
        }
        for (unsigned i = 0; i < n; ++i) {
            FishAgent& a = s.fish[i];
            // bounds: a soft push off the shallows, a hard stop at kFishHardDepth
            const float bed = w.depth(a.x, a.y);
            float gx = (w.depth(a.x + .2f, a.y) - bed) / .2f, gy = (w.depth(a.x, a.y + .2f) - bed) / .2f;
            const float push = std::clamp((kFishMinDepth + .3f - bed) / .3f, 0.f, 4.f);
            // What the fish wants: a heading and a forward speed. It moves only
            // along its heading, and turns only as fast as its speed allows.
            const float burst = slides ? L * kFishLunge : std::min(L * 11, kFishBurstSpeed);
            const bool hover = resting && a.state == fs_calm;
            float want_speed = a.speed, want_yaw = a.yaw;
            float cx, cy; w.current(a.x, a.y, cx, cy);
            if (slides) cx = cy = 0;
            else {   // weaker with depth, and never more than the species swims against (fish_current_share)
                const float share = fish_current_share(a.depth, bed);
                cx *= share; cy *= share;
                const float c = std::hypot(cx, cy), most = fish_cruise_speed(rule) / kFishUpstream;
                if (c > most) { cx *= most / c; cy *= most / c; }
            }
            if (a.out) continue;
            if (bed < .2f) { a.out = true; continue; }   // its water is gone from under it
            if (hover) {
                const float sx = s.home[0] + a.slot_x - a.x, sy = s.home[1] + a.slot_y - a.y;
                if (std::hypot(cx, cy) > .03f) {
                    // In a current it faces upstream and holds its ground position (it swims
                    // at the current's speed); its station pulls gently, so it is never carried off.
                    const float rx = sx * .6f - cx, ry = sy * .6f - cy;
                    want_speed = std::hypot(rx, ry); want_yaw = std::atan2(rx, ry);
                } else {
                    want_speed = slides ? 0.f : kFishCreep * L;   // still water: a slow creep straight ahead (a slider lies still)
                    if (a.scoot > 0) { want_speed = L * 3; want_yaw = a.yaw_to; a.scoot -= a.speed * h + 1e-4f; }
                    else if (a.glide > 0) { want_speed = L * .8f; want_yaw = a.yaw_to; a.glide -= h; }
                }
                if (push > 0) { want_yaw = std::atan2(gx, gy); want_speed = std::max(want_speed, L * 1.2f); }   // off the shallows
            } else if (a.state != fs_react) {
                // the school law's next velocity (cohesion, alignment, station, threat, shallows) is the aim
                const float own = a.state == fs_calm ? a.pace : 1.f;
                const float top = slides ? (a.state == fs_burst ? burst : a.state == fs_regroup ? L : kFishSlide * L) :
                    a.state == fs_burst ? burst : (a.state == fs_regroup ? std::min(L * 4.5f, burst) : L * 2.6f * own) * rule.pace;
                // (a.vx, a.vy is its motion through the water; a current carries it besides)
                const float vx = a.vx + (ax[i] + gx * push * L * 30) * h, vy = a.vy + (ay[i] + gy * push * L * 30) * h, v = std::hypot(vx, vy);
                want_speed = std::clamp(v, slides ? .7f * top : L * 1.2f * rule.pace * own, top);
                if (v > 1e-6f) want_yaw = std::atan2(vx, vy);
            }
            const bool start = a.state == fs_burst && !slides;   // the C-start: a fast turn with thrust
            // Look ahead along its course: an edge of its water ahead turns it to the open
            // side (else to its station) before it gets there, at a speed that makes the turn.
            if (a.state != fs_react) {
                const float reach = std::max(2.5f * L, a.speed * .8f);
                const auto open = [&](float turn, float ahead) {
                    return w.depth(a.x + std::sin(a.yaw + turn) * ahead, a.y + std::cos(a.yaw + turn) * ahead) >= kFishHardDepth + .1f;
                };
                if (!open(0, reach) || !open(0, .5f * reach)) {
                    const bool right = open(.9f, reach), left = open(-.9f, reach);
                    want_yaw = right && !left ? a.yaw + 1.2f : left && !right ? a.yaw - 1.2f : std::atan2(s.home[0] - a.x, s.home[1] - a.y);
                    want_speed = std::max(want_speed, L);
                }
            }
            if (std::abs(std::remainder(want_yaw - a.yaw, TAU)) > .05f) want_speed = std::max(want_speed, kFishTurnSpeed * L);   // a turn needs way on
            if (start) a.speed = want_speed;
            else if (want_speed > a.speed) a.speed = std::min(want_speed, a.speed + L * 10 * h);
            else a.speed = std::max(want_speed, a.speed * std::exp(-2 * h));
            const float cap = start ? kFishStartTurn : a.speed / ((slides ? kFishSlideTurn : kFishTurnLengths) * L);   // no speed, no turn
            const float rate = std::clamp((start ? 20.f : 3.f) * std::remainder(want_yaw - a.yaw, TAU), -cap, cap);
            a.omega = std::clamp(a.omega + (rate - a.omega) * std::min(1.f, h * (start ? 30.f : 6.f)), -cap, cap);
            a.yaw += a.omega * h;
            a.vx = std::sin(a.yaw) * a.speed; a.vy = std::cos(a.yaw) * a.speed;
            float speed = a.speed;
            const float nx = a.x + (a.vx + cx) * h, ny = a.y + (a.vy + cy) * h;
            if (w.depth(nx, ny) >= kFishHardDepth || bed < kFishHardDepth) { a.x = nx; a.y = ny; }
            else a.speed *= .5f;   // never into the shallows: it holds this step (the look-ahead is turning it)
            // vertical: a spring toward its own wandering depth, clamped to the water column; a leaving school dives
            const float here = w.depth(a.x, a.y), lo = .06f + half, hi = std::max(here - .05f - half, lo);
            const bool fleeing = a.state == fs_burst || a.state == fs_regroup;
            const float target = s.leaving ? hi : std::clamp((rule.bottom ? here - half - a.pref : a.pref) +
                a.wander_a * std::sin(a.wander_w * float(s.t) + a.wander_p) + (fleeing ? .15f : 0.f), lo, hi);
            a.vz = std::clamp(a.vz + (az[i] + (target - a.depth) * 2 - a.vz * 2.2f) * h, -L * 1.5f, L * 1.5f);
            a.depth = std::clamp(a.depth + a.vz * h, lo, hi);
            if (slides) { a.depth = std::max(here - half - kFishBedGap, lo); a.vz = 0; }   // always on the bed
            // the wave follows the speed: a slow swim beat up to the burst's fast, wide one
            // the wave follows the speed: a tail flicker at rest, a swim beat, the burst's fast wide one
            if (slides) {
                // a long slow wave that reaches the head (the instance's eel share); lying still, a twitch now and then
                const float lps = speed / L, swim = std::min(lps / kFishSlide, 1.f), lunge = std::clamp((lps - kFishSlide) / 2, 0.f, 1.f);
                const bool still = speed < .05f * L;
                const float twitch = std::pow(std::max(0.f, std::sin(TAU * (float(s.t) / 3.1f + a.wander_p))), 12.f);
                a.dart = 0; a.amp = still ? .012f * twitch : .10f * swim + .03f * lunge;
                a.phase += (still ? 3 * twitch : .6f + lunge) * h; a.phase -= std::floor(a.phase);
            } else {
                const float lengths = speed / (L * rule.pace), swim = fish_step(.2f, 1.6f, lengths);
                a.dart = fish_step(3, 9, lengths);
                a.amp = .015f + .07f * swim + .035f * a.dart;
                a.phase += (.9f + 1.5f * swim + 3.6f * a.dart) * rule.pace * h; a.phase -= std::floor(a.phase);
            }
            a.scull += h; a.scull -= std::floor(a.scull);
        }
    }
}
