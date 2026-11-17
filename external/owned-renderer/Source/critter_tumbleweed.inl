// Tumbleweeds of the Suslanger desert (issue #251, foliage lookdev rounds 3-4:
// claude/foliage-lookdev-3 Prototypes/foliage-lookdev tumble3.py mesh, tumble4.py
// low count and one-way pushes). Included by renderer_critters.cpp inside its
// unnamed namespace; drawn by the critter pass (kCritterShader CR_CUTOUT).
//
// Look: a round, airy ball of dry twigs. Cards of the base game's tree04 palm
// atlas twig art lie tangent to the ball, a few larger ones cross the core and
// thin stems of its pale stem art run along great circles; slightly squashed.
// The art is dried (saturation .55, multiplied straw-grey) once into a critter
// skin slice, alpha kept for the cutout.
//
// Motion, on game time (frozen while paused, faster at fast speed): the
// renderer's wind (weather_wind: the map's wind, its spells and storms, the
// grass's travelling gust fronts) drags the ball against rolling resistance
// in the sand and pulls it down slopes; it rolls without slipping, bounces off
// objects and sits still in their lee. It rolls on any open walkable ground,
// not only sand. Units push it, one way: a unit walking into a ball kicks it
// away with a capped hop under gravity, damped bounces, and is never moved.
//
// Count: at most one free-rolling ball (two in a sand storm), which arrives
// from upwind off screen; otherwise the second one rests in the lee of an
// object. Both are born off screen and leave off screen: out of sight a few
// seconds away from the party, a ball goes and another comes in elsewhere
// (like the critters), while one in view stays at any distance (farther than
// critters). One that has to go while in view (setting off) fades out (the
// critters' screen-door dither), so none pops. A sand storm blows harder on
// the ground and sends rollers more often.

// ---------------------------------------------------------------- mesh and skin
constexpr float kTumbleRadius = .32f;                  // tumble3 make_tumbleweed radius (m)
constexpr float kTumbleRoll = kTumbleRadius * .91f;    // mean radius of the squashed ball: its rolling radius
// tree04 atlas crops in pixels of the 512 px base-game atlas (r3.py T4) and
// where they sit on the tumbleweed slice (kSkin px), apart for mip bleeding.
struct TumbleCrop { int x0, y0, x1, y1, sx, sy; };
constexpr TumbleCrop kTumbleTwig{331, 230, 445, 365, 0, 0}, kTumbleTwig2{331, 368, 445, 470, 0, 144},
                     kTumblePale{455, 285, 510, 505, 128, 0};

V3 tw_unit(V3 v) { const float l = len3(v); return l > 1e-9f ? v * (1 / l) : V3{0, 0, 1}; }
struct TumbleRng {
    std::uint64_t s;
    float r(float a, float b) { s = splitmix(s); return a + (b - a) * float(s >> 40) / 16777216.f; }
};
std::array<float, 4> tumble_uv(const TumbleCrop& c) {
    const float h = .5f / float(kSkin);   // half a texel inside the crop
    return {float(c.sx) / kSkin + h, float(c.sy) / kSkin + h,
            float(c.sx + c.x1 - c.x0) / kSkin - h, float(c.sy + c.y1 - c.y0) / kSkin - h};
}

