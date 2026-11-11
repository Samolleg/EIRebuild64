#pragma once
#include "nala_pose_asset.h"
#include "../../../Game/BodyShapeSchema.h"
#include <string>

namespace owned::skn::nala_shape {
using namespace UnityBody;
using Values = std::array<double, BodyShape::EditorSchema::Count>;
struct Edit
{
    unsigned target, source, mode;
    V position, normal;
};
struct Region
{
    unsigned pivot, canonical, shift;
    std::vector<unsigned> stops;
    // Each component is constant + coefficient * control[index].
    std::array<unsigned, 3> control;
    V constant, coefficient;
};
struct Weapon
{
    std::string name;
    unsigned role, side;
    V point, axis;
};
struct Member
{
    std::string name;
    unsigned holder;
};
struct AttachmentData
{
    std::vector<Weapon> weapons;
    std::vector<Member> members;
    std::array<V, 2> palm, forward, normal, game_soles, source_soles;
    std::array<unsigned, 2> feet;
    double scale;
    double palm_forward, palm_normal;
    const Weapon *find(const std::string &name) const
    {
        for (const auto &w : weapons)
            if (w.name == name)
                return &w;
        return nullptr;
    }
    const Weapon *assembly(const std::string &name) const
    {
        for (const auto &m : members)
            if (m.name == name)
                return &weapons[m.holder];
        return nullptr;
    }
    bool retained(const std::string &name) const
    {
        return name.rfind("hr.", 0) == 0 || assembly(name);
    }
};
struct Program
{
    Data source;
    std::vector<V> positive, negative, normal_positive, normal_negative;
    std::vector<Edit> edits;
    std::vector<Region> regions;
    std::vector<unsigned> pivots;
    std::vector<std::array<unsigned, 3>> pivot_channels;
    std::vector<int> groups;
    std::array<double, 20> upper;
    std::array<std::array<double, 3>, 3> knots;
    std::array<Values, 9> stats;
    std::array<Values, 6> database;
    std::array<std::array<double, 2>, BodyShape::EditorSchema::Count> limits;
    double scale, head;
};
// Section payloads are explicit LE scalar streams, never native structures.
class Stream
{
    const nala_pose::Bytes &bytes;
    nala_pose::Reader reader;
    unsigned at = 0;

public:
    explicit Stream(const nala_pose::Bytes &b)
        : bytes(b)
        , reader(b)
    {
    }
    unsigned integer()
    {
        unsigned v = reader.u32(at);
        at += 4;
        return v;
    }
    double number()
    {
        double v = reader.number(at);
        at += 8;
        return v;
    }
    V vector() { return { number(), number(), number() }; }
    unsigned count(unsigned stride)
    {
        unsigned n = integer();
        reader.range(at, std::uint64_t(n) * stride);
        return n;
    }
    nala_pose::Bytes blob()
    {
        unsigned n = count(1);
        nala_pose::Bytes b(bytes.begin() + at, bytes.begin() + at + n);
        at += n;
        return b;
    }
    std::string text()
    {
        auto b = blob();
        for (auto c : b)
            if (c < 32 || c > 126)
                throw std::runtime_error("SKN attachment name");
        return { b.begin(), b.end() };
    }
    void finish()
    {
        if (at != bytes.size())
            throw std::runtime_error("SKN shape trailing bytes");
    }
};
inline Program decode(const nala_pose::Bytes &bytes)
{
    Stream r(bytes);
    Program p;
    if (r.integer() != 0x31504853)
        throw std::runtime_error("SKN section 8 shape version");
    p.source = nala_pose::decode(r.blob());
    auto vectors = [&](std::vector<V> &out) {
        out.resize(r.count(24));
        for (auto &v : out)
            v = r.vector();
    };
    vectors(p.source.vertices);
    vectors(p.source.normals);
    p.source.skin.resize(r.count(48));
    for (auto &s : p.source.skin)
    {
        double sum = 0;
        for (auto &b : s.bone)
        {
            unsigned v = r.integer();
            if (v >= p.source.bindInverse.size())
                throw std::runtime_error("SKN shape skin slot");
            b = std::uint16_t(v);
        }
        for (auto &w : s.weight)
        {
            w = r.number();
            if (w < 0)
                throw std::runtime_error("SKN shape negative skin");
            sum += w;
        }
        if (std::abs(sum - 1) > 1e-5)
            throw std::runtime_error("SKN shape skin sum");
    }
    vectors(p.positive);
    vectors(p.negative);
    vectors(p.normal_positive);
    vectors(p.normal_negative);
    auto n = p.source.vertices.size();
    const auto morph_count = p.positive.size();
    if (!n || n > 1000000 || p.source.normals.size() != n || p.source.skin.size() != n ||
        morph_count > n || p.negative.size() != morph_count ||
        p.normal_positive.size() != morph_count || p.normal_negative.size() != morph_count)
        throw std::runtime_error("SKN shape source sizes");
    p.edits.resize(r.count(60));
    for (auto &e : p.edits)
    {
        e.target = r.integer();
        e.source = r.integer();
        e.mode = r.integer();
        e.position = r.vector();
        e.normal = r.vector();
        if (e.source >= n || e.mode > 2 || (e.mode == 2 ? e.target != n : e.target >= n))
            throw std::runtime_error("SKN shape edit reference");
        if (e.mode == 2)
            ++n;
    }
    p.regions.resize(r.count(76));
    for (auto &s : p.regions)
    {
        s.pivot = r.integer();
        s.canonical = r.integer();
        s.shift = r.integer();
        s.stops.resize(r.count(4));
        for (auto &v : s.stops)
            v = r.integer();
        for (auto &v : s.control)
            v = r.integer();
        s.constant = r.vector();
        s.coefficient = r.vector();
        if (s.pivot >= p.source.rest.size() || s.canonical > 1 || s.shift > 2)
            throw std::runtime_error("SKN shape region");
        for (auto v : s.stops)
            if (v >= p.source.rest.size())
                throw std::runtime_error("SKN shape stop");
        for (auto v : s.control)
            if (v >= Values{}.size())
                throw std::runtime_error("SKN shape control");
    }
    p.pivots.resize(r.count(4));
    for (auto &v : p.pivots)
    {
        v = r.integer();
        if (v >= p.source.rest.size())
            throw std::runtime_error("SKN shape pivot");
    }
    p.pivot_channels.resize(p.pivots.size());
    for (auto &a : p.pivot_channels)
        for (auto &v : a)
        {
            v = r.integer();
            if (v > 7)
                throw std::runtime_error("SKN shape pivot channel");
        }
    p.groups.resize(r.count(4));
    for (auto &v : p.groups)
    {
        v = int(r.integer());
        if (v < -1 || v >= int(p.pivots.size()))
            throw std::runtime_error("SKN shape group");
    }
    if (p.groups.size() != p.source.rest.size())
        throw std::runtime_error("SKN shape groups count");
    for (auto &v : p.upper)
        v = r.number();
    for (auto &a : p.knots)
        for (auto &v : a)
            v = r.number();
    for (auto &a : p.stats)
        for (auto &v : a)
            v = r.number();
    for (auto &a : p.database)
        for (auto &v : a)
            v = r.number();
    for (auto &a : p.limits)
        for (auto &v : a)
            v = r.number();
    p.scale = r.number();
    p.head = r.number();
    r.finish();
    for (const auto &a : p.knots)
        if (!(a[0] < a[1] && a[1] < a[2]))
            throw std::runtime_error("SKN shape knots");
    for (unsigned i = 0; i < 14; i += 2)
        if (!(p.upper[i] < p.upper[i + 1]))
            throw std::runtime_error("SKN shape nonlinear interval");
    if (!(p.upper[16] < p.upper[17]) || p.upper[18] <= 0)
        throw std::runtime_error("SKN shape nonlinear derivative");
    for (const auto &a : p.limits)
        if (a[0] > a[1])
            throw std::runtime_error("SKN shape limits");
    if (p.scale <= 0 || p.head <= 0)
        throw std::runtime_error("SKN shape scale");
    return p;
}
inline AttachmentData decode_attachments(const nala_pose::Bytes &bytes, unsigned nodes)
{
    Stream r(bytes);
    AttachmentData d;
    if (r.integer() != 0x31545441)
        throw std::runtime_error("SKN section 14 attachment version");
    d.weapons.resize(r.count(60));
    for (auto &w : d.weapons)
    {
        w.name = r.text();
        w.role = r.integer();
        w.side = r.integer();
        w.point = r.vector();
        w.axis = r.vector();
        if (w.name.empty() || w.role < 1 || w.role > 5 || w.side > 1 || Dot(w.axis, w.axis) < 1e-12)
            throw std::runtime_error("SKN attachment weapon");
    }
    d.members.resize(r.count(8));
    for (auto &m : d.members)
    {
        m.name = r.text();
        m.holder = r.integer();
        if (m.name.empty() || m.holder >= d.weapons.size())
            throw std::runtime_error("SKN attachment holder");
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        d.palm[i] = r.vector();
        d.forward[i] = r.vector();
        d.normal[i] = r.vector();
        d.game_soles[i] = r.vector();
        d.source_soles[i] = r.vector();
        d.feet[i] = r.integer();
        if (d.feet[i] >= nodes)
            throw std::runtime_error("SKN attachment foot");
    }
    d.scale = r.number();
    d.palm_forward = r.number();
    d.palm_normal = r.number();
    r.finish();
    if (d.scale < .5 || d.scale > 1.5)
        throw std::runtime_error("SKN attachment scale");
    return d;
}
Values resolve(const Program &, const std::array<double, 5> &);
void build(const Program &, const Values &, Data &);
void apply_scales(const Program &, const std::array<double, 7> &, std::vector<M> &);
} // namespace owned::skn::nala_shape
