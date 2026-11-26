#define _CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "../../../dependencies/cgltf/v1.15/cgltf.h"
#include "gltf_asset.h"
#include "../../../SDKs/JSON/json.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <limits>

namespace owned::gltf {
using Json = nlohmann::json;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool valid, const std::string &reason)
{
    if (!valid)
        throw std::runtime_error("glTF " + reason);
}
void check(HRESULT result, const char *operation)
{
    require(SUCCEEDED(result), operation);
}
Json json(const ResourceBlob &blob)
{
    return Json::parse(blob.bytes->begin(), blob.bytes->end());
}
Json extras(const cgltf_extras &e)
{
    return e.data ? Json::parse(e.data) : Json::object();
}
float linear(float x)
{
    return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
float encoded(float x)
{
    return x <= .0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.f / 2.4f) - .055f;
}
// WIC is shared by both GPU backends. BGRA matches argb8888 in the native pool.
skn::Dds image(const PublishPixels &publish, const ResourceBlob &source, const cgltf_image &input,
               bool color, const float *factor = nullptr,
               const cgltf_texture_view *occlusion = nullptr, float normalScale = 1,
               bool orm = false)
{
    require(!input.uri && input.buffer_view && input.buffer_view->buffer->data,
            "embedded PNG/JPEG required");
    require(input.mime_type && (std::strcmp(input.mime_type, "image/png") == 0 ||
                                std::strcmp(input.mime_type, "image/jpeg") == 0),
            "image MIME");
    const auto &view = *input.buffer_view;
    require(view.size <= UINT_MAX, "image too large");
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment
    {
        HRESULT result;
        ~Apartment()
        {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } apartment{ initialized };
    require(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE, "COM initialization");
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)),
          "WIC factory");
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "WIC stream");
    auto *start = static_cast<BYTE *>(view.buffer->data) + view.offset;
    check(stream->InitializeFromMemory(start, static_cast<DWORD>(view.size)), "WIC memory stream");
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                           &decoder),
          "PNG/JPEG decoder");
    GUID container{};
    check(decoder->GetContainerFormat(&container), "image container");
    require(container == GUID_ContainerFormatPng || container == GUID_ContainerFormatJpeg,
            "PNG/JPEG container required");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "image frame");
    UINT width = 0, height = 0;
    check(frame->GetSize(&width, &height), "image size");
    require(width && height && width <= 16384 && height <= 16384 &&
                std::uint64_t(width) * height <= 67108864,
            "image dimensions");
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter), "image converter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom),
          "BGRA conversion");
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
    check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                pixels.data()),
          "image pixels");
    // All input channels are bytes. Reuse the exact sRGB transfer results
    // rather than running pow for every pixel in every stock material.
    static const auto linearByte=[] {
        std::array<float,256> values{};
        for(unsigned i=0;i<values.size();++i) values[i]=linear(i/255.f);
        return values;
    }();
    if (color && factor && (factor[0]!=1 || factor[1]!=1 || factor[2]!=1)) {
        std::array<std::array<std::uint8_t,256>,3> tint{};
        for(unsigned c=0;c<3;++c) for(unsigned i=0;i<256;++i) {
            const float value=encoded(linearByte[i]*factor[2-c]);
            tint[c][i]=static_cast<std::uint8_t>(std::lround((std::clamp)(value,0.f,1.f)*255));
        }
        for(std::size_t p=0;p<pixels.size();p+=4)
            for(unsigned c=0;c<3;++c) pixels[p+c]=tint[c][pixels[p+c]];
    }
    if (normalScale != 1)
        for (std::size_t p = 0; p < pixels.size(); p += 4)
        {
            const float x = (pixels[p + 2] / 127.5f - 1) * normalScale,
                        y = (pixels[p + 1] / 127.5f - 1) * normalScale;
            const float z = pixels[p] / 127.5f - 1;
            const float norm = std::sqrt(x * x + y * y + z * z);
            require(norm > 0, "normal map vector");
            pixels[p + 2] = static_cast<std::uint8_t>(std::lround((x / norm + 1) * 127.5f));
            pixels[p + 1] = static_cast<std::uint8_t>(std::lround((y / norm + 1) * 127.5f));
            pixels[p] = static_cast<std::uint8_t>(std::lround((z / norm + 1) * 127.5f));
        }
    if (orm && (!occlusion || !occlusion->texture))
        for (std::size_t i = 2; i < pixels.size(); i += 4)
            pixels[i] = 255;
    if (occlusion && occlusion->texture)
    {
        require(occlusion->texture->image, "occlusion image");
        require(std::isfinite(occlusion->scale) && occlusion->scale >= 0 && occlusion->scale <= 1,
                "occlusion strength");
        auto ao = image(publish, source, *occlusion->texture->image, false);
        const auto &mip = ao.mips.front();
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const unsigned sx = static_cast<unsigned>(std::uint64_t(x) * mip.width / width);
                const unsigned sy = static_cast<unsigned>(std::uint64_t(y) * mip.height / height);
                const float value = mip.data[(sy * mip.width + sx) * 4 + 2] / 255.f;
                pixels[(y * width + x) * 4 + 2] = static_cast<std::uint8_t>(
                    std::lround((1 - occlusion->scale + occlusion->scale * value) * 255));
            }
    }
    // Map a linear channel to the identical rounded sRGB byte. Only bins
    // crossing an 8-bit rounding boundary need the original transfer function.
    static const auto encodedByte=[] {
        std::array<std::uint8_t,65537> values{};
        for(unsigned i=0;i<values.size();++i)
            values[i]=static_cast<std::uint8_t>(std::lround(encoded(i/65536.f)*255));
        return values;
    }();
    const auto colorByte=[](float v) {
        const auto i=static_cast<unsigned>(v*65536.f);
        if(i>=65536 || encodedByte[i]==encodedByte[i+1]) return encodedByte[i];
        return static_cast<std::uint8_t>(std::lround(encoded(v)*255));
    };
    std::vector<std::array<unsigned, 4>> levels;
    unsigned w = width, h = height;
    for (;;)
    {
        levels.push_back(
            { static_cast<unsigned>(pixels.size() - std::size_t(w) * h * 4), w, h, w * h * 4 });
        if (w == 1 && h == 1)
            break;
        const auto previous = levels.back();
        const unsigned nw = (std::max)(1u, w / 2), nh = (std::max)(1u, h / 2);
        const auto offset = pixels.size();
        pixels.resize(offset + std::size_t(nw) * nh * 4);
        if(w==2*nw && h==2*nh) {
            // Stock PNGs are predominantly power-of-two. Keep the source
            // addition order and rounding, without per-sample axis divisions.
            for(unsigned y=0;y<nh;++y) for(unsigned x=0;x<nw;++x) {
                const auto* top=pixels.data()+previous[0]+(2*y*w+2*x)*4;
                const auto* bottom=top+w*4;
                auto* dst=pixels.data()+offset+(y*nw+x)*4;
                for(unsigned c=0;c<4;++c) {
                    if(color && c<3) {
                        const float avg=(linearByte[top[c]]+linearByte[top[4+c]]+
                                         linearByte[bottom[c]]+linearByte[bottom[4+c]])*.25f;
                        dst[c]=colorByte(avg);
                    } else {
                        const float avg=(top[c]/255.f+top[4+c]/255.f+
                                         bottom[c]/255.f+bottom[4+c]/255.f)*.25f;
                        dst[c]=static_cast<std::uint8_t>(std::lround(avg*255));
                    }
                }
            }
            w=nw;h=nh;
            continue;
        }
        for (unsigned y = 0; y < nh; ++y)
            for (unsigned x = 0; x < nw; ++x)
                for (unsigned c = 0; c < 4; ++c)
                {
                    float sum = 0;
                    unsigned count = 0;
                    // Area coverage includes odd trailing rows and columns.
                    for (unsigned sy = y * h / nh; sy < (y + 1) * h / nh; ++sy)
                        for (unsigned sx = x * w / nw; sx < (x + 1) * w / nw; ++sx)
                        {
                            const auto v=pixels[previous[0]+(sy*w+sx)*4+c];
                            sum += color && c < 3 ? linearByte[v] : v/255.f;
                            ++count;
                        }
                    float value = sum / count;
                    if (color && c < 3)
                        value = encoded(value);
                    pixels[offset + (y * nw + x) * 4 + c] = static_cast<std::uint8_t>(
                        std::lround((std::clamp)(value, 0.f, 1.f) * 255));
                }
        w = nw;
        h = nh;
    }
    skn::Dds result;
    result.blob = publish(source.identity.canonical_name + "#pixels", pixels.data(),
                          static_cast<std::uint32_t>(pixels.size()));
    result.format = NativeOwnedTextureFormat::argb8888;
    for (const auto &level : levels)
        result.mips.push_back(
            { result.blob.bytes->data() + level[0], level[1], level[2], level[3] });
    return result;
}
std::vector<float> values(const cgltf_accessor *a)
{
    require(a && a->count <= 16777216, "accessor count");
    const auto count = cgltf_num_components(a->type) * a->count;
    std::vector<float> output(count);
    require(cgltf_accessor_unpack_floats(a, output.data(), count) == count, "accessor decode");
    for (float v : output)
        require(std::isfinite(v), "nonfinite accessor");
    return output;
}
skn::Joint joint(const cgltf_node &node)
{

    skn::Joint j{};
    j.name = node.name ? node.name : "";
    j.parent = -1;
    std::copy_n(node.translation, 3, j.translation.begin());
    std::copy_n(node.rotation, 4, j.rotation.begin());
    std::copy_n(node.scale, 3, j.scale.begin());
    if (node.has_matrix)
    {
        DirectX::XMFLOAT4X4 matrix;
        std::copy_n(node.matrix, 16, &matrix.m[0][0]);
        DirectX::XMVECTOR scale, rotation, translation;
        require(DirectX::XMMatrixDecompose(&scale, &rotation, &translation,
                                          DirectX::XMLoadFloat4x4(&matrix)), "node matrix decomposition");
        DirectX::XMFLOAT3 t, s;
        DirectX::XMFLOAT4 q;
        DirectX::XMStoreFloat3(&t, translation);
        DirectX::XMStoreFloat3(&s, scale);
        DirectX::XMStoreFloat4(&q, rotation);
        j.translation = {t.x, t.y, t.z}; j.scale = {s.x, s.y, s.z};
        j.rotation = {q.x, q.y, q.z, q.w};
        const auto reconstructed = skn::trs(j);
        for (unsigned k = 0; k < 16; ++k)
            require(std::isfinite(node.matrix[k]) && std::abs(reconstructed[k] - node.matrix[k]) <= 1e-4f,
                    "node matrix must be TRS");
    }
    float norm = 0;
    for (float v : j.rotation)
        norm += v * v;
    require(std::abs(norm - 1) < 1e-4, "quaternion " + j.name);
    for (float v : j.scale)
        require(std::isfinite(v) && v > 0, "scale " + j.name);
    return j;
}
skn::Joint reference_joint(const Json &input)
{
    skn::Joint j{};
    j.translation = input.at("translation").get<std::array<float, 3>>();
    j.rotation = input.at("rotation").get<std::array<float, 4>>();
    j.scale = input.at("scale").get<std::array<float, 3>>();
    return j;
}
void matrix_equal(const skn::Matrix &a, const skn::Matrix &b, const std::string &name)
{
    for (unsigned k = 0; k < 16; ++k)
        require(std::isfinite(a[k]) && std::abs(a[k] - b[k]) <= 1e-4, "bind " + name);
}
} // namespace
skn::Dds decode_base_color(const ResourceBlob &blob, const PublishPixels &publish,
                           const std::array<float,4>& factor)
{
    require(blob.bytes && !blob.bytes->empty() && blob.bytes->size() <= UINT_MAX,
            "external base color PNG size");
    const std::uint8_t signature[] = {137,80,78,71,13,10,26,10};
    require(blob.bytes->size()>=sizeof(signature) &&
            std::equal(std::begin(signature),std::end(signature),blob.bytes->begin()),
            "external base color requires PNG");
    cgltf_buffer buffer{};
    buffer.data = const_cast<std::uint8_t*>(blob.bytes->data());
    cgltf_buffer_view view{};
    view.buffer = &buffer;
    view.size = blob.bytes->size();
    cgltf_image input{};
    input.buffer_view = &view;
    input.mime_type = const_cast<char*>("image/png");
    return image(publish, blob, input, true, factor.data());
}
skn::Dds decode_metallic_roughness(const ResourceBlob &blob, const PublishPixels &publish)
{
    require(blob.bytes && !blob.bytes->empty() && blob.bytes->size() <= UINT_MAX,
            "external metallic/roughness PNG size");
    const std::uint8_t signature[] = {137,80,78,71,13,10,26,10};
    require(blob.bytes->size()>=sizeof(signature) &&
            std::equal(std::begin(signature),std::end(signature),blob.bytes->begin()),
            "external metallic/roughness requires PNG");
    cgltf_buffer buffer{};
    buffer.data = const_cast<std::uint8_t*>(blob.bytes->data());
    cgltf_buffer_view view{};
    view.buffer = &buffer;
    view.size = blob.bytes->size();
    cgltf_image input{};
    input.buffer_view = &view;
    input.mime_type = const_cast<char*>("image/png");
    // Data maps are linear; do not apply the base-color sRGB conversion.
    return image(publish, blob, input, false);
}
std::shared_ptr<Asset> decode(const ResourceBlob &blob, const ResourceBlob &profile,
                              const PublishPixels &publish)
{
    auto output = std::make_shared<Asset>();
    output->document.bytes = blob.bytes;
    cgltf_options options{};
    cgltf_data *raw = nullptr;
    require(cgltf_parse(&options, blob.bytes->data(), blob.bytes->size(), &raw) ==
                cgltf_result_success,
            "parse " + blob.identity.canonical_name);
    output->document.parsed = { raw, cgltf_free };
    auto &data = *raw;
    require(data.file_type == cgltf_file_type_glb, "GLB container required");
    require(data.nodes_count <= 65535, "node count");
    for (std::size_t i = 0; i < data.nodes_count; ++i)
    {
        std::size_t depth = 0;
        for (auto *node = &data.nodes[i]; node; node = node->parent)
            require(++depth <= data.nodes_count, "node hierarchy cycle");
    }
    for (std::size_t i = 0; i < data.extensions_required_count; ++i)
    {
        const std::string name = data.extensions_required[i];
        require(name == "KHR_materials_unlit",
                "required extension " + name);
    }
    for (std::size_t i = 0; i < data.buffers_count; ++i)
        require(!data.buffers[i].uri, "external buffer URI");
    for (std::size_t i = 0; i < data.images_count; ++i)
        require(!data.images[i].uri, "external image URI");
    require(cgltf_load_buffers(&options, raw, nullptr) == cgltf_result_success, "embedded buffers");
    require(cgltf_validate(raw) == cgltf_result_success, "validation");
    const auto *scene = data.scene;
    if (!scene && data.scenes_count == 1)
        scene = data.scenes;
    require(scene, "unambiguous active scene required");
    std::set<const cgltf_node *> activeNodes;
    std::vector<const cgltf_node *> pendingNodes;
    for (std::size_t i = 0; i < scene->nodes_count; ++i)
        pendingNodes.push_back(scene->nodes[i]);
    while (!pendingNodes.empty())
    {
        const auto *node = pendingNodes.back();
        pendingNodes.pop_back();
        require(activeNodes.insert(node).second, "duplicate active scene node");
        for (std::size_t i = 0; i < node->children_count; ++i)
            pendingNodes.push_back(node->children[i]);
    }
    const auto metadata = extras(data.asset.extras).at("ei");
    const bool articulated = metadata.at("kind") == "articulated-item";
    const bool rigid = articulated || metadata.at("kind") == "rigid-item";
    const bool wearable = metadata.at("kind") == "skinned-item";
    require(metadata.at("profile") == "human.v1" &&
        (rigid || wearable || metadata.at("kind") == "body"), "asset profile/kind");
    output->rigid = rigid; output->wearable = wearable;
    const bool physique = metadata.contains("physique");
    if(physique) {
        const auto& schema=metadata.at("physique");
        require(!rigid && !wearable && schema.at("version")==1,"physique version/kind");
        auto& model=output->physique;
        model.targets=schema.at("targets").get<std::vector<std::string>>();
        require(!model.targets.empty() && model.targets.size()<=64 &&
            std::set<std::string>(model.targets.begin(),model.targets.end()).size()==model.targets.size(),"physique names");
        model.axes=schema.at("axes").get<std::array<std::vector<float>,5>>();
        size_t samples=1,neutralRow=0;
        for(unsigned i=0;i<5;++i){
            const auto& axis=model.axes[i];
            require(axis.size()>=2 && axis.size()<=16 && samples*axis.size()<=8192,"physique grid size");
            for(size_t j=0;j<axis.size();++j)require(std::isfinite(axis[j]) && (!j || axis[j-1]<axis[j]),"physique knots");
            const auto neutral=std::find(axis.begin(),axis.end(),neutral_physique[i]);
            require(neutral!=axis.end(),"physique neutral knot");
            neutralRow=neutralRow*axis.size()+size_t(neutral-axis.begin());samples*=axis.size();
        }
        model.rig_rank=schema.at("rigRank").get<unsigned>();
        const auto joints=schema.at("joints").size();
        require(model.rig_rank>0 && model.rig_rank<=64 && joints>0 && joints<=2048,"physique rig dimensions");
        const auto read=[&](const char* name,size_t count) {
            const auto i=schema.at(name).get<unsigned>();
            require(i<data.buffer_views_count,"physique buffer view");
            const auto& view=data.buffer_views[i];
            require(view.size==count*sizeof(float) && view.buffer && view.buffer->data &&
                view.offset<=view.buffer->size && view.size<=view.buffer->size-view.offset,"physique buffer range");
            std::vector<float> result(count);
            std::memcpy(result.data(),static_cast<const char*>(view.buffer->data)+view.offset,view.size);
            for(float v:result)require(std::isfinite(v),"physique finite coefficient");
            return result;
        };
        model.mesh_coefficients=read("meshCoefficients",samples*model.targets.size());
        model.rig_coefficients=read("rigCoefficients",samples*model.rig_rank);
        model.rig_basis=read("rigBasis",model.rig_rank*(joints*24+1));
        for(float v:model.mesh_coefficients)require(std::abs(v)<=1.00001f,"physique coefficient range");
        for(size_t i=0;i<model.targets.size();++i)require(model.mesh_coefficients[neutralRow*model.targets.size()+i]==0,"physique neutral mesh");
        for(unsigned i=0;i<model.rig_rank;++i)require(model.rig_coefficients[neutralRow*model.rig_rank+i]==0,"physique neutral rig");
        model.offsets.push_back(0);
    }
    require(!wearable || (data.animations_count == 0 && !metadata.contains("bindings") &&
        !metadata.contains("socket")), "skinned item shares body pose; no clips/socket");
    require(!rigid || (data.skins_count == 0 && (articulated || data.animations_count == 0) &&
        !metadata.contains("bindings")), "rigid item cannot own skin/animation bindings");
    std::set<const cgltf_skin *> activeSkins;
    for (const auto *node : activeNodes)
        if (node->mesh)
        {
            require(rigid ? !node->skin : node->skin != nullptr, "mesh skin/kind");
            if (node->skin) activeSkins.insert(node->skin);
        }
    const auto contract = json(profile);
    require(contract.at("id") == "human.v1" && contract.at("version") == 1, "profile version");
    const double maxIdleEdgeStretch = contract.at("gltf").at("maxIdleEdgeStretch").get<double>();
    require(std::isfinite(maxIdleEdgeStretch) && maxIdleEdgeStretch >= 1,
            "profile idle edge stretch limit");
    output->animation = std::make_shared<Animation>();
    auto &runtime = *output->animation;
    std::map<const cgltf_node *, unsigned> indices;
    skn::Matrix inverseGrip = skn::identity();
    if (rigid) {
        output->socket = metadata.at("socket").get<std::string>();
        bool known = false;
        for (const auto& socket : contract.at("sockets"))
            known = known || socket.at("name") == output->socket;
        for (const auto& bone : contract.at("skeleton").at("bones"))
            known = known || bone.at("name") == output->socket;
        require(known, "unknown item attachment node");
        auto grip = reference_joint(metadata.at("grip"));
        float q = 0;
        for (float v : grip.rotation) q += v*v;
        require(std::isfinite(q) && std::abs(q-1) < 1e-4f, "grip quaternion");
        for (float v : grip.translation) require(std::isfinite(v), "grip translation");
        for (float v : grip.scale) require(std::isfinite(v) && v > 0, "grip scale");
        const auto matrix = skn::trs(grip);
        DirectX::XMFLOAT4X4 input, result;
        std::copy_n(matrix.data(),16,&input.m[0][0]);
        DirectX::XMStoreFloat4x4(&result,DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&input)));
        std::copy_n(&result.m[0][0],16,inverseGrip.begin());
        for (float v : inverseGrip) require(std::isfinite(v), "inverse grip");
        if(metadata.contains("anchors")) for(auto it=metadata.at("anchors").begin();it!=metadata.at("anchors").end();++it) {
            require(it.key()=="cast" || it.key()=="particle", "item anchor kind");
            const auto point=it.value().get<std::array<float,3>>();
            for(float v:point)require(std::isfinite(v),"item anchor position");
            output->anchors.emplace(it.key(),skn::point(inverseGrip,point.data()));
        }
        skn::Joint socket{}; socket.name=output->socket; socket.parent=-1;
        socket.scale={1,1,1}; socket.rotation={0,0,0,1}; socket.inverse=skn::identity();
        if (articulated) {
            require(activeNodes.size()==data.nodes_count && data.nodes_count<=16 &&
                    data.animations_count>0 && data.animations_count<=256,"mechanism dimensions");
            matrix_equal(inverseGrip,skn::identity(),"mechanism grip frame");
            for (size_t i=0;i<data.nodes_count;++i) {
                const auto& node=data.nodes[i];
                require(node.mesh && !node.parent && node.name,"mechanism mesh node");
                matrix_equal(skn::trs(joint(node)),skn::identity(),"mechanism neutral transform");
                socket.name=node.name;
                output->geometry.joints.push_back(socket);
                runtime.nodes.emplace_back();
                indices.emplace(&node,unsigned(i));
            }
        } else {
            output->geometry.joints.push_back(socket);
            output->idle.push_back(skn::identity());
            runtime.nodes.emplace_back();
            for (const auto* node : activeNodes) if (node->mesh) indices.emplace(node,0);
        }
        const auto read_point=[&](const Json& value) {
            const auto p=value.get<Point3>();
            for(float v:p)require(std::isfinite(v) && std::abs(v)<100.f,"weapon constraint point");
            return p;
        };
        if(metadata.contains("stringConstraint")) {
            require(articulated,"string constraint requires articulated item");
            const auto& value=metadata.at("stringConstraint");
            require(value.at("version")==1 && value.at("strings").is_array() &&
                    value.at("strings").size()==2,"string constraint schema");
            auto constraint=std::make_unique<WeaponBowConstraint>();
            constraint->drawing_bones=value.at("drawingBones").get<std::array<std::string,4>>();
            constraint->grip_bones=value.at("gripBones").get<std::array<std::string,4>>();
            for(const auto& group:{constraint->drawing_bones,constraint->grip_bones}) {
                require(std::set<std::string>(group.begin(),group.end()).size()==group.size(),"weapon hand markers unique");
                for(const auto& name:group) {
                    bool found=false;
                    for(const auto& bone:contract.at("skeleton").at("bones"))found|=bone.at("name")==name;
                    require(found,"weapon hand marker missing in profile");
                }
            }
            std::set<unsigned> cords;
            for(unsigned i=0;i<2;++i) {
                const auto& row=value.at("strings").at(i);auto& out=constraint->strings[i];
                out.cord=row.at("cord").get<unsigned>();out.limb=row.at("limb").get<unsigned>();
                require(out.cord<data.nodes_count && out.limb<data.nodes_count && out.cord!=out.limb &&
                        cords.insert(out.cord).second,"weapon string node");
                out.outside=read_point(row.at("outside"));out.inside=read_point(row.at("inside"));
                out.tip=read_point(row.at("tip"));
                require(weapon_length(weapon_subtract(out.outside,out.inside))>1e-5f,"weapon string length");
            }
            output->bow_constraint=std::move(constraint);
        }
        if(metadata.contains("projectileAim")) {
            require(!articulated && data.nodes_count==1,"projectile aim requires one rigid node");
            matrix_equal(inverseGrip,skn::identity(),"projectile aim grip frame");
            const auto& value=metadata.at("projectileAim");
            require(value.at("version")==1,"projectile aim schema");
            auto aim=std::make_unique<WeaponProjectileAim>();
            aim->nock=read_point(value.at("nock"));aim->forward=read_point(value.at("forward"));
            require(std::abs(weapon_length(aim->forward)-1.f)<1e-3f,"projectile direction must be unit length");
            output->projectile_aim=std::move(aim);
        }
        if(metadata.contains("twoHandSupport")) {
            require(!output->projectile_aim && !output->bow_constraint && (articulated || data.nodes_count==1),
                    "two-hand support requires a compatible rigid or articulated item");
            matrix_equal(inverseGrip,skn::identity(),"two-hand support grip frame");
            const auto& value=metadata.at("twoHandSupport");
            require(value.at("version")==1,"two-hand support schema");
            auto support=std::make_unique<WeaponTwoHandSupport>();
            support->grip=read_point(value.at("grip"));support->axis=read_point(value.at("axis"));
            require(std::abs(weapon_length(support->axis)-1.f)<1e-3f,"two-hand support axis");
            support->upper=value.at("upper").get<std::string>();
            support->forearm=value.at("forearm").get<std::string>();
            support->hand=value.at("hand").get<std::string>();
            support->markers=value.at("markers").get<std::array<std::string,4>>();
            support->clip_prefixes=value.at("clipPrefixes").get<std::vector<std::string>>();
            support->full_correction=value.at("fullCorrection").get<float>();
            support->max_correction=value.at("maxCorrection").get<float>();
            require(std::isfinite(support->full_correction)&&std::isfinite(support->max_correction)&&
                    support->full_correction>0 && support->full_correction<support->max_correction &&
                    support->max_correction<=1.f,"two-hand support correction limits");
            require(!support->clip_prefixes.empty() && support->clip_prefixes.size()<=8,"two-hand support clips");
            for(const auto& prefix:support->clip_prefixes)
                require(!prefix.empty() && prefix.size()<=32,"two-hand support clip prefix");
            if(value.contains("clipWindows")) {
                support->clip_windows=value.at("clipWindows").get<std::map<std::string,std::array<float,4>>>();
                require(support->clip_windows.size()==support->clip_prefixes.size(),"two-hand support window count");
                for(const auto& prefix:support->clip_prefixes) {
                    const auto it=support->clip_windows.find(prefix);
                    require(it!=support->clip_windows.end(),"two-hand support window clip");
                    const auto& w=it->second;
                    require(std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]) && std::isfinite(w[3]) &&
                            w[0]>=0 && w[0]<w[1] && w[1]<w[2] && w[2]<w[3] && w[3]<=1,
                            "invalid two-hand support window");
                }
            }
            auto bones=support->markers;
            require(std::set<std::string>(bones.begin(),bones.end()).size()==bones.size(),"two-hand markers unique");
            for(const auto& name:{support->upper,support->forearm,support->hand,bones[0],bones[1],bones[2],bones[3]}) {
                bool found=false;
                for(const auto& bone:contract.at("skeleton").at("bones"))found|=bone.at("name")==name;
                require(found,"two-hand support bone missing in profile");
            }
            if(value.contains("gripMirror")) {
                const auto& mirror=value.at("gripMirror");
                require(mirror.at("version")==1,"two-hand mirrored grip schema");
                auto grip=std::make_unique<WeaponGripMirror>();
                grip->primary_upper=mirror.at("primaryUpper").get<std::string>();
                grip->primary_forearm=mirror.at("primaryForearm").get<std::string>();
                grip->primary_hand=mirror.at("primaryHand").get<std::string>();
                grip->primary_markers=mirror.at("primaryMarkers").get<std::array<std::string,4>>();
                grip->width_bones=mirror.at("widthBones").get<std::array<std::string,2>>();
                grip->bind_normal=read_point(mirror.at("bindNormal"));
                require(std::abs(weapon_length(grip->bind_normal)-1.f)<1e-3f,"grip mirror normal");
                grip->pairs=mirror.at("pairs").get<std::vector<std::array<std::string,2>>>();
                require(!grip->pairs.empty() && grip->pairs.size()<=64,"grip mirror pair count");
                std::set<std::string> left,right;
                std::vector<std::string> names{grip->primary_upper,grip->primary_forearm,grip->primary_hand,
                    grip->width_bones[0],grip->width_bones[1]};
                names.insert(names.end(),grip->primary_markers.begin(),grip->primary_markers.end());
                for(const auto& pair:grip->pairs) {
                    require(left.insert(pair[0]).second && right.insert(pair[1]).second,"duplicate grip mirror pair");
                    names.insert(names.end(),pair.begin(),pair.end());
                }
                require(std::find(grip->pairs.begin(),grip->pairs.end(),
                    std::array<std::string,2>{support->hand,grip->primary_hand})!=grip->pairs.end(),
                    "grip mirror hand pair");
                for(const auto& name:names) {
                    bool found=false;
                    for(const auto& bone:contract.at("skeleton").at("bones"))found|=bone.at("name")==name;
                    require(found,"grip mirror bone missing in profile");
                }
                support->mirror=std::move(grip);
            }
            output->two_hand_support=std::move(support);
        }
    } else {
    std::map<std::string, const Json *> reference;
    for (const auto &bone : contract.at("skeleton").at("bones"))
        reference.emplace(bone.at("name").get<std::string>(), &bone);
    for (const auto &socket : contract.at("sockets"))
        reference.emplace(socket.at("name").get<std::string>(), &socket);
    std::set<const cgltf_node *> skinJoints;
    for (const auto *skin : activeSkins)
        for (std::size_t j = 0; j < skin->joints_count; ++j)
            skinJoints.insert(skin->joints[j]);
    std::set<const cgltf_node *> visiting;
    std::map<std::string, unsigned> names;
    std::vector<const cgltf_node *> nodes;
    std::function<void(const cgltf_node *)> append = [&](const cgltf_node *node) {
        if (indices.count(node))
            return;
        require(visiting.insert(node).second, "skeleton cycle");
        require(nodes.size() < 65535, "joint limit");
        if (node->parent)
            append(node->parent);
        const std::string name = node->name ? node->name : "";
        auto found = reference.find(name);
        require(!skinJoints.count(node) || found != reference.end(), "unknown joint " + name);
        auto j = joint(*node);
        j.parent = node->parent ? static_cast<int>(indices.at(node->parent)) : -1;
        j.inverse = skn::identity();
        if (found != reference.end())
        {
            const auto &expected = *found->second;
            const auto *parentNode = node->parent;
            while (parentNode && !reference.count(parentNode->name ? parentNode->name : ""))
                parentNode = parentNode->parent;
            const std::string parent = parentNode && parentNode->name ? parentNode->name : "";
            require(parent == (expected.at("parent").is_null() ? "" :
                                expected.at("parent").get<std::string>()), "parent " + name);
            require(names.emplace(name, static_cast<unsigned>(nodes.size())).second,
                    "duplicate node " + name);
            // Rest frames are relative to the nearest profile parent; the
            // root frame includes all armature/container ancestors.
            skn::Matrix local = skn::trs(j);
            for (auto *ancestor = node->parent; ancestor != parentNode; ancestor = ancestor->parent)
                local = skn::multiply(skn::trs(joint(*ancestor)), local);
            matrix_equal(local, skn::trs(reference_joint(expected)), name);
            if (expected.contains("inverseBindMatrix"))
                j.inverse = expected.at("inverseBindMatrix").get<skn::Matrix>();
        }
        else
            j.name = "#gltf-node-" + std::to_string(node - data.nodes);
        indices.emplace(node, static_cast<unsigned>(nodes.size()));
        nodes.push_back(node);
        output->geometry.joints.push_back(j);
        visiting.erase(node);
    };
    require(!activeSkins.empty(), "active body skin required");
    for (const auto *skin : activeSkins)
        for (std::size_t j = 0; j < skin->joints_count; ++j)
        {
            require(activeNodes.count(skin->joints[j]), "skin joint outside active scene");
            append(skin->joints[j]);
        }
    for (std::size_t i = 0; i < data.nodes_count; ++i)
        if (activeNodes.count(&data.nodes[i]) && data.nodes[i].name &&
            reference.count(data.nodes[i].name))
            append(&data.nodes[i]);
    // Mesh nodes own weights channels even when they are not skin joints.
    for (const auto *node : activeNodes)
        if (node->mesh) append(node);
    for (const auto &bone : contract.at("skeleton").at("bones"))
        if (!wearable && bone.at("required").get<bool>())
            require(names.count(bone.at("name").get<std::string>()),
                    "missing bone " + bone.at("name").get<std::string>());
    for (const auto &socket : contract.at("sockets"))
        require(wearable || names.count(socket.at("name").get<std::string>()),
                "missing socket " + socket.at("name").get<std::string>());
    for (const auto *activeSkin : activeSkins)
    {
        const auto &skin = *activeSkin;
        require(skin.inverse_bind_matrices && skin.inverse_bind_matrices->type == cgltf_type_mat4,
                "inverse bind matrices");
        auto matrices = values(skin.inverse_bind_matrices);
        require(matrices.size() == skin.joints_count * 16, "inverse bind count");
        for (std::size_t j = 0; j < skin.joints_count; ++j)
        {
            skn::Matrix inverse;
            std::copy_n(matrices.data() + j * 16, 16, inverse.begin());
            matrix_equal(inverse, output->geometry.joints[indices.at(skin.joints[j])].inverse,
                         skin.joints[j]->name);
        }
    }
    // Keep the complete cgltf document for later animation/morph evaluation.
    // This slice samples the first idle key, with bind defaults for absent channels.
    std::vector<skn::Matrix> bind;
    for (const auto &joint : output->geometry.joints)
    {
        auto matrix = skn::trs(joint);
        if (joint.parent >= 0)
            matrix = skn::multiply(bind[joint.parent], matrix);
        bind.push_back(matrix);
        if (reference.count(joint.name) && reference.at(joint.name)->contains("inverseBindMatrix"))
            matrix_equal(skn::multiply(matrix, joint.inverse), skn::identity(), joint.name);
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto &j = output->geometry.joints[i];
        Node n; n.parent = j.parent; n.rotation = j.rotation;
        std::copy_n(j.translation.begin(), 3, n.translation.begin());
        std::copy_n(j.scale.begin(), 3, n.scale.begin());
        if (nodes[i]->mesh) {
            require(nodes[i]->mesh->primitives_count > 0, "empty evaluation mesh");
            const auto count = nodes[i]->mesh->primitives[0].targets_count;
            require(count <= 256, "morph target limit");
            require(!wearable || count == 0 || physique, "skinned item cannot own unbound morph weights");
            const auto *w = nodes[i]->weights ? nodes[i]->weights : nodes[i]->mesh->weights;
            const auto size = nodes[i]->weights ? nodes[i]->weights_count : nodes[i]->mesh->weights_count;
            require(!w || size == count, "morph defaults");
            n.weights.resize(count, 0); if (w) std::copy_n(w, count, n.weights.begin());
            if(physique){
                const auto names=nodes[i]->mesh->target_names;
                require(count==output->physique.targets.size() && names && nodes[i]->mesh->target_names_count==count,"physique target count");
                for(unsigned j=0;j<count;++j)
                    require(names[j] && names[j]==output->physique.targets[j] && n.weights[j]==0,"physique name/default");
            }
        }
        runtime.nodes.push_back(std::move(n));
    }
    if(physique) {
        auto& model=output->physique;
        const auto& schema=metadata.at("physique");
        std::map<std::string,unsigned> named;
        for(unsigned i=0;i<nodes.size();++i)if(nodes[i]->name)require(named.emplace(nodes[i]->name,i).second,"ambiguous physique node name");
        model.root=named.at(schema.at("root").get<std::string>());
        const auto& soles=schema.at("soles");require(soles.size()==2,"physique soles");
        for(unsigned i=0;i<2;++i){
            output->sole_nodes[i]=named.at(soles.at(i).at("joint").get<std::string>());
            output->sole_points[i]=soles.at(i).at("point").get<std::array<float,3>>();
            for(float v:output->sole_points[i])require(std::isfinite(v),"physique sole point");
        }
        std::set<unsigned> visited{model.root};
        for(const auto& row:schema.at("joints")){
            require(row.is_array() && row.size()==3,"physique joint record");
            Physique::Joint j{named.at(row.at(0).get<std::string>()),named.at(row.at(1).get<std::string>()),row.at(2).get<int>()};
            require(j.parent>=-1 && j.parent<int(schema.at("joints").size()) && visited.insert(j.node).second &&
                visited.insert(j.carrier).second && runtime.nodes[j.node].parent==int(j.carrier),"physique joint binding");
            model.joints.push_back(j);
        }
    }
    }
    if (!rigid || articulated) {
    auto pose = output->geometry.joints;
    std::set<std::string> clips;
    for (std::size_t a = 0; a < data.animations_count; ++a)
    {
        const auto &animation = data.animations[a];
        require(animation.name && clips.insert(animation.name).second, "animation names");
        Clip runtimeClip; runtimeClip.name = animation.name;
        const auto animationExtras = extras(animation.extras);
        if (animationExtras.contains("ei") && animationExtras.at("ei").contains("events"))
            for (const auto &event : animationExtras.at("ei").at("events")) {
                require(event.at("payload").is_object(), "event payload object");
                runtimeClip.events.push_back({event.at("id").get<std::string>(),
                    event.at("name").get<std::string>(), event.at("payload").dump(), event.at("time").get<float>()});
            }
        std::set<std::pair<const cgltf_node *, cgltf_animation_path_type>> targets;
        for (std::size_t c = 0; c < animation.channels_count; ++c)
        {
            const auto &channel = animation.channels[c];
            if(articulated) require(channel.target_path!=cgltf_animation_path_type_weights &&
                channel.sampler && channel.sampler->interpolation==cgltf_interpolation_type_linear,
                "mechanism requires local linear TRS");
            require(channel.target_node && channel.sampler, "animation channel");
            require(targets.emplace(channel.target_node, channel.target_path).second,
                    "duplicate animation node/path");
            const auto &sampler = *channel.sampler;
            require(sampler.input && sampler.input->type == cgltf_type_scalar &&
                        sampler.input->component_type == cgltf_component_type_r_32f &&
                        sampler.output &&
                        sampler.output->component_type == cgltf_component_type_r_32f,
                    "animation accessor type");
            require(sampler.interpolation == cgltf_interpolation_type_step ||
                        sampler.interpolation == cgltf_interpolation_type_linear ||
                        sampler.interpolation == cgltf_interpolation_type_cubic_spline,
                    "animation interpolation");
            std::size_t components = 0;
            switch (channel.target_path)
            {
            case cgltf_animation_path_type_translation:
            case cgltf_animation_path_type_scale:
                require(sampler.output->type == cgltf_type_vec3, "animation TRS type");
                components = 3;
                break;
            case cgltf_animation_path_type_rotation:
                require(sampler.output->type == cgltf_type_vec4, "animation rotation type");
                components = 4;
                break;
            case cgltf_animation_path_type_weights:
                require(!physique,"physique weights controlled by appearance");
                require(channel.target_node->mesh &&
                            channel.target_node->mesh->primitives_count &&
                            sampler.output->type == cgltf_type_scalar,
                        "animation morph target");
                components = channel.target_node->mesh->primitives[0].targets_count;
                require(components > 0, "animation morph count");
                break;
            default:
                require(false, "animation target path");
            }
            const auto times = values(channel.sampler->input),
                       samples = values(channel.sampler->output);
            require(!times.empty() && times.front() >= 0, "animation time");
            const std::size_t keysPerSample = sampler.interpolation ==
                                                    cgltf_interpolation_type_cubic_spline
                                                ? 3 : 1;
            require(samples.size() == times.size() * components * keysPerSample,
                    "animation sample count");
            for (std::size_t k = 1; k < times.size(); ++k)
                require(times[k] > times[k - 1], "animation times increasing");
            runtimeClip.duration = (std::max)(runtimeClip.duration, times.back());
            if (indices.count(channel.target_node)) {
                Track track; track.node = indices.at(channel.target_node);
                track.width = static_cast<unsigned>(components); track.times = times; track.values = samples;
                track.interpolation = sampler.interpolation == cgltf_interpolation_type_step ? Interpolation::step :
                    sampler.interpolation == cgltf_interpolation_type_linear ? Interpolation::linear : Interpolation::cubic;
                track.path = channel.target_path == cgltf_animation_path_type_translation ? Path::translation :
                    channel.target_path == cgltf_animation_path_type_rotation ? Path::rotation :
                    channel.target_path == cgltf_animation_path_type_scale ? Path::scale : Path::weights;
                runtimeClip.tracks.push_back(std::move(track));
            }
            if (std::strcmp(animation.name, "idle") || !indices.count(channel.target_node))
                continue;
            auto &j = pose[indices.at(channel.target_node)];
            const auto width = cgltf_num_components(channel.sampler->output->type);
            require((channel.target_path == cgltf_animation_path_type_rotation && width == 4) ||
                        ((channel.target_path == cgltf_animation_path_type_translation ||
                          channel.target_path == cgltf_animation_path_type_scale) &&
                         width == 3) ||
                        channel.target_path == cgltf_animation_path_type_weights,
                    "animation channel width");
            const auto offset = channel.sampler->interpolation ==
                                        cgltf_interpolation_type_cubic_spline
                                    ? width
                                    : 0;
            require(samples.size() >= offset + width, "animation output");
            if (channel.target_path == cgltf_animation_path_type_translation)
                std::copy_n(samples.data() + offset, 3, j.translation.begin());
            if (channel.target_path == cgltf_animation_path_type_rotation)
                std::copy_n(samples.data() + offset, 4, j.rotation.begin());
            if (channel.target_path == cgltf_animation_path_type_scale)
                std::copy_n(samples.data() + offset, 3, j.scale.begin());
        }
        runtime.clips.push_back(std::move(runtimeClip));
    }
    for (std::size_t j = 0; j < pose.size(); ++j)
    {
        float norm = 0;
        for (float value : pose[j].rotation)
            norm += value * value;
        require(std::abs(norm - 1) <= 1e-4f, "idle quaternion");
        for (float value : pose[j].scale)
            require(std::isfinite(value) && value > 0, "idle scale");
        auto m = skn::trs(pose[j]);
        if (pose[j].parent >= 0)
            m = skn::multiply(output->idle[pose[j].parent], m);
        output->idle.push_back(m);
    }
    } // body skeleton/animation
    cgltf_material defaultMaterial{};
    std::fill_n(defaultMaterial.pbr_metallic_roughness.base_color_factor, 4, 1.f);
    defaultMaterial.pbr_metallic_roughness.metallic_factor = 1;
    defaultMaterial.pbr_metallic_roughness.roughness_factor = 1;
    defaultMaterial.alpha_cutoff = .5f;
    bool defaultNeeded = false;
    for (std::size_t m = 0; m < data.meshes_count; ++m)
        for (std::size_t p = 0; p < data.meshes[m].primitives_count; ++p)
            defaultNeeded = defaultNeeded || !data.meshes[m].primitives[p].material;
    for (std::size_t m = 0; m < data.materials_count + (defaultNeeded ? 1 : 0); ++m)
    {
        const auto &input = m < data.materials_count ? data.materials[m] : defaultMaterial;

        const auto &pbr = input.pbr_metallic_roughness;
        std::array<float,4> baseFactor{};
        std::copy_n(pbr.base_color_factor, 4, baseFactor.begin());
        output->base_color_factors.push_back(baseFactor);
        skn::Material material{};
        std::copy_n(pbr.base_color_factor, 4, material.base);
        std::copy_n(input.emissive_factor, 3, material.emission);
        for (unsigned c = 0; c < 3; ++c)
        {
            material.base[c] = encoded(material.base[c]);
            material.emission[c] = encoded(material.emission[c]);
        }
        material.unlit = input.unlit;
        for (float v : pbr.base_color_factor)
            require(std::isfinite(v) && v >= 0 && v <= 1, "base color factor");
        for (float v : input.emissive_factor)
            require(std::isfinite(v) && v >= 0, "emission factor");
        require(std::isfinite(pbr.roughness_factor) && pbr.roughness_factor >= 0 &&
                    pbr.roughness_factor <= 1 && std::isfinite(pbr.metallic_factor) &&
                    pbr.metallic_factor >= 0 && pbr.metallic_factor <= 1,
                "material factors");
        material.roughness = pbr.roughness_factor;
        material.metallic = pbr.metallic_factor;
        material.ao = 1;
        material.alpha = static_cast<unsigned>(input.alpha_mode);
        require(std::isfinite(input.alpha_cutoff) && input.alpha_cutoff >= 0,
                "alpha cutoff");
        material.cutoff = input.alpha_cutoff;
        material.two_sided = input.double_sided;
        const cgltf_texture_view *views[] = { &pbr.base_color_texture, &input.normal_texture,
                                              &pbr.metallic_roughness_texture,
                                              &input.emissive_texture };
        std::array<skn::Dds, 4> textures;
        for (unsigned k = 0; k < 4; ++k)
            if (views[k]->texture)
            {
                require(views[k]->texture->image, "texture image");
                const float *factor = k == 0   ? pbr.base_color_factor
                                      : k == 3 ? input.emissive_factor
                                               : nullptr;
                textures[k] = image(publish, blob, *views[k]->texture->image, k == 0 || k == 3,
                                    factor, k == 2 ? &input.occlusion_texture : nullptr,
                                    k == 1 ? input.normal_texture.scale : 1, k == 2);
                if (k == 0)
                    std::fill_n(material.base, 3, 1.f);
                if (k == 3)
                    std::fill_n(material.emission, 3, 1.f);
                material.maps[k] = blob.identity.canonical_name + "#" + std::to_string(m) + "/" +
                                   std::to_string(k);
            }
        if (input.occlusion_texture.texture && !pbr.metallic_roughness_texture.texture)
        {
            require(input.occlusion_texture.texture->image, "occlusion image");
            require(std::isfinite(input.occlusion_texture.scale) &&
                        input.occlusion_texture.scale >= 0 && input.occlusion_texture.scale <= 1,
                    "occlusion strength");
            auto ao = image(publish, blob, *input.occlusion_texture.texture->image, false);
            // A standalone AO texture supplies red. G/B become neutral roughness/metallic.
            auto bytes = std::make_shared<std::vector<std::uint8_t>>(*ao.blob.bytes);
            for (std::size_t i = 0; i < bytes->size(); i += 4)
            {
                const float value = (*bytes)[i + 2] / 255.f;
                (*bytes)[i + 2] = static_cast<std::uint8_t>(std::lround(
                    (1 - input.occlusion_texture.scale + input.occlusion_texture.scale * value) *
                    255));
                (*bytes)[i] = (*bytes)[i + 1] = 255;
            }
            auto packed = publish(blob.identity.canonical_name + "#ao", bytes->data(),
                                  static_cast<std::uint32_t>(bytes->size()));
            for (auto &mip : ao.mips)
                mip.data = packed.bytes->data() + (mip.data - ao.blob.bytes->data());
            ao.blob = std::move(packed);
            textures[2] = std::move(ao);
        }
        if (textures[3].mips.empty() &&
            (material.emission[0] > 0 || material.emission[1] > 0 || material.emission[2] > 0))
        {
            static const std::uint32_t white = 0xffffffffu;
            textures[3].blob = publish(blob.identity.canonical_name + "#emission", &white, 4);
            textures[3].format = NativeOwnedTextureFormat::argb8888;
            textures[3].mips.push_back({ textures[3].blob.bytes->data(), 1, 1, 4 });
        }
        output->geometry.materials.push_back(material);
        output->textures.push_back(std::move(textures));
    }
    for (std::size_t n = 0; n < data.nodes_count; ++n)
    {
        const auto &node = data.nodes[n];
        if (!activeNodes.count(&node) || !node.mesh)
            continue;
        require(rigid ? !node.skin : node.skin != nullptr, "mesh skin/kind");
        float world[16];
        cgltf_node_transform_world(&node, world);
        skn::Matrix meshWorld;
        std::copy_n(world, 16, meshWorld.begin());
        if (!rigid) matrix_equal(meshWorld, skn::identity(), "mesh container");
        else {
            for (auto* ancestor=&node; ancestor; ancestor=ancestor->parent) joint(*ancestor);
            meshWorld=skn::multiply(inverseGrip,meshWorld);
            for (float v : meshWorld) require(std::isfinite(v), "item transform");
        }
        for (std::size_t p = 0; p < node.mesh->primitives_count; ++p)
        {
            const auto &primitive = node.mesh->primitives[p];
            require(primitive.type == cgltf_primitive_type_triangles,
                    "triangle primitive required");
            const auto *material = primitive.material ? primitive.material : &defaultMaterial;
            const auto *positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position,
                                                        0);
            const auto *normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const auto *joints = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
            const auto *weights = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);
            const auto &pbr = material->pbr_metallic_roughness;
            const cgltf_texture_view *candidates[] = {
                &pbr.base_color_texture, &material->normal_texture, &pbr.metallic_roughness_texture,
                &material->emissive_texture, &material->occlusion_texture
            };
            const cgltf_texture_view *selected = nullptr;
            for (const auto *candidate : candidates)
                if (candidate->texture)
                {
                    require(candidate->texcoord == contract.at("materials").at("textureTexCoord").get<int>(),
                            "human.v1 textures require TEXCOORD_0");
                    require(!candidate->has_transform,
                            "human.v1 forbids KHR_texture_transform");
                    if (!selected)
                        selected = candidate;
                }
            const auto *uv = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
            require(!selected || uv, "texture coordinate attribute required");
            const auto *tangent = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
            // human.v1 normal maps use the authored glTF tangent basis.
            require(!material->normal_texture.texture || tangent,
                    "human.v1 normal map requires authored TANGENT");
            require(positions && normals && (rigid ? !joints && !weights && !primitive.targets_count : joints && weights) &&
                        !cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 1),
                    "vertex attributes/influences");
            auto pos = values(positions), normal = values(normals);
            auto js = rigid ? std::vector<float>(positions->count*4,0) : values(joints);
            auto ws = rigid ? std::vector<float>(positions->count*4,0) : values(weights);
            if (rigid) for (std::size_t v=0;v<positions->count;++v) ws[v*4]=1;
            auto tex = uv ? values(uv) : std::vector<float>(positions->count * 2, 0);
            auto tan = tangent ? values(tangent) : std::vector<float>();
            require(!tangent || tangent->type == cgltf_type_vec4, "tangent VEC4 required");
            require(tan.empty() || tan.size() == positions->count * 4, "tangent size");
            for (std::size_t i = 3; i < tan.size(); i += 4)
                require(tan[i] == -1.f || tan[i] == 1.f, "tangent handedness");
            for (float coordinate : tex)
                require(std::isfinite(coordinate) && std::abs(coordinate) <= 65504.f,
                        "UV outside GPU half-float range");
            require(pos.size() == positions->count * 3 && normal.size() == pos.size() &&
                        js.size() == positions->count * 4 && ws.size() == js.size() &&
                        tex.size() == positions->count * 2,
                    "attribute sizes");
            if (tan.empty())
            {
                tan.assign(positions->count * 4, 0);
                std::vector<std::array<float, 3>> bitangent(positions->count);
                const auto count = primitive.indices ? primitive.indices->count : positions->count;
                require(count % 3 == 0, "triangle count");
                for (std::size_t t = 0; t < count; t += 3)
                {
                    std::size_t v[3];
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        v[k] = primitive.indices
                                   ? cgltf_accessor_read_index(primitive.indices, t + k)
                                   : t + k;
                        require(v[k] < positions->count, "triangle vertex");
                    }
                    const float du1 = tex[v[1] * 2] - tex[v[0] * 2],
                                dv1 = tex[v[1] * 2 + 1] - tex[v[0] * 2 + 1];
                    const float du2 = tex[v[2] * 2] - tex[v[0] * 2],
                                dv2 = tex[v[2] * 2 + 1] - tex[v[0] * 2 + 1];
                    const float determinant = du1 * dv2 - dv1 * du2;
                    if (std::abs(determinant) < 1e-12f)
                        continue;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const float e1 = pos[v[1] * 3 + c] - pos[v[0] * 3 + c],
                                    e2 = pos[v[2] * 3 + c] - pos[v[0] * 3 + c];
                        for (unsigned k = 0; k < 3; ++k)
                        {
                            tan[v[k] * 4 + c] += (e1 * dv2 - e2 * dv1) / determinant;
                            bitangent[v[k]][c] += (e2 * du1 - e1 * du2) / determinant;
                        }
                    }
                }
                for (std::size_t i = 0; i < positions->count; ++i)
                {
                    float dot = 0;
                    for (unsigned c = 0; c < 3; ++c)
                        dot += normal[i * 3 + c] * tan[i * 4 + c];
                    float norm = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        tan[i * 4 + c] -= normal[i * 3 + c] * dot;
                        norm += tan[i * 4 + c] * tan[i * 4 + c];
                    }
                    if (norm < 1e-12f)
                    {
                        const auto *n = normal.data() + i * 3;
                        tan[i * 4] = std::abs(n[2]) < .9f ? -n[1] : 0;
                        tan[i * 4 + 1] = std::abs(n[2]) < .9f ? n[0] : -n[2];
                        tan[i * 4 + 2] = std::abs(n[2]) < .9f ? 0 : n[1];
                        norm = tan[i * 4] * tan[i * 4] + tan[i * 4 + 1] * tan[i * 4 + 1] +
                               tan[i * 4 + 2] * tan[i * 4 + 2];
                    }
                    for (unsigned c = 0; c < 3; ++c)
                        tan[i * 4 + c] /= std::sqrt(norm);
                    const auto *n = normal.data() + i * 3;
                    const auto *t = tan.data() + i * 4;
                    const auto &b = bitangent[i];
                    const float handedness = (n[1] * t[2] - n[2] * t[1]) * b[0] +
                                             (n[2] * t[0] - n[0] * t[2]) * b[1] +
                                             (n[0] * t[1] - n[1] * t[0]) * b[2];
                    tan[i * 4 + 3] = handedness < 0 ? -1.f : 1.f;
                }
            }
            if (rigid) {
                // Bake hierarchy and inverse grip once. The GPU uses the body's
                // socket matrix with identity inverse bind, without a second pose.
                DirectX::XMFLOAT4X4 input, inverse;
                std::copy_n(meshWorld.data(),16,&input.m[0][0]);
                DirectX::XMStoreFloat4x4(&inverse,DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&input)));
                const float* inv=&inverse.m[0][0];
                for (std::size_t v=0;v<positions->count;++v) {
                    const auto p=skn::point(meshWorld,pos.data()+v*3);
                    float n[3]{},t[3]{};
                    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) {
                        n[r]+=inv[r*4+c]*normal[v*3+c];
                        t[r]+=meshWorld[c*4+r]*tan[v*4+c];
                    }
                    float nl=0,tl=0;
                    for (unsigned c=0;c<3;++c) { nl+=n[c]*n[c];tl+=t[c]*t[c]; }
                    require(std::isfinite(nl)&&std::isfinite(tl)&&nl>1e-12f&&tl>1e-12f,"item normal/tangent");
                    for (unsigned c=0;c<3;++c) {
                        pos[v*3+c]=p[c];normal[v*3+c]=n[c]/std::sqrt(nl);tan[v*4+c]=t[c]/std::sqrt(tl);
                    }
                }
            }
            const auto restPos = pos, restNormal = normal, restTan = tan;
            require(primitive.targets_count <= 256 &&
                std::uint64_t(positions->count) * primitive.targets_count * 36 <= 512ull*1024*1024,
                "GPU morph storage limit");
            std::vector<std::vector<std::array<float,9>>> morphDeltas(positions->count,
                std::vector<std::array<float,9>>(primitive.targets_count));
            require(primitive.targets_count == runtime.nodes.at(indices.at(&node)).weights.size(), "primitive morph count");
            std::vector<float> idleWeights;
            for (std::size_t a = 0; a < data.animations_count; ++a)
            {
                const auto &animation = data.animations[a];
                if (!animation.name || std::strcmp(animation.name, "idle"))
                    continue;
                for (std::size_t c = 0; c < animation.channels_count; ++c)
                {
                    const auto &channel = animation.channels[c];
                    if (channel.target_node != &node ||
                        channel.target_path != cgltf_animation_path_type_weights)
                        continue;
                    const auto samples = values(channel.sampler->output);
                    const auto width = primitive.targets_count;
                    const auto offset = channel.sampler->interpolation ==
                                                cgltf_interpolation_type_cubic_spline
                                            ? width
                                            : 0;
                    require(samples.size() >= offset + width, "morph animation output");
                    idleWeights.assign(samples.begin() + offset, samples.begin() + offset + width);
                }
            }
            const float *morphWeights = !idleWeights.empty() ? idleWeights.data()
                                        : node.weights       ? node.weights
                                                             : node.mesh->weights;
            const auto morphCount = !idleWeights.empty() ? idleWeights.size()
                                    : node.weights       ? node.weights_count
                                                         : node.mesh->weights_count;
            require(!morphWeights || morphCount == primitive.targets_count, "morph default count");
            for (std::size_t t = 0; t < primitive.targets_count; ++t)
            {
                const float weight = morphWeights ? morphWeights[t] : 0;
                require(std::isfinite(weight), "nonfinite morph weight");
                const auto &target = primitive.targets[t];
                for (std::size_t a = 0; a < target.attributes_count; ++a)
                {
                    const auto &attribute = target.attributes[a];
                    const auto delta = values(attribute.data);
                    require(delta.size() == positions->count * 3, "morph accessor size");
                    const unsigned lane = attribute.type == cgltf_attribute_type_position ? 0 :
                        attribute.type == cgltf_attribute_type_normal ? 3 : 6;
                    require(attribute.type == cgltf_attribute_type_position || attribute.type == cgltf_attribute_type_normal ||
                        attribute.type == cgltf_attribute_type_tangent, "morph attribute kind");
                    for (std::size_t v = 0; v < positions->count; ++v)
                        std::copy_n(delta.data()+v*3,3,morphDeltas[v][t].begin()+lane);
                    if (attribute.type == cgltf_attribute_type_position)
                        for (std::size_t k = 0; k < delta.size(); ++k)
                            pos[k] += weight * delta[k];
                    if (attribute.type == cgltf_attribute_type_normal)
                        for (std::size_t k = 0; k < delta.size(); ++k)
                            normal[k] += weight * delta[k];
                    if (attribute.type == cgltf_attribute_type_tangent && !tan.empty())
                        for (std::size_t k = 0; k < positions->count; ++k)
                            for (unsigned c = 0; c < 3; ++c)
                                tan[k * 4 + c] += weight * delta[k * 3 + c];
                }
            }
            for (std::size_t i = 0; i < positions->count; ++i)
            {
                float norm = 0;
                for (unsigned c = 0; c < 3; ++c)
                    norm += normal[i * 3 + c] * normal[i * 3 + c];
                require(std::isfinite(norm) && norm > 1e-12f, "invalid vertex normal");
                for (unsigned c = 0; c < 3; ++c)
                    normal[i * 3 + c] /= std::sqrt(norm);
            }
            // Morph deltas can change the authored tangent length and orthogonality.
            // Integer packing must never receive a nonfinite value.
            for (std::size_t i = 0; i < positions->count; ++i)
            {
                float dot = 0;
                for (unsigned c = 0; c < 3; ++c)
                    dot += normal[i * 3 + c] * tan[i * 4 + c];
                float norm = 0;
                for (unsigned c = 0; c < 3; ++c)
                {
                    tan[i * 4 + c] -= normal[i * 3 + c] * dot;
                    norm += tan[i * 4 + c] * tan[i * 4 + c];
                    require(std::isfinite(pos[i * 3 + c]), "nonfinite morphed position");
                }
                require(std::isfinite(norm) && norm > 1e-12f, "invalid vertex tangent");
                for (unsigned c = 0; c < 3; ++c)
                    tan[i * 4 + c] /= std::sqrt(norm);
            }
            const unsigned base = static_cast<unsigned>(output->geometry.vertices.size());
            std::vector<std::array<float, 3>> idlePositions;
            idlePositions.reserve(positions->count);
            for (std::size_t v = 0; v < positions->count; ++v)
            {
                skn::Vertex vertex{};
                std::copy_n(pos.data() + v * 3, 3, vertex.position);
                float sum = 0;
                for (unsigned k = 0; k < 4; ++k)
                {
                    require(ws[v * 4 + k] >= 0, "negative weight");
                    sum += ws[v * 4 + k];
                }
                require(std::isfinite(sum) && std::abs(sum - 1.f) <= 1e-4f,
                        "skin weights must sum to one");
                unsigned total = 0, biggest = 0;
                for (unsigned k = 0; k < 4; ++k)
                {
                    const float index = js[v * 4 + k];
                    require(index >= 0 && index < (rigid ? 1 : node.skin->joints_count) &&
                                index == std::floor(index),
                            "joint index");
                    vertex.joints[k] = static_cast<std::uint16_t>(
                        rigid ? indices.at(&node) : indices.at(node.skin->joints[static_cast<unsigned>(index)]));
                    vertex.weights[k] = static_cast<std::uint16_t>(
                        std::floor(ws[v * 4 + k] / sum * 65535));
                    total += vertex.weights[k];
                    if (ws[v * 4 + k] > ws[v * 4 + biggest])
                        biggest = k;
                    const float t = tan.empty() ? (k == 0 || k == 3 ? 1.f : 0.f) : tan[v * 4 + k];
                    vertex.tangent[k] = static_cast<std::int16_t>(
                        std::lround((std::clamp)(t, -1.f, 1.f) * 32767));
                    if (k < 3)
                        vertex.normal[k] = static_cast<std::int16_t>(
                            std::lround((std::clamp)(normal[v * 3 + k], -1.f, 1.f) * 32767));
                }
                vertex.weights[biggest] += static_cast<std::uint16_t>(65535 - total);
                for (unsigned k = 0; k < 2; ++k)
                    vertex.uv[k] = DirectX::PackedVector::XMConvertFloatToHalf(tex[v * 2 + k]);
                output->geometry.vertices.push_back(vertex);
                auto rest = vertex;
                std::copy_n(restPos.data()+v*3,3,rest.position);
                float length = 0; for (unsigned c=0;c<3;++c) length += restNormal[v*3+c]*restNormal[v*3+c];
                require(std::abs(length-1.f)<.002f, "rest normal must be unit length");
                for (unsigned c=0;c<3;++c) rest.normal[c]=static_cast<std::int16_t>(std::lround(
                    (std::clamp)(restNormal[v*3+c],-1.f,1.f)*32767));
                if (!restTan.empty()) for (unsigned c=0;c<4;++c) rest.tangent[c]=static_cast<std::int16_t>(std::lround(
                    (std::clamp)(restTan[v*4+c],-1.f,1.f)*32767));
                output->rest_vertices.push_back(rest);
                if (physique) {
                    for(unsigned t=0;t<primitive.targets_count;++t){
                        PhysiqueDelta entry{};
                        entry.target=static_cast<std::uint16_t>(t);
                        bool nonzero=false;
                        for(unsigned c=0;c<9;++c){
                            const float value=morphDeltas[v][t][c];
                            require(std::abs(value)<=65504.f,"physique delta overflow");
                            entry.half[c]=DirectX::PackedVector::XMConvertFloatToHalf(value);
                            nonzero|=(entry.half[c]&0x7fffu)!=0;
                        }
                        if(nonzero)output->physique.deltas.push_back(entry);
                    }
                    output->physique.offsets.push_back(static_cast<unsigned>(output->physique.deltas.size()));
                    output->vertex_morphs.push_back(UINT32_MAX);
                } else if (primitive.targets_count) {
                    output->vertex_morphs.push_back(static_cast<unsigned>(runtime.morphs.size()));
                    runtime.morphs.push_back({indices.at(&node), std::move(morphDeltas[v])});
                } else output->vertex_morphs.push_back(UINT32_MAX);
                std::array<float, 3> deformed{};
                for (unsigned k = 0; k < 4; ++k)
                    if (vertex.weights[k])
                    {
                        auto matrix = skn::multiply(
                            output->idle[vertex.joints[k]],
                            output->geometry.joints[vertex.joints[k]].inverse);
                        auto point = skn::point(matrix, vertex.position);
                        for (unsigned c = 0; c < 3; ++c)
                            deformed[c] += point[c] * vertex.weights[k] / 65535.f;
                    }
                idlePositions.push_back(deformed);
                output->radius = (std::max)(output->radius, std::sqrt(deformed[0] * deformed[0] +
                                                                      deformed[1] * deformed[1] +
                                                                      deformed[2] * deformed[2]));
            }
            const unsigned first = static_cast<unsigned>(output->geometry.indices.size());
            const auto count = primitive.indices ? primitive.indices->count : positions->count;
            require(count % 3 == 0, "triangle index count");
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto index = primitive.indices
                                       ? cgltf_accessor_read_index(primitive.indices, i)
                                       : i;
                require(index < positions->count, "vertex index");
                output->geometry.indices.push_back(base + static_cast<unsigned>(index));
            }
            // Matching rest matrices alone cannot detect a mesh normalized in a
            // different frame from its skeleton. Reject catastrophic deformation
            // in the very pose we display, using the same quantized skin as draws.
            for (std::size_t i = 0; i < count; i += 3)
                for (unsigned edge = 0; edge < 3; ++edge)
                {
                    const auto a = output->geometry.indices[first + i + edge] - base;
                    const auto b = output->geometry.indices[first + i + (edge + 1) % 3] - base;
                    double restSquared = 0, idleSquared = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const double rest = double(pos[a * 3 + c]) - pos[b * 3 + c];
                        const double idle = double(idlePositions[a][c]) - idlePositions[b][c];
                        restSquared += rest * rest;
                        idleSquared += idle * idle;
                    }
                    require(restSquared <= 1e-12 || idleSquared <=
                                restSquared * maxIdleEdgeStretch * maxIdleEdgeStretch,
                            "idle skin edge stretch exceeds profile limit; rebind mesh and skeleton together: " +
                                std::string(node.name ? node.name : "mesh") +
                                " triangle " + std::to_string(i / 3));
                }
            output->geometry.submeshes.push_back(
                { first, static_cast<unsigned>(count),
                  primitive.material ? static_cast<unsigned>(primitive.material - data.materials)
                                     : static_cast<unsigned>(data.materials_count),
                  0, 0 });
            output->geometry.semantic_joints.push_back(0);
        }
    }
    require(!output->geometry.vertices.empty() && !output->geometry.indices.empty(), "empty body");
    skn::Lod lod{};
    lod.vertex_count = static_cast<unsigned>(output->geometry.vertices.size());
    lod.index_count = static_cast<unsigned>(output->geometry.indices.size());
    lod.submesh_count = static_cast<unsigned>(output->geometry.submeshes.size());
    output->geometry.lods.push_back(lod);
    if (rigid || wearable) {
        if(articulated) {
            const auto limits=envelope(runtime);
            const auto restRadius=output->radius;
            for(unsigned n=0;n<runtime.nodes.size();++n) {
                const auto chain=socket_chain(runtime,n);
                output->mechanism.push_back(chain.back());
                const double bound=limits[n].translation+limits[n].scale*restRadius;
                require(std::isfinite(bound) && bound<FLT_MAX/4,"mechanism bounds");
                output->radius=(std::max)(output->radius,float(bound));
            }
            output->animation_radius=output->radius;
            output->mechanism_animation=std::move(output->animation);
            return output;
        }
        output->animation.reset();
        output->animation_radius=output->radius;
        return output;
    }
    const auto selector = [&](const Json &value, const std::map<std::string,unsigned> &names) {
        if (value.is_number_unsigned() || value.is_number_integer()) return value.get<unsigned>();
        const auto found=names.find(value.get<std::string>());
        require(found!=names.end(), "binding selector"); return found->second;
    };
    const std::map<std::string,unsigned> states{{"neutral",0},{"rest",0x8000},{"attack",0x10000},{"warry",0x20000},{"lie",0x30000}};
    const std::map<std::string,unsigned> actions{{"special",0x40000},{"attack",0x80000},{"attack_unarmed",0x80000},{"cast",0xc0000},{"run",0x100000},
        {"walk",0x140000},{"idle",0x180000},{"death",0x1c0000},{"suffer",0x200000},{"cross",0x240000}};
    const std::map<std::string,unsigned> stages{{"unique",0},{"start",0x40000000},{"cycle",0x80000000},{"end",0xc0000000}};
    if (metadata.contains("bindings")) for (const auto &binding:metadata.at("bindings")) {
        Binding b; b.clip=named_clip(runtime,binding.at("clip").get<std::string>());
        require(b.clip!=UINT32_MAX,"binding names missing clip");
        if (binding.contains("legacyClip")) {
            b.legacy=binding.at("legacyClip").get<std::string>();
            require(!b.legacy.empty(),"empty legacy binding");
        } else {
            const auto state=selector(binding.at("state"),states), action=selector(binding.at("action"),actions),
                stage=selector(binding.at("stage"),stages), weapon=binding.at("weaponMask").get<unsigned>(),
                modifiers=binding.at("modifierBits").get<unsigned>();
            require(!(state&~0x38000u)&&!(action&~0x3c0000u)&&!(stage&~0xc0000000u)&&!(weapon&~0x7fffu)&&!(modifiers&~0x3fc00000u),"binding field mask");
            b.flags=state|action|stage|weapon|modifiers;
        }
        for(const auto &prior:runtime.bindings) {
            require(b.legacy.empty()||prior.legacy!=b.legacy,"duplicate legacy binding");
            if(b.legacy.empty()&&prior.legacy.empty()&&(b.flags&~0x7fffu)==(prior.flags&~0x7fffu))
                require((b.flags&prior.flags&0x7fffu)==0 && ((b.flags|prior.flags)&0x7fffu)!=0,"overlapping tuple bindings");
        }
        runtime.bindings.push_back(std::move(b));
    }
    if(physique)for(auto& n:runtime.nodes)n.weights.clear();
    output->gpu_animation=pack(runtime,{});
    for(const auto& socket:contract.at("sockets")) {
        const auto name=socket.at("name").get<std::string>();
        const auto found=std::find_if(output->geometry.joints.begin(),output->geometry.joints.end(),
            [&](const skn::Joint& j){return j.name==name;});
        output->sockets.emplace(name,socket_chain(runtime,unsigned(found-output->geometry.joints.begin())));
    }
    if(physique){
        output->physique_tree.initialize(output->rest_vertices);
        for(auto node:output->sole_nodes)output->attachment_chain(output->geometry.joints[node].name);
    }
    output->node_envelopes=envelope(runtime);
    const auto& limits=output->node_envelopes;
    double radius=output->radius;
    for(std::size_t i=0;i<output->rest_vertices.size();++i){const auto& v=output->rest_vertices[i];
        double p[3]{std::abs(v.position[0]),std::abs(v.position[1]),std::abs(v.position[2])};
        if(output->vertex_morphs[i]!=UINT32_MAX){const auto& m=runtime.morphs[output->vertex_morphs[i]];
            for(unsigned t=0;t<m.deltas.size();++t)for(unsigned c=0;c<3;++c)p[c]+=std::abs(m.deltas[t][c])*limits[m.node].weights[t];}
        if(physique)for(unsigned k=output->physique.offsets[i];k<output->physique.offsets[i+1];++k)
            for(unsigned c=0;c<3;++c)p[c]+=std::abs(skn::half_float(output->physique.deltas[k].half[c]));
        const double meshRadius=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);double bound=0;
        for(unsigned k=0;k<4;++k)if(v.weights[k]){const auto j=v.joints[k];const auto& inverse=output->geometry.joints[j].inverse;
            double linear=0,offset=0;for(unsigned r=0;r<3;++r){offset+=double(inverse[12+r])*inverse[12+r];
                for(unsigned c=0;c<3;++c)linear+=double(inverse[c*4+r])*inverse[c*4+r];}
            bound+=(limits[j].translation+limits[j].scale*(std::sqrt(linear)*meshRadius+std::sqrt(offset)))*v.weights[k]/65535.0;}
        radius=(std::max)(radius,bound);
    }
    require(std::isfinite(radius)&&radius<std::numeric_limits<float>::max()/4,"animation bounds overflow");
    output->animation_radius=static_cast<float>(radius*(1+1e-5));
    return output;
}
} // namespace