// The near mesh of tumble3 make_tumbleweed: 30 tangent and 8 core cards, 22 arc stems (472 triangles).
std::vector<CrVertex> tumbleweed_mesh(std::uint64_t seed) {
    std::vector<CrVertex> out;
    TumbleRng r{splitmix(seed)};
    const V3 squash{1.f, .93f, .84f};
    const auto put = [&](V3 p, V3 n, float u, float v, float shade) {
        out.push_back({{p.x * squash.x, p.y * squash.y, p.z * squash.z}, 0, {u, v}, shade, pack_normal(n)});
    };
    // r3.py card: bottom edge centred at base, rising along up; normals lean .65 toward the radial direction.
    const auto card = [&](V3 base, V3 up, V3 side, float w, float h, const TumbleCrop& crop, float tone) {
        up = tw_unit(up); side = tw_unit(side - up * dot3(side, up));
        const V3 normal = tw_unit(cross3(side, up));
        const auto uv = tumble_uv(crop);
        V3 p[4]; float u[4], v[4], ao[4]; V3 n[4];
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < 2; ++k) {
                const int i = row * 2 + k;
                p[i] = base + up * (h * float(row)) + side * ((k ? .5f : -.5f) * w);
                n[i] = tw_unit(normal * .35f + tw_unit(p[i]) * .65f);
                u[i] = k ? uv[2] : uv[0]; v[i] = row ? uv[1] : uv[3];
                ao[i] = .8f + .15f * float(row);
            }
        for (const int i : {0, 1, 3, 0, 3, 2}) put(p[i], n[i], u[i], v[i], tone * ao[i]);
    };
    // r3.py stem: a tapered three-sided tube along pts, the pale crop wrapped once around.
    const auto stem = [&](const std::array<V3, 4>& pts, float r0, float r1, float tone) {
        const auto uv = tumble_uv(kTumblePale);
        constexpr int sides = 3;
        float length = 1e-9f;
        for (int i = 0; i < 3; ++i) length += len3(pts[i + 1] - pts[i]);
        std::array<std::array<V3, sides + 1>, 4> ring{}, normal{};
        std::array<float, 4> t{};
        float along = 0;
        for (int s = 0; s < 4; ++s) {
            if (s) along += len3(pts[s] - pts[s - 1]);
            const V3 d = tw_unit(pts[std::min(s + 1, 3)] - pts[s ? s - 1 : 0]);
            V3 c = cross3(d, {0, 0, 1});
            if (dot3(c, c) < 1e-6f) c = cross3(d, {1, 0, 0});
            const V3 sd = tw_unit(c), upv = cross3(sd, d);
            t[s] = along / length;
            for (int k = 0; k <= sides; ++k) {
                const float a = float(k % sides) * TAU / sides;
                normal[s][k] = sd * std::cos(a) + upv * std::sin(a);
                ring[s][k] = pts[s] + normal[s][k] * (r0 + (r1 - r0) * t[s]);
            }
        }
        const auto corner = [&](int s, int k) {
            put(ring[s][k], normal[s][k], uv[0] + (uv[2] - uv[0]) * float(k) / sides, uv[3] + (uv[1] - uv[3]) * t[s], tone * (.7f + .3f * t[s]));
        };
        for (int s = 0; s < 3; ++s)
            for (int k = 0; k < sides; ++k) {
                corner(s, k); corner(s, k + 1); corner(s + 1, k);
                corner(s, k + 1); corner(s + 1, k + 1); corner(s + 1, k);
            }
    };
    const auto aspect = [](const TumbleCrop& c) { return float(c.x1 - c.x0) / float(c.y1 - c.y0); };
    constexpr int shell = 30, core = 8, arcs = 22;
    const float R = kTumbleRadius;
    for (int k = 0; k < shell + core; ++k) {
        const float u = r.r(-1, 1), ph = r.r(0, TAU), rr = std::sqrt(std::max(0.f, 1 - u * u));
        const V3 nrm{rr * std::cos(ph), rr * std::sin(ph), u};
        const V3 a = tw_unit(cross3(nrm, {r.r(-1, 1), r.r(-1, 1), r.r(-1, 1)})), b = cross3(nrm, a);
        const TumbleCrop& crop = k % 2 ? kTumbleTwig : kTumbleTwig2;
        float s; V3 c, up = b, side = a;
        if (k < shell) { s = R * r.r(.75f, 1.f); c = nrm * (R * r.r(.68f, .92f)); }
        else { s = R * 1.7f * r.r(.9f, 1.1f); c = nrm * (R * r.r(0, .2f)); side = nrm; }
        card(c - up * (s * .5f), up, side, s * aspect(crop), s, crop, r.r(.82f, 1.08f));
    }
    for (int k = 0; k < arcs; ++k) {
        const V3 n0 = tw_unit({r.r(-1, 1), r.r(-1, 1), r.r(-1, 1)});
        const V3 t0 = tw_unit(cross3(n0, {r.r(-1, 1), r.r(-1, 1), r.r(-1, 1)})), t1 = cross3(n0, t0);
        const float span = r.r(1.4f, 2.6f), a0 = r.r(0, TAU), rad = R * r.r(.78f, 1.f);
        std::array<V3, 4> pts{};
        for (int i = 0; i < 4; ++i) {
            const float t = float(i) / 3;
            pts[i] = (t0 * std::cos(a0 + span * t) + t1 * std::sin(a0 + span * t)) * (rad * (1 + .08f * std::sin(t * 7 + float(k))));
        }
        stem(pts, .006f, .003f, r.r(.85f, 1.05f));
    }
    return out;
}

