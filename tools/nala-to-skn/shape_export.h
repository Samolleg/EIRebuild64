// Model tables are consumed exclusively by this offline cooker.
#include "nala_shape_evaluator.h"
#include "nala/NalaAttachments.h"
#include "nala/UnityHandsData.h"
#include "nala/UnityBodySizeData.h"
namespace {
void vector(Bytes &b, V v)
{
    for (double x : { v.x, v.y, v.z })
        number(b, x);
}
template <class Range>
void vectors(Bytes &b, const Range &values)
{
    u32(b, unsigned(std::size(values)));
    for (V v : values)
        vector(b, v);
}
void blob(Bytes &b, const Bytes &value)
{
    u32(b, unsigned(value.size()));
    b.insert(b.end(), value.begin(), value.end());
}
void string(Bytes &b, const char *value)
{
    std::string s(value);
    u32(b, unsigned(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
Bytes shape_section()
{
    namespace E = EditorSchema;
    using owned::skn::nala_shape::Edit;
    using owned::skn::nala_shape::Region;
    const Data source = CompleteData();
    Bytes b;
    u32(b, 0x31504853);
    blob(b, encode(source));
    vectors(b, source.vertices);
    vectors(b, source.normals);
    u32(b, unsigned(source.skin.size()));
    for (const auto &skin : source.skin)
    {
        for (unsigned v : skin.bone)
            u32(b, v);
        for (double v : skin.weight)
            number(b, v);
    }
    vectors(b, UnityAppearanceData::FatPositive);
    vectors(b, UnityAppearanceData::FatNegative);
    vectors(b, UnityAppearanceData::NormalPositive);
    vectors(b, UnityAppearanceData::NormalNegative);
    std::vector<Edit> edits;
    for (unsigned i = 0; i < std::size(NalaFace::Index); ++i)
        edits.push_back({ NalaFace::Index[i], NalaFace::Index[i], 0, NalaFace::Position[i],
                          NalaFace::Normal[i] });
    for (const auto &e : NalaSelectedIdentity::Entries)
        edits.push_back({ e.index, e.index, 0, e.position, e.normal });
    for (const auto &e : NalaEyeStageIdentity::Aliases)
        edits.push_back({ e.missing, e.represented, 1, {}, {} });
    unsigned target = unsigned(source.vertices.size());
    for (const auto &e : NalaLidContact::Proxies)
        edits.push_back({ target++, e.source, 2, e.position, e.normal });
    auto append = [&](const auto &values) {
        for (const auto &e : values)
            edits.push_back({ e.source, e.source, 0, e.position, e.normal });
    };
    append(NalaPerioralIdentity::Edits);
    append(NalaLowerOvalIdentity::Edits);
    append(NalaEyeSpacingIdentity::Edits);
    append(NalaUpperArcIdentity::Edits);
    u32(b, unsigned(edits.size()));
    for (const auto &e : edits)
    {
        u32(b, e.target);
        u32(b, e.source);
        u32(b, e.mode);
        vector(b, e.position);
        vector(b, e.normal);
    }
    std::vector<Region> commands;
    auto region = [&](unsigned pivot, std::initializer_list<unsigned> stops,
                      std::array<unsigned, 3> controls, V constant = V{},
                      V coefficient = V{ 1, 1, 1 }, bool canonical = false) {
        commands.push_back({ pivot, unsigned(canonical), 0, std::vector<unsigned>(stops), controls,
                             constant, coefficient });
    };
    auto shift = [&](unsigned pivot, V coefficient, std::array<unsigned, 3> controls,
                     unsigned mode = 1) {
        commands.push_back({ pivot, 0, mode, {}, controls, {}, coefficient });
    };
    auto longitudinal = [&](unsigned pivot, std::initializer_list<unsigned> stops,
                            unsigned control) {
        region(pivot, stops, { 0, control, 0 }, { 1, 0, 1 }, { 0, 1, 0 });
    };
    longitudinal(1, { 2 }, E::stature);
    longitudinal(2, { 3 }, E::stature);
    longitudinal(3, { 73, 4, 34, 66, 68, 139, 140, 141, 148 }, E::stature);
    for (unsigned side = 0; side < 2; ++side)
    {
        unsigned upper = side ? 5 : 35, forearm = side ? 8 : 38, hand = side ? 12 : 42,
                 thigh = side ? 157 : 173, calf = side ? 160 : 176, foot = side ? 164 : 180;
        longitudinal(upper, { forearm }, E::stature);
        longitudinal(forearm, { hand }, E::stature);
        longitudinal(thigh, { calf }, E::stature);
        longitudinal(calf, { foot }, E::stature);
    }
    longitudinal(2, { 3 }, E::waistLength);
    region(3, { 73, 5, 35 }, { E::chestWidth, E::chestLength, E::chestDepth });
    region(156, { 157, 173 }, { E::hipWidth, E::hipDepth, 0 }, { 0, 0, 1 }, { 1, 1, 0 }, true);
    for (unsigned side = 0; side < 2; ++side)
    {
        unsigned upper = side ? 5 : 35, forearm = side ? 8 : 38, hand = side ? 12 : 42,
                 thigh = side ? 157 : 173, calf = side ? 160 : 176, foot = side ? 164 : 180;
        region(upper, { forearm }, { E::upperArmWidth, E::upperArmLength, E::upperArmDepth });
        region(forearm, { hand }, { E::forearmWidth, E::forearmLength, E::forearmDepth });
        region(hand, {}, { E::handWidth, E::handLength, E::handDepth });
        region(thigh, { calf }, { E::thighWidth, E::thighLength, E::thighDepth });
        region(calf, { foot }, { E::calfWidth, E::calfLength, E::calfDepth });
        region(foot, {}, { E::footWidth, E::footLength, E::footHeight });
        unsigned glute = side ? 350 : 344;
        region(glute, {}, { E::gluteWidth, E::gluteDepth, E::gluteHeight }, {}, { 1, 1, 1 }, true);
        shift(glute, { 0, 0, .001 }, { 0, 0, E::gluteRaise });
        region(side ? 141 : 148, {}, { E::breastWidth, E::breastDepth, E::breastHeight }, {},
               { 1, 1, 1 }, true);
        for (unsigned i = 139; i <= 154; ++i)
            if ((side && (i == 139 || (i >= 141 && i <= 147))) || (!side && (i == 140 || i >= 148)))
                shift(i, { (side ? -1. : 1.) * .001, 0, .001 },
                      { E::breastSpread, 0, E::breastRaise }, 2);
    }
    region(73, { 76 }, { E::neckWidth, E::neckLength, E::neckWidth });
    region(76, {}, { E::headSize, E::headSize, E::headSize });
    u32(b, unsigned(commands.size()));
    for (const auto &c : commands)
    {
        u32(b, c.pivot);
        u32(b, c.canonical);
        u32(b, c.shift);
        u32(b, unsigned(c.stops.size()));
        for (unsigned v : c.stops)
            u32(b, v);
        for (unsigned v : c.control)
            u32(b, v);
        vector(b, c.constant);
        vector(b, c.coefficient);
    }
    u32(b, unsigned(std::size(UnityAppearanceData::Pivots)));
    for (unsigned v : UnityAppearanceData::Pivots)
        u32(b, v);
    for (unsigned i = 0; i < std::size(UnityAppearanceData::Pivots); ++i)
    {
        const std::array<unsigned, 3> channels = i == 0  ? std::array<unsigned, 3>{ 3, 7, 4 }
                                                 : i < 5 ? std::array<unsigned, 3>{ 2, 7, 2 }
                                                 : i < 9 ? std::array<unsigned, 3>{ 1, 7, 1 }
                                                         : std::array<unsigned, 3>{ 5, 5, 5 };
        for (unsigned v : channels)
            u32(b, v);
    }
    u32(b, unsigned(std::size(UnityAppearanceData::Groups)));
    for (int v : UnityAppearanceData::Groups)
        u32(b, unsigned(v));
    for (double v : { .04, .08, .035, .075, .16,  .28,   .465, .525, .53,  .56,
                      .07, .10, .37,  .43,  .035, .0304, .16,  .56,  1e-6, .30 })
        number(b, v);
    for (const auto &a : BodyShape::NalaMorphData::Knots)
        for (double v : a)
            number(b, v);
    for (const auto &a : BodyShape::NalaMorphData::Stats)
        for (double v : a)
            number(b, v);
    for (const auto &a : BodyShape::NalaMorphData::Database)
        for (double v : a)
            number(b, v);
    for (const auto &a : BodyShape::NalaMorphData::Limits)
        for (double v : a)
            number(b, v);
    number(b, .9309906028352594);
    number(b, 1.05);
    return b;
}
Bytes attachment_section()
{
    Bytes b;
    u32(b, 0x31545441);
    u32(b, unsigned(std::size(NalaAttachments::Weapons)));
    for (const auto &w : NalaAttachments::Weapons)
    {
        string(b, w.name);
        u32(b, unsigned(w.role));
        u32(b, w.side);
        vector(b, w.point);
        vector(b, w.axis);
    }
    u32(b, unsigned(std::size(NalaAttachments::Members)));
    for (const auto &m : NalaAttachments::Members)
    {
        string(b, m.name);
        u32(b, m.holder);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto &h = UnityHandsData::Sides[i];
        vector(b, h.palm);
        vector(b, h.forward);
        vector(b, h.normal);
        vector(b, UnityBodySize::GameSoles[i]);
        vector(b, UnityBodySize::SourceSoles[i]);
        u32(b, UnityBodySize::Feet[i]);
    }
    number(b, UnityBodySize::Scale);
    number(b, .020);
    number(b, .012);
    return b;
}
} // namespace
