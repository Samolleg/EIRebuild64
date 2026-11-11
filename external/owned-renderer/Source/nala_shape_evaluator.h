#pragma once
#include "nala_shape_asset.h"
namespace owned::skn::nala_shape {
namespace detail {
inline bool Desc(const Data &d, unsigned node, unsigned root)
{
    int i = int(node);
    while (i >= 0)
    {
        if (unsigned(i) == root)
            return true;
        i = d.parent[unsigned(i)];
    }
    return false;
}
inline bool Identity(V s)
{
    return s.x == 1 && s.y == 1 && s.z == 1;
}
inline M Diagonal(V s)
{
    M m;
    m.r[0][0] = s.x;
    m.r[1][1] = s.y;
    m.r[2][2] = s.z;
    return m;
}
inline void Region(const Data &d, std::vector<M> &rest, unsigned pivot,
                   const std::vector<unsigned> &stops, V scale, bool canonical = false)
{
    if (Identity(scale))
        return;
    M frame = canonical ? M{} : Proper(rest[pivot]);
    frame.t = rest[pivot].t;
    M affine = Product(Product(frame, Diagonal(scale)), Inverse(frame));
    std::vector<std::pair<unsigned, V>> terminal;
    for (unsigned stop : stops)
        terminal.push_back({ stop, Sub(Point(affine, rest[stop].t), rest[stop].t) });
    for (unsigned i = 0; i < rest.size(); ++i)
        if (Desc(d, i, pivot))
        {
            int stopped = -1;
            for (unsigned k = 0; k < terminal.size(); ++k)
                if (Desc(d, i, terminal[k].first))
                {
                    stopped = int(k);
                    break;
                }
            if (stopped < 0)
                rest[i] = Product(affine, rest[i]);
            else
                rest[i].t = Add(rest[i].t, terminal[unsigned(stopped)].second);
        }
}
inline void Shift(const Data &d, std::vector<M> &rest, unsigned pivot, V v)
{
    if (Dot(v, v) == 0)
        return;
    for (unsigned i = 0; i < rest.size(); ++i)
        if (Desc(d, i, pivot))
            rest[i].t = Add(rest[i].t, v);
}
inline M NormalMatrix(const M &m)
{
    M inv = Inverse(m), r;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            r.r[i][j] = inv.r[j][i];
    return r;
}
inline M Zero()
{
    M m;
    for (auto &row : m.r)
        for (double &x : row)
            x = 0;
    return m;
}
inline bool author(const Program &program, Data &d, const Values &v, double globalScale)
{
    auto original = d.rest, rest = d.rest;
    for (const auto &command : program.regions)
    {
        const double c[] = { command.constant.x, command.constant.y, command.constant.z };
        const double k[] = { command.coefficient.x, command.coefficient.y, command.coefficient.z };
        double s[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            s[axis] = c[axis] + k[axis] * v[command.control[axis]];
        if (command.shift == 2)
            rest[command.pivot].t = Add(rest[command.pivot].t,
                                        Scale({ s[0], s[1], s[2] }, 1 / globalScale));
        else if (command.shift)
            Shift(d, rest, command.pivot, Scale({ s[0], s[1], s[2] }, 1 / globalScale));
        else
            Region(d, rest, command.pivot, command.stops, { s[0], s[1], s[2] },
                   command.canonical != 0);
    }
    bool changed = false;
    std::vector<M> delta(rest.size());
    for (unsigned i = 0; i < rest.size(); ++i)
    {
        delta[i] = Product(rest[i], Inverse(original[i]));
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                changed |= rest[i].r[r][c] != original[i].r[r][c];
        changed |= Dot(Sub(rest[i].t, original[i].t), Sub(rest[i].t, original[i].t)) != 0;
    }
    if (!changed)
        return true;
    std::vector<M> oldSkin(d.bindInverse.size()), newSkin(oldSkin.size()),
        oldNormal(oldSkin.size()), newNormal(oldSkin.size());
    for (unsigned i = 0; i < oldSkin.size(); ++i)
    {
        unsigned node = d.bindNode.empty() ? i : d.bindNode[i];
        oldSkin[i] = Product(original[node], d.bindInverse[i]);
        newSkin[i] = Product(rest[node], d.bindInverse[i]);
        oldNormal[i] = NormalMatrix(oldSkin[i]);
        newNormal[i] = NormalMatrix(newSkin[i]);
    }
    for (unsigned i = 0; i < d.vertices.size(); ++i)
    {
        M neutral = Zero(), neutralNormal = Zero();
        V target{}, normal{};
        for (unsigned k = 0; k < 4; ++k)
        {
            unsigned slot = d.skin[i].bone[k];
            double w = d.skin[i].weight[k];
            if (!w)
                continue;
            target = Add(target, Scale(Point(newSkin[slot], d.vertices[i]), w));
            normal = Add(normal, Scale(Rotate(newNormal[slot], d.normals[i]), w));
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 3; ++c)
                {
                    neutral.r[r][c] += oldSkin[slot].r[r][c] * w;
                    neutralNormal.r[r][c] += oldNormal[slot].r[r][c] * w;
                }
            neutral.t = Add(neutral.t, Scale(oldSkin[slot].t, w));
        }
        d.vertices[i] = Point(Inverse(neutral), target);
        d.normals[i] = Unit(Rotate(Inverse(neutralNormal), normal));
        if (!std::isfinite(Dot(d.vertices[i], d.vertices[i])))
            return false;
    }
    // Preserve the original neutral renderer mapping S0=oldRest*oldBind.
    // Eye/teeth/tongue renderers do not share the body's bind coordinate frame.
    for (unsigned i = 0; i < d.bindInverse.size(); ++i)
    {
        unsigned node = d.bindNode.empty() ? i : d.bindNode[i];
        d.bindInverse[i] = Product(Inverse(rest[node]), Product(original[node], d.bindInverse[i]));
    }
    d.rest = rest;
    for (unsigned i = 0; i < rest.size(); ++i)
    {
        d.restQ[i] = Quaternion(rest[i]);
        d.stretch[i] = Product(Inverse(Rotation(d.restQ[i])), rest[i]);
        d.stretch[i].t = {};
        if (d.parent[i] >= 0)
            d.local[i] = Product(Inverse(rest[unsigned(d.parent[i])]), rest[i]);
    }
    for (auto &limit : d.limits)
        limit.frame = Product(Proper(delta[limit.parent]), limit.frame);
    return true;
}

inline double Smooth(double a, double b, double x)
{
    double t = std::clamp((x - a) / (b - a), 0., 1.);
    return t * t * (3 - 2 * t);
}
// Smoothly saturate lateral displacement, so the distal arm is translated
// rather than widened/stretched together with the torso.
inline double Capped(const std::array<double, 20> &k, double x)
{
    double u = std::abs(x), a = k[0], b = k[1], v;
    if (u <= a)
        v = u;
    else if (u >= b)
        v = (a + b) * .5;
    else
    {
        double t = (u - a) / (b - a);
        v = a + (b - a) * (t - t * t * t + .5 * t * t * t * t);
    }
    return std::copysign(v, x);
}
inline V Map(const std::array<double, 20> &k, double width, V p)
{
    double central = 1 - Smooth(k[2], k[3], std::abs(p.x));
    double rise = Smooth(k[4], k[5], p.z);
    double upper = rise * (1 - Smooth(k[6], k[7], p.z) * central) * (1 - Smooth(k[8], k[9], p.z));
    double torso = (1 - Smooth(k[10], k[11], std::abs(p.x))) * (1 - Smooth(k[12], k[13], p.z));
    return { p.x + width * Capped(k, p.x) * upper, p.y + k[14] * (p.y + k[15]) * rise * torso,
             p.z };
}
inline void upper(const Program &program, double width, Data &d)
{
    const auto &k = program.upper;
    for (unsigned i = 0; i < d.vertices.size(); ++i)
    {
        V p = d.vertices[i];
        if (p.z <= k[16] || p.z >= k[17])
            continue;
        V mapped = Map(k, width, p);
        M jac;
        const double h = k[18];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            V a = p, b = p;
            double *ap = axis == 0 ? &a.x : axis == 1 ? &a.y : &a.z;
            double *bp = axis == 0 ? &b.x : axis == 1 ? &b.y : &b.z;
            *ap += h;
            *bp -= h;
            V column = Scale(Sub(Map(k, width, a), Map(k, width, b)), .5 / h);
            jac.r[0][axis] = column.x;
            jac.r[1][axis] = column.y;
            jac.r[2][axis] = column.z;
        }
        M inv = Inverse(jac), normal;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col)
                normal.r[row][col] = inv.r[col][row];
        d.vertices[i] = mapped;
        d.normals[i] = Unit(Rotate(normal, d.normals[i]));
    }
    auto original = d.rest;
    std::vector<bool> changed(d.rest.size(), false);
    for (unsigned i = 0; i < d.rest.size(); ++i)
    {
        d.rest[i].t = Map(k, width, original[i].t);
        V shift = Sub(d.rest[i].t, original[i].t);
        changed[i] = Dot(shift, shift) > 0;
    }
    for (unsigned i = 0; i < d.rest.size(); ++i)
        if (d.parent[i] >= 0 && (changed[i] || changed[unsigned(d.parent[i])]))
            d.local[i] = Product(Inverse(d.rest[unsigned(d.parent[i])]), d.rest[i]);
    // Each renderer has its OWN inverse bind slots; retain their rotations and
    // relative bind offsets, updating only the corresponding global translation.
    for (unsigned i = 0; i < d.bindInverse.size(); ++i)
    {
        unsigned node = d.bindNode.empty() ? i : d.bindNode[i];
        if (!changed[node])
            continue;
        M inverseShift;
        inverseShift.t = Sub(original[node].t, d.rest[node].t);
        d.bindInverse[i] = Product(d.bindInverse[i], inverseShift);
    }
}

} // namespace detail
inline Values resolve(const Program &p, const std::array<double, 5> &input)
{
    // CUnit capture order is database build/fullness/height, strength, dexterity.
    for (double v : input)
        if (!std::isfinite(v))
            throw std::runtime_error("SKN shape input nonfinite");
    const double strength = std::clamp(input[3], 15., 35.),
                 dexterity = std::clamp(input[4], 15., 35.);
    std::array<double, 3> db;
    for (unsigned i = 0; i < 3; ++i)
        db[i] = std::clamp(input[i], p.knots[i][0], p.knots[i][2]);
    Values out;
    unsigned si = strength < 25 ? 0 : 1, di = dexterity < 25 ? 0 : 1;
    double x = (strength - (15 + si * 10)) / 10, y = (dexterity - (15 + di * 10)) / 10;
    for (unsigned k = 0; k < out.size(); ++k)
    {
        const auto &v = p.stats;
        out[k] = ((1 - x) * v[si * 3 + di][k] + x * v[(si + 1) * 3 + di][k]) * (1 - y) +
                 ((1 - x) * v[si * 3 + di + 1][k] + x * v[(si + 1) * 3 + di + 1][k]) * y;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            double value = db[axis], center = p.knots[axis][1];
            unsigned side = value < center ? 0 : 1;
            double endpoint = p.knots[axis][side ? 2 : 0],
                   t = (value - center) / (endpoint - center), base = v[4][k],
                   sample = p.database[axis * 2 + side][k];
            if (k == BodyShape::EditorSchema::height || k == BodyShape::EditorSchema::stature)
                out[k] *= 1 + t * (sample / base - 1);
            else
                out[k] += t * (sample - base);
        }
        if (!std::isfinite(out[k]) || out[k] < p.limits[k][0] - 1e-9 ||
            out[k] > p.limits[k][1] + 1e-9)
            throw std::runtime_error("SKN shape resolved control range");
    }

    out[BodyShape::EditorSchema::headSize] = p.head;
    return out;
}
inline void build(const Program &p, const Values &v, Data &d)
{
    d = p.source;
    const double fat = v[BodyShape::EditorSchema::fat];
    const auto &delta = fat >= 0 ? p.positive : p.negative;
    const auto &dn = fat >= 0 ? p.normal_positive : p.normal_negative;
    for (unsigned i = 0; i < delta.size(); ++i)
    {
        d.vertices[i] = Add(d.vertices[i], Scale(delta[i], std::abs(fat)));
        d.normals[i] = Unit(Add(d.normals[i], Scale(dn[i], std::abs(fat))));
    }
    for (const auto &e : p.edits)
    {
        V position = Add(d.vertices[e.source], e.position),
          normal = Unit(Add(d.normals[e.source], e.normal));
        if (e.mode == 2)
        {
            d.vertices.push_back(position);
            d.normals.push_back(normal);
            d.skin.push_back(d.skin[e.source]);
        }
        else
        {
            d.vertices[e.target] = position;
            d.normals[e.target] = normal;
        }
    }
    detail::upper(p, p.upper[19] * v[BodyShape::EditorSchema::shoulders], d);
    if (!detail::author(p, d, v, p.scale * v[BodyShape::EditorSchema::height]))
        throw std::runtime_error("SKN shape authoring rejected");
    auto validate_matrix = [](const M &matrix) {
        const M inverse = Inverse(matrix);
        if (!std::isfinite(Dot(matrix.t, matrix.t)))
            throw std::runtime_error("SKN shape nonfinite rig translation");
        for (const auto &row : inverse.r)
            for (double value : row)
                if (!std::isfinite(value))
                    throw std::runtime_error("SKN shape singular rig matrix");
    };
    for (const auto &matrix : d.rest)
        validate_matrix(matrix);
    for (const auto &matrix : d.bindInverse)
        validate_matrix(matrix);
}
inline void apply_scales(const Program &p, const std::array<double, 7> &profile,
                         std::vector<M> &world)
{
    std::vector<M> affine(p.pivots.size());
    for (unsigned i = 0; i < affine.size(); ++i)
    {
        unsigned pivot = p.pivots[i];
        M r = Proper(world[pivot]), s;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            unsigned channel = p.pivot_channels[i][axis];
            s.r[axis][axis] = channel == 7 ? 1 : profile[channel];
        }
        affine[i] = Product(Product(r, s), Inverse(r));
        affine[i].t = Sub(world[pivot].t, Rotate(affine[i], world[pivot].t));
    }
    for (unsigned i = 0; i < world.size(); ++i)
        if (p.groups[i] >= 0)
            world[i] = Product(affine[unsigned(p.groups[i])], world[i]);
}
} // namespace owned::skn::nala_shape