// The tumbleweed slice: the three tree04 crops dried (lookdev DRY_SAT .55 toward
// luminance, DRY_MUL 1.02/.93/.78) with their cutout. A cut texel takes its
// crop's mean colour so the generated mips do not darken the twig edges.
bool tumbleweed_skin(const TextureCpuAsset& atlas, std::uint8_t* slice, std::string& why) {
    Image img;
    if (!decode_skin_texture(atlas, img) || img.w < 128 || img.h < 128) { why = "tree04: unsupported format"; return false; }
    TextureAlphaPixels alpha;
    if (!texture_alpha_pixels(atlas, 0, alpha)) { why = "tree04: no alpha"; return false; }
    std::fill(slice, slice + std::size_t(kSkin) * kSkin * 4, std::uint8_t(0));
    for (const TumbleCrop& c : {kTumbleTwig, kTumbleTwig2, kTumblePale}) {
        const int w = c.x1 - c.x0, h = c.y1 - c.y0;
        double sum[3]{}; unsigned kept = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::uint32_t ax = std::min(img.w - 1, std::uint32_t((c.x0 + x + .5f) * img.w / 512)),
                                        ay = std::min(img.h - 1, std::uint32_t((c.y0 + y + .5f) * img.h / 512));
                    const std::uint8_t* s = &img.rgb[(std::size_t(ay) * img.w + ax) * 3];
                    const bool cut = texture_alpha_texel_cut(alpha, ax * alpha.width / img.w, ay * alpha.height / img.h);
                    const float lum = .3f * s[0] + .59f * s[1] + .11f * s[2];
                    constexpr float mul[3]{1.02f, .93f, .78f};
                    float rgb[3];
                    for (int k = 0; k < 3; ++k) rgb[k] = std::clamp((lum * .45f + s[k] * .55f) * mul[k], 0.f, 255.f);
                    if (pass == 0) { if (!cut) { for (int k = 0; k < 3; ++k) sum[k] += rgb[k]; ++kept; } continue; }
                    std::uint8_t* o = slice + (std::size_t(c.sy + y) * kSkin + c.sx + x) * 4;
                    for (int k = 0; k < 3; ++k) o[k] = std::uint8_t(cut ? sum[k] / std::max(kept, 1u) : rgb[k]);
                    o[3] = cut ? 0 : 255;
                }
        if (!kept) { why = "tree04: twig crops fully transparent (not the base-game atlas layout)"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------- simulation
// Wind on the ground (m/s) per unit of windiness (wind_trails::windiness: the
// map's effective force times the spell/storm speed and gust pulse); the
// grass's gust fronts carry it between 55 % and 100 %.
constexpr float kTumbleWind = 9.f, kTumbleWindMax = 7.f;
// A sand storm (renderer_precipitation: its visible share, 0..1) blows harder
// on the ground than the shared wind's speed alone says (precip_test 3 forces
// the storm without touching the wind): up to +80 % ground wind, cap 10 m/s.
constexpr float kTumbleStormWind = .8f, kTumbleStormMax = 10.f;
constexpr float kTumbleRollWind = 1.6f;   // steady wind (m/s) below which no new ball is sent rolling
constexpr float kTumbleFadeIn = 1.f, kTumbleFadeOut = 1.5f;   // game seconds
constexpr float kTumbleRolling = .3f;     // m/s: a ball this fast counts as rolling
constexpr float kTumbleGone = 70.f;       // metres from the party focus: an off-screen ball is dropped
// Out of sight: a ball off screen this long (game seconds) and at least
// kTumbleKeep m from the party is dropped and another comes in elsewhere,
// like the critters; in view a ball stays at any distance (critters leave
// at twice the party's vision), the fog takes it.
constexpr float kTumbleUnseen = 4.f, kTumbleKeep = 12.f;
constexpr float kTumbleUnseenNew = 25.f;  // a ball never yet seen (on its way in, or resting off screen) gets longer
constexpr float kTumbleRespawn = 4.f;     // game seconds after a drop before the next ball may come
// A unit's kick: unit speed from its smoothed motion (game positions step
// at the game's tick, never per frame), at most a run; the ball leaves
// at most kTumbleKickMax over the ground and kTumbleHopMax upward.
constexpr float kTumbleUnitMax = 6.f, kTumbleUnitSmooth = .15f;   // m/s; seconds
constexpr float kTumbleKickMax = 6.f, kTumbleHopMax = 3.f, kTumbleSpeedMax = 9.f;
constexpr unsigned kTumbleSlots = 2;

struct Tumble {
    bool active = false, leaving = false;
    float x = 0, y = 0, h = 0;          // ground point under the centre; height of the ball above the ground
    float vx = 0, vy = 0, vz = 0;
    float wx = 0, wy = 0, wz = 0;       // spin (rad/s): rolling on the ground, kept in the air
    M3 rot;
    float scale = 1, alpha = 0, unseen = 0, peak = 0;   // unseen: game seconds off screen; peak: highest hop since the last push
    bool seen = false;                  // on screen at least once since it came
    double kicked = -1e9;
};
struct TumbleUnit { std::uint64_t id; float x, y, vx, vy; };
struct TumbleWeeds {
    Tumble weed[kTumbleSlots];
    double clock = 0, next_roll = 0, next_rest = 0, next_report = 0;
    std::uint64_t seed = 0, born = 0;
    std::vector<TumbleUnit> last_units, units_now;     // by the retained unit identity: position, smoothed velocity
    std::vector<std::array<float, 2>> unit_velocity;   // per unit row of this tick (m/s)
    bool showcase_done = false;
    // diagnostics (status line)
    unsigned pushes = 0, drops = 0, spawns = 0, bumps = 0; double last_bump = -1e9; float wind = 0, windiness = 0, storm = 0;
};

// Where a ball may roll: any open walkable ground (sand, grass, rock alike),
// never water, swamp or a solid object. Spawns keep to the dry ground.
bool tumble_roll_ground(const WorldView& w, float x, float y) {
    const auto a = w.area_at(x, y);
    return a != 0xff && !((kWater | kSwamp) >> a & 1u) && w.walkable(x, y) && !w.solid_at(x, y);
}
bool tumble_ground(const WorldView& w, float x, float y) {
    const auto a = w.area_at(x, y);
    return a != 0xff && ((kSand | kBare | kDry) >> a & 1u) && w.walkable(x, y) && !w.solid_at(x, y);
}
// Shelter from the wind 0..1: an object's footprint just upwind (its
// trunk for a tree, its turned box otherwise: solid_at). The obstacle grid's
// 2 m cells are no measure here: in an oasis nearly every cell holds a palm
// or a bush, which held every ball in a full lee.
float tumble_shelter(const WorldView& w, float x, float y, const float dir[2]) {
    float shelter = 0;
    for (const float k : {.8f, 1.6f, 2.6f})
        if (w.solid_at(x - dir[0] * k, y - dir[1] * k)) shelter = std::max(shelter, 1 - (k - .8f) / 2.6f);
    return shelter;
}
bool tumble_offscreen(const WorldView& w, float x, float y) {
    float nx, ny, ppm;
    if (!w.project({x, y, w.ground(x, y) + kTumbleRoll}, nx, ny, ppm)) return true;
    return std::abs(nx) > 1.15f || std::abs(ny) > 1.15f;
}
void tumble_start(TumbleWeeds& t, Tumble& b, float x, float y, float alpha) {
    const std::uint64_t k = splitmix(t.seed ^ ++t.born);
    b = Tumble{};
    b.active = true; b.x = x; b.y = y; b.alpha = alpha;
    b.scale = .9f + .25f * hash01(k, 1);
    b.rot = rot_z(TAU * hash01(k, 2)) * rot_x(TAU * hash01(k, 3)) * rot_y(TAU * hash01(k, 4));
}

// Balls against each other (equal masses): no interpenetration (each moves
// half the overlap out along the contact normal, in 3D so one can ride up on
// another), the closing speed exchanged with restitution kTumbleBump and the
// sliding speed damped by friction kTumbleRub. A ball dead on top of another
// slides off to the side instead of balancing there.
constexpr float kTumbleBump = .3f, kTumbleRub = .3f, kTumbleContact = .92f;   // contact radius per rolling radius
void tumble_collide(TumbleWeeds& t, const WorldView& w, const bool* live, bool showcase) {
    for (unsigned i = 0; i < kTumbleSlots; ++i)
        for (unsigned j = i + 1; j < kTumbleSlots; ++j) {
            if (!live[i] || !live[j]) continue;
            Tumble& a = t.weed[i];
            Tumble& b = t.weed[j];
            const float ra = kTumbleContact * kTumbleRoll * a.scale, rb = kTumbleContact * kTumbleRoll * b.scale;
            const V3 pa{a.x, a.y, w.ground(a.x, a.y) + kTumbleRoll * a.scale + a.h}, pb{b.x, b.y, w.ground(b.x, b.y) + kTumbleRoll * b.scale + b.h};
            V3 d = pb - pa;
            const float dist = len3(d), reach = ra + rb;
            if (!(dist < reach)) continue;
            if (std::hypot(d.x, d.y) < .05f * reach) d = V3{i == 0 ? .05f : -.05f, .05f, d.z};   // dead on top: off to a side
            const V3 n = tw_unit(d);
            const float push = .5f * (reach - dist);
            a.x -= n.x * push; a.y -= n.y * push; a.h = std::max(0.f, a.h - n.z * push);
            b.x += n.x * push; b.y += n.y * push; b.h = std::max(0.f, b.h + n.z * push);
            const V3 rel{b.vx - a.vx, b.vy - a.vy, b.vz - a.vz};
            const float closing = dot3(rel, n);
            if (closing >= 0) continue;
            const float impulse = -.5f * (1 + kTumbleBump) * closing;
            V3 slide = rel - n * closing;
            const float slide_len = len3(slide);
            const float rub = slide_len > 1e-5f ? std::min(kTumbleRub * impulse, .5f * slide_len) / slide_len : 0.f;
            slide = slide * rub;
            a.vx -= n.x * impulse - slide.x; a.vy -= n.y * impulse - slide.y; a.vz -= n.z * impulse - slide.z;
            b.vx += n.x * impulse - slide.x; b.vy += n.y * impulse - slide.y; b.vz += n.z * impulse - slide.z;
            if (showcase && t.clock - t.last_bump > .5) {
                t.last_bump = t.clock;
                char line[112];
                std::snprintf(line, sizeof(line), "balls at %.1f,%.1f and %.1f,%.1f closing %.1f m/s, overlap %.2f m resolved", a.x, a.y, b.x, b.y, -closing, 2 * push);
                bridge_append_probe_event("tumbleweed_bump", line);
            }
            ++t.bumps;
        }
}

// One game-time step of the tumbleweeds. units: the frame's unit rows with
// their details; wind the frame's renderer wind; storm the visible sand storm
// (0..1); enabled false (setting off, not Suslanger) sends every ball away.
void tumble_tick(TumbleWeeds& t, float dt, const WorldView& w, const WeatherWindGpu& wind, float storm, bool enabled, bool showcase) {
    t.clock += dt;
    const float dir[2]{wind.direction[0], wind.direction[1]};
    t.windiness = wind_trails::windiness(wind);
    t.storm = std::clamp(storm, 0.f, 1.f);
    const float base = std::min(kTumbleWindMax + (kTumbleStormMax - kTumbleWindMax) * t.storm,
                                kTumbleWind * t.windiness * (1 + kTumbleStormWind * t.storm));
    t.wind = base;
    if (!(dt > 0)) return;
    // Unit velocities by the retained identity (never an address), smoothed
    // over kTumbleUnitSmooth: positions advance on the game's ticks, so a
    // per-frame difference alternates zero and spikes (a spike was the
    // "launched into the sky" kick).
    const std::size_t unit_rows = w.units ? w.units->size() : 0;
    const float follow = 1 - std::exp(-dt / kTumbleUnitSmooth);
    t.units_now.clear(); t.unit_velocity.assign(unit_rows, {0.f, 0.f});
    for (std::size_t i = 0; i < unit_rows; ++i) {
        if ((*w.details)[i].dead) continue;
        TumbleUnit now{(*w.details)[i].id, (*w.units)[i][0], (*w.units)[i][1], 0, 0};
        for (const TumbleUnit& u : t.last_units) {
            if (u.id != now.id) continue;
            if (std::hypot(now.x - u.x, now.y - u.y) > 2) break;   // a teleport is not a kick: velocity restarts at 0
            const float jx = (now.x - u.x) / dt, jy = (now.y - u.y) / dt;
            now.vx = u.vx + (jx - u.vx) * follow; now.vy = u.vy + (jy - u.vy) * follow;
            const float s = std::hypot(now.vx, now.vy);
            if (s > kTumbleUnitMax) { now.vx *= kTumbleUnitMax / s; now.vy *= kTumbleUnitMax / s; }
            break;
        }
        t.unit_velocity[i] = {now.vx, now.vy};
        t.units_now.push_back(now);
    }
    t.last_units.swap(t.units_now);

    // Per ball once a tick: leaving, fading and its wind; then all balls step
    // together so they can collide with each other.
    float wind_at[kTumbleSlots]{};
    bool live[kTumbleSlots]{};
    for (unsigned i = 0; i < kTumbleSlots; ++i) {
        Tumble& b = t.weed[i];
        if (!b.active) continue;
        // Out of sight (off screen a while, away from the party) or too far:
        // it goes off screen and another comes in elsewhere; in view it
        // only fades when the setting goes off or it leaves the zone.
        const float away = std::hypot(b.x - w.focus.x, b.y - w.focus.y);
        const bool offscreen = tumble_offscreen(w, b.x, b.y);
        b.unseen = offscreen ? b.unseen + dt : 0.f;
        b.seen = b.seen || !offscreen;
        const bool lost = b.seen ? b.unseen > kTumbleUnseen && away > kTumbleKeep : b.unseen > kTumbleUnseenNew;
        if (!enabled) b.leaving = true;
        if (offscreen && (b.leaving || away > kTumbleGone || lost)) {
            b.active = false; ++t.drops;
            t.next_roll = std::min(t.next_roll, t.clock + kTumbleRespawn);
            t.next_rest = std::min(t.next_rest, t.clock + 2 * kTumbleRespawn);
            if (showcase) {
                char line[128];
                std::snprintf(line, sizeof(line), "out of sight at %.1f,%.1f (%.0f m from the party, unseen %.1f s%s)", b.x, b.y, away, b.unseen,
                              b.seen ? "" : ", never seen");
                bridge_append_probe_event("tumbleweed_drop", line);
            }
            continue;
        }
        b.alpha = b.leaving ? b.alpha - dt / kTumbleFadeOut : std::min(1.f, b.alpha + dt / kTumbleFadeIn);
        if (b.alpha <= 0) { b.active = false; continue; }
        // Shelter holds a resting ball in the lee against the steady wind,
        // but never fully: strong gusts still roll it out, a storm sooner.
        const float shelter = .8f * tumble_shelter(w, b.x, b.y, dir) * (1 - .5f * t.storm);
        const float gust = .55f + .45f * wind_trails::gust_front(b.x, b.y, wind, wind.seconds);
        wind_at[i] = base * gust * (1 - shelter);
        live[i] = true;
    }
    // Fixed 60 Hz steps (tumble4 step): wind drag, rolling resistance, slope,
    // units, objects, ground bounce; then the balls against each other.
    const int steps = std::clamp(int(std::ceil(dt * 60)), 1, 16);
    const float h = dt / float(steps);
    for (int s = 0; s < steps; ++s) {
        for (unsigned i = 0; i < kTumbleSlots; ++i) {
            if (!live[i]) continue;
            Tumble& b = t.weed[i];
            const float W = wind_at[i], r = kTumbleRoll * b.scale;
            float rx = W * dir[0] - b.vx, ry = W * dir[1] - b.vy;
            const float rel = std::hypot(rx, ry);
            float ax = .8f * rx * rel, ay = .8f * ry * rel, az = -9.81f;
            const bool on = b.h <= .01f;
            if (on) {
                const float sp = std::hypot(b.vx, b.vy);
                if (sp > 1e-4f) { const float stop = std::min(.7f, sp / h); ax -= b.vx / sp * stop; ay -= b.vy / sp * stop; }
                // down the slope: a rolling ball takes ~.7 of g along the ground's gradient
                const float gx = (w.ground(b.x + .25f, b.y) - w.ground(b.x - .25f, b.y)) * 2, gy = (w.ground(b.x, b.y + .25f) - w.ground(b.x, b.y - .25f)) * 2;
                ax -= 6.9f * gx; ay -= 6.9f * gy;
            }
            b.vx += ax * h; b.vy += ay * h; b.vz += az * h;
            // Units push it, one way: the ball leaves the body (the liquid
            // ripples' and grass's footprint, envelope x .35) and is kicked
            // up and away; the unit is never moved.
            for (std::size_t i = 0; w.units && i < w.units->size(); ++i) {
                const auto& u = (*w.units)[i];
                if ((*w.details)[i].dead) continue;
                const float body = u[3] > 0 ? std::clamp(u[3] * .35f, .3f, 2.5f) : .5f;
                const float dx = b.x - u[0], dy = b.y - u[1], d = std::hypot(dx, dy), reach = body + r;
                if (d >= reach || b.h > 1.5f) continue;
                const float nx = d > 1e-5f ? dx / d : 1.f, ny = d > 1e-5f ? dy / d : 0.f;
                b.x = u[0] + nx * reach; b.y = u[1] + ny * reach;
                const float ux = t.unit_velocity[i][0], uy = t.unit_velocity[i][1];
                const float vn = (b.vx - ux) * nx + (b.vy - uy) * ny;
                if (vn < 0) {
                    // The ball leaves along the contact normal a little faster
                    // than the unit (at most kTumbleKickMax over the ground)
                    // and hops (at most kTumbleHopMax up: about knee high).
                    const float us = std::hypot(ux, uy);
                    const float kick = std::min(kTumbleKickMax, -vn * 1.1f + 1.2f * us + .8f);
                    b.vx += nx * kick; b.vy += ny * kick;
                    b.vz = std::max(b.vz, std::min(kTumbleHopMax, .9f + .35f * us));
                    if (t.clock - b.kicked > .5) {
                        ++t.pushes; b.peak = 0;
                        if (showcase) {   // the diagnostic names each push
                            char line[112];
                            std::snprintf(line, sizeof(line), "at %.1f,%.1f unit speed %.1f m/s kick %.1f m/s hop %.1f m/s", b.x, b.y, us, kick, b.vz);
                            bridge_append_probe_event("tumbleweed_push", line);
                        }
                    }
                    b.kicked = t.clock;
                }
            }
            // Whatever pushed it, a light ball never outruns kTumbleSpeedMax.
            const float sp = std::hypot(b.vx, b.vy);
            if (sp > kTumbleSpeedMax) { b.vx *= kTumbleSpeedMax / sp; b.vy *= kTumbleSpeedMax / sp; }
            b.vz = std::min(b.vz, kTumbleHopMax);
            // Objects, water and cliffs: it bounces back off them.
            float px = b.x + b.vx * h, py = b.y + b.vy * h;
            // Blocked on one axis, it glances off toward the open side (a bush
            // or rock straight downwind no longer pins it for good).
            const auto glance = [&](float along, bool x_axis) {
                const float lx = x_axis ? 0.f : r + .2f, ly = x_axis ? r + .2f : 0.f;
                const bool plus = tumble_roll_ground(w, b.x + lx, b.y + ly), minus = tumble_roll_ground(w, b.x - lx, b.y - ly);
                if (plus == minus && !plus) return 0.f;
                const float side = plus && minus ? ((x_axis ? b.vy : b.vx) >= 0 ? 1.f : -1.f) : plus ? 1.f : -1.f;
                return side * .6f * std::abs(along);
            };
            // A ball already inside an object's footprint (kicked or bumped in,
            // or born there) is never held: it rolls on until it is out.
            // (Probe 2026-11-15: every stuck ball was blocked on both axes.)
            const bool free = tumble_roll_ground(w, b.x, b.y);
            if (free && !tumble_roll_ground(w, px + (b.vx > 0 ? r : -r), b.y)) { b.vy += glance(b.vx, true); b.vx = -b.vx * .4f; px = b.x; }
            if (free && !tumble_roll_ground(w, b.x, py + (b.vy > 0 ? r : -r))) { b.vx += glance(b.vy, false); b.vy = -b.vy * .4f; py = b.y; }
            b.x = px; b.y = py;
            b.h += b.vz * h;
            b.peak = std::max(b.peak, b.h);
            // Landing: a springy twig ball keeps about a third of its fall as
            // a smaller hop and loses a fifth of its ground speed per bounce;
            // slow landings settle.
            if (b.h < 0) {
                const bool bounce = b.vz < -.8f;
                b.h = 0; b.vz = bounce ? -b.vz * .35f : 0.f;
                if (bounce) { b.vx *= .8f; b.vy *= .8f; }
            }
            // Rolls without slipping on the ground; keeps its spin in the air.
            if (b.h <= .01f) { b.wx = -b.vy / r; b.wy = b.vx / r; b.wz = 0; }
            const float spin = std::sqrt(b.wx * b.wx + b.wy * b.wy + b.wz * b.wz), angle = spin * h;
            if (angle > 1e-7f) {
                const V3 k{b.wx / spin, b.wy / spin, b.wz / spin};
                const float c = std::cos(angle), sn = std::sin(angle), C = 1 - c;
                M3 q;
                q.m[0][0] = c + k.x * k.x * C;       q.m[0][1] = k.x * k.y * C - k.z * sn; q.m[0][2] = k.x * k.z * C + k.y * sn;
                q.m[1][0] = k.y * k.x * C + k.z * sn; q.m[1][1] = c + k.y * k.y * C;       q.m[1][2] = k.y * k.z * C - k.x * sn;
                q.m[2][0] = k.z * k.x * C - k.y * sn; q.m[2][1] = k.z * k.y * C + k.x * sn; q.m[2][2] = c + k.z * k.z * C;
                b.rot = q * b.rot;
            }
        }
        tumble_collide(t, w, live, showcase);
    }
    for (unsigned i = 0; i < kTumbleSlots; ++i) {
        if (!live[i]) continue;
        Tumble& b = t.weed[i];
        // Keep the orientation orthonormal (Gram-Schmidt on its columns).
        V3 c0{b.rot.m[0][0], b.rot.m[1][0], b.rot.m[2][0]}, c1{b.rot.m[0][1], b.rot.m[1][1], b.rot.m[2][1]};
        c0 = tw_unit(c0); c1 = tw_unit(c1 - c0 * dot3(c0, c1));
        b.rot = columns(c0, c1, cross3(c0, c1));
    }

    if (!enabled) return;
    // Diagnostic (tumbleweed_showcase): two balls upwind of the party (its
    // focus), 5 m and 14 m, that the wind carries into it; once per switch-on.
    // (The camera's ground point is no anchor: at a low camera it lies far
    // off, past where a ball is dropped.)
    if (showcase && !t.showcase_done) {
        t.showcase_done = true;
        const float near_x = w.focus.x - dir[0] * 5, near_y = w.focus.y - dir[1] * 5;
        const float far_x = w.focus.x - dir[0] * 14, far_y = w.focus.y - dir[1] * 14;
        tumble_start(t, t.weed[1], near_x, near_y, 1);
        tumble_start(t, t.weed[0], far_x, far_y, 1);
        char line[200];
        std::snprintf(line, sizeof(line), "party %.1f,%.1f; balls %.1f,%.1f and %.1f,%.1f; downwind %.2f,%.2f; ground wind %.1f m/s (windiness %.2f)",
                      w.focus.x, w.focus.y, near_x, near_y, far_x, far_y, dir[0], dir[1], base, t.windiness);
        bridge_append_probe_event("tumbleweed_showcase", line);
        return;
    }
    if (!showcase) t.showcase_done = false;
    // Diagnostic: each ball's state once a game second (wind, storm, speed, hop).
    if (showcase && t.clock >= t.next_report) {
        t.next_report = t.clock + 1;
        char line[256];
        int n = std::snprintf(line, sizeof(line), "ground wind %.1f m/s (windiness %.2f, storm %.2f)", base, t.windiness, t.storm);
        for (const Tumble& b : t.weed)
            if (b.active && n > 0 && n < int(sizeof(line)))
                n += std::snprintf(line + n, sizeof(line) - n, "; ball %.1f,%.1f speed %.1f m/s height %.2f m peak %.2f m%s", b.x, b.y,
                                   std::hypot(b.vx, b.vy), b.h, b.peak, tumble_offscreen(w, b.x, b.y) ? " off screen" : "");
        bridge_append_probe_event("tumbleweed_state", line);
    }
    unsigned rolling = 0; bool resting = false;
    for (const Tumble& b : t.weed) {
        if (!b.active || b.leaving) continue;
        if (std::hypot(b.vx, b.vy) > kTumbleRolling) ++rolling; else resting = true;
    }
    Tumble* spare = nullptr;
    for (Tumble& b : t.weed) if (!b.active) { spare = &b; break; }
    if (!spare) return;
    const std::uint64_t k = splitmix(t.seed ^ (std::uint64_t(t.clock * 8) << 8));
    // A roller: from upwind, off screen, while none rolls (two in a sand
    // storm) and the wind can carry it.
    if (rolling < (t.storm > .4f ? 2u : 1u) && t.clock >= t.next_roll && base * .75f > kTumbleRollWind) {
        t.next_roll = t.clock + 3;   // retried every 3 game seconds until one starts
        const float perp[2]{-dir[1], dir[0]};
        for (const float d : {25.f, 32.f, 40.f, 50.f, 60.f}) {
            const float lateral = (hash01(k, key("lat"), unsigned(d)) - .5f) * .6f * d;
            const float x = w.focus.x - dir[0] * d + perp[0] * lateral, y = w.focus.y - dir[1] * d + perp[1] * lateral;
            if (!tumble_ground(w, x, y) || !tumble_offscreen(w, x, y)) continue;
            tumble_start(t, *spare, x, y, 0);
            spare->vx = dir[0] * base * .5f; spare->vy = dir[1] * base * .5f;
            t.next_roll = t.clock + (20 + 40 * hash01(k, key("wait"))) * (1 - .7f * t.storm);
            ++t.spawns;
            if (showcase) {
                char line[96];
                std::snprintf(line, sizeof(line), "roller at %.1f,%.1f (%.0f m upwind, off screen)", x, y, d);
                bridge_append_probe_event("tumbleweed_spawn", line);
            }
            return;
        }
        return;
    }
    // A rester: in the lee of an object near the party, off screen, while none rests.
    if (!resting && t.clock >= t.next_rest) {
        t.next_rest = t.clock + 5;
        for (unsigned i = 0; i < 12; ++i) {
            const float a = TAU * hash01(k, key("ra"), i), d = 12 + 18 * hash01(k, key("rd"), i);
            const float x = w.focus.x + std::cos(a) * d, y = w.focus.y + std::sin(a) * d;
            if (!tumble_ground(w, x, y) || !tumble_offscreen(w, x, y) || tumble_shelter(w, x, y, dir) < .6f) continue;
            tumble_start(t, *spare, x, y, 0);
            t.next_rest = t.clock + 60;
            ++t.spawns;
            if (showcase) {
                char line[96];
                std::snprintf(line, sizeof(line), "rester at %.1f,%.1f (%.0f m away, in a lee, off screen)", x, y, d);
                bridge_append_probe_event("tumbleweed_spawn", line);
            }
            return;
        }
    }
}
