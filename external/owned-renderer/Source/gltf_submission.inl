// Included in namespace owned. Uses the same retained pose binding and GPU pool.
namespace {
bool prepare_gltf_pose(const RetainedVisualSample &sample,
                       const std::shared_ptr<const skn::RuntimeAsset> &asset)
{
    auto &prepared = *sample.prepared;
    auto &current = prepared.gpu_pose;
    const auto base = current.binding->model->nodes.size();
    if (prepared.motion_asset != asset || prepared.motion_source_binding != current.binding)
    {
        prepared.motion_source_binding = current.binding;
        auto model = std::make_shared<ModelCpuAsset>(*current.binding->model);
        for (const auto &joint : asset->asset.joints)
        {
            ModelNodeCpuAsset node;
            node.name = joint.name;
            node.transform_available = false;
            ModelLink link;
            link.child = joint.name;
            link.parent_index = joint.parent < 0 ? UINT_MAX
                                                 : static_cast<unsigned>(base) + joint.parent;
            if (joint.parent >= 0)
                link.parent = asset->asset.joints[joint.parent].name;
            model->nodes.push_back(std::move(node));
            model->links.push_back(std::move(link));
        }
        auto binding = std::make_shared<VisualSkeletonBinding>(*current.binding);
        binding->model = std::move(model);
        binding->tracks.resize(binding->model->nodes.size(), nullptr);
        prepared.motion_binding = std::move(binding);
        prepared.motion_asset = asset;
        prepared.motion_pose_valid = false;
    }
    current.binding = prepared.motion_binding;
    if (!prepared.motion_pose_valid)
    {
        auto result = std::make_shared<std::vector<NativeWorldGeometry::Pose::Node>>(
            base + asset->gltf_asset->idle.size());
        const skn::Matrix yToZ{ 1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1 };
        for (unsigned i = 0; i < asset->gltf_asset->idle.size(); ++i)
        {
            const auto matrix = skn::multiply(yToZ, asset->gltf_asset->idle[i]);
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    (*result)[base + i].rows[r][c] = matrix[c * 4 + r];
        }
        current.baked = std::move(result);
        prepared.motion_pose_valid = true;
    }
    current.height_scale = 1;
    current.contact_world_height = asset->gltf_asset->radius * 2;
    return true;
}
} // namespace
