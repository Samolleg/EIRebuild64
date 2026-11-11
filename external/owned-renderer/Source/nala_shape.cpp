#include "nala_shape.h"
#include "retained_static_submission.h"
#include "skn_runtime.h"
#include "nala_shape_evaluator.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>

// The immutable model program is decoded from SKN section 8.
namespace owned {
std::shared_ptr<const skn::RuntimeAsset> unit_skn_asset(
    const RetainedVisualSample& sample, bool& present) {
    auto& prepared = *sample.prepared;
    const auto& input = sample.values.nala;
    if (!input.selected)
        return skn::select(sample.assembly->skeleton->visual_reference, present);
    present = true;
    if (!input.valid) throw std::runtime_error("Nala shape inputs invalid");
    if (prepared.nala_shape_model == sample.assembly->skeleton->visual_reference &&
        prepared.nala_shape_valid && prepared.nala_shape_inputs == input.shape_inputs) {
        if (!prepared.nala_shape_error.empty()) throw std::runtime_error(prepared.nala_shape_error);
        return prepared.nala_shape_asset;
    }
    // Invalidate all consumers together before publishing the replacement.
    prepared.merged_assembly.reset();
    prepared.skn_bounds_valid = false;
    prepared.nala_shape_asset.reset();
    prepared.nala_shape_model = sample.assembly->skeleton->visual_reference;
    prepared.nala_shape_inputs = input.shape_inputs;
    prepared.nala_shape_valid = true;
    prepared.nala_shape_error = "Nala bind shape calculation failed";
    try {
        const auto start = std::chrono::steady_clock::now();
        auto base = skn::select(sample.assembly->skeleton->visual_reference, present);
        if (!base || !base->asset.nala_rig) throw std::runtime_error("Nala SKN rig missing");
        if(!base->asset.nala_shape)throw std::runtime_error("Nala shape section 8 missing");
        const auto& program=*base->asset.nala_shape;
        const auto controls=skn::nala_shape::resolve(program,input.shape_inputs);
        auto shaped=std::make_shared<UnityBody::Data>();
        skn::nala_shape::build(program,controls,*shaped);
        auto result = std::make_shared<skn::RuntimeAsset>(*base);
        auto& data = result->asset;
        const auto count = shaped->vertices.size();
        if (data.vertices.size() != count + data.nala_contacts.size() ||
            data.joints.size() != shaped->bindInverse.size() ||
            data.nala_contacts.empty())
            throw std::runtime_error("Nala section 8/SKN topology mismatch");
        auto set_vertex = [](skn::Vertex& vertex, UnityBody::V p, UnityBody::V n) {
            using namespace UnityBody;
            if (!std::isfinite(Dot(p,p)) || !std::isfinite(Dot(n,n)))
                throw std::runtime_error("Nala nonfinite bind vertex");
            n = Unit(n);
            const V tangent = Unit(std::abs(n.z) < .9 ? V{-n.y,n.x,0} : V{0,-n.z,n.y});
            const double position[] = {p.x,p.y,p.z};
            const double normal[] = {n.x,n.y,n.z};
            const double direction[] = {tangent.x,tangent.y,tangent.z};
            for (unsigned axis=0; axis<3; ++axis) {
                vertex.position[axis] = float(position[axis]);
                vertex.normal[axis] = std::int16_t(std::lround(normal[axis]*32767));
                vertex.tangent[axis] = std::int16_t(std::lround(direction[axis]*32767));
            }
        };
        for (unsigned i=0; i<count; ++i)
            set_vertex(data.vertices[i], shaped->vertices[i], shaped->normals[i]);
        // Section 9 retains correspondence. Rebuild its bind positions and
        // normals from the newly shaped sources, including eyelid proxies.
        for (const auto& contact : data.nala_contacts) {
            UnityBody::V position{}, normal{};
            for (unsigned k=0; k<3; ++k) {
                if (contact.source[k]>=count) throw std::runtime_error("Nala contact source mismatch");
                position = UnityBody::Add(position, UnityBody::Scale(shaped->vertices[contact.source[k]],contact.weight[k]));
                normal = UnityBody::Add(normal, UnityBody::Scale(shaped->normals[contact.source[k]],contact.weight[k]));
            }
            set_vertex(data.vertices.at(contact.target), position, normal);
        }
        data.nala_rig = std::move(shaped);
        for (auto& lod : data.lods) {
            lod.minimum.fill(FLT_MAX);
            lod.maximum.fill(-FLT_MAX);
            for (unsigned i=lod.first_vertex; i<lod.first_vertex+lod.vertex_count; ++i)
                for (unsigned axis=0; axis<3; ++axis) {
                    lod.minimum[axis]=(std::min)(lod.minimum[axis],data.vertices[i].position[axis]);
                    lod.maximum[axis]=(std::max)(lod.maximum[axis],data.vertices[i].position[axis]);
                }
        }
        using namespace BodyShape::EditorSchema;
        result->nala_profile = {controls[fat],controls[legs],controls[arms],controls[waist],controls[waistDepth],controls[breasts],program.scale*controls[height]};
        result->joint_radius.assign(data.joints.size(), -1);
        for (const auto& vertex : data.vertices) for (unsigned k=0; k<4; ++k) if (vertex.weights[k]) {
            const float* p=vertex.position;
            const float radius=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
            auto& bound=result->joint_radius[vertex.joints[k]];
            bound=(std::max)(bound,std::nextafter(radius,INFINITY));
        }
        static std::atomic<std::uint64_t> revision{0};
        result->shape_key = "|nala-shape=" + std::to_string(++revision);
        prepared.nala_shape_asset = result;
        prepared.nala_shape_error.clear();
        const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::ostringstream detail;
        detail.precision(17);
        detail << "model=\"" << sample.assembly->skeleton->visual_reference << "\" inputs=";
        for (double value : input.shape_inputs) detail << value << ' ';
        detail << "milliseconds=" << milliseconds;
        bridge_append_probe_event("nala_shape_applied",detail.str().c_str());
        return result;
    } catch (const std::exception& error) {
        prepared.nala_shape_error=error.what();
        throw;
    }
}
}
