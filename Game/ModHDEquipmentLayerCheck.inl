Json CheckHDEquipmentLayer(const std::string& archivePath)
{
    CMappedResource archive(true);
    if (!archive.OpenResource(archivePath.c_str()))
        throw std::runtime_error("Cannot open read-only HD equipment archive");
    DWORD bytes = 0;
    auto* raw = archive.GetFile("unhumahl_25.at.0.x2.mmp", &bytes);
    if (!raw)
    {
        archive.CloseResource();
        throw std::runtime_error("HD equipment MMP absent from archive");
    }
    struct CloseMember
    {
        CMappedResource& archive;
        BYTE* bytes;
        ~CloseMember() { archive.CloseFile(bytes); }
    } close{archive, raw};
    auto* header = reinterpret_cast<SMMPFileHeader*>(raw);
    if (bytes < sizeof(*header) || !ModHDEquipmentLayer::Supported(
            header->dwDimentionX, header->dwDimentionY, header->dwFormat))
        throw std::runtime_error("HD equipment MMP does not match the supported 512x512 layer contract");
    if (header->dwFormat != MMP_FORMAT_8888 || header->dwUserDataShift > bytes - sizeof(*header) ||
        uint64_t(header->dwDimentionX) * header->dwDimentionY * 4 >
            bytes - sizeof(*header) - header->dwUserDataShift)
        throw std::runtime_error("Expected complete raw 8888 pixel payload");

    std::vector<DWORD> composed(size_t(header->dwDimentionX) * header->dwDimentionY, 0xFF204060);
    CGraphicsProcessorMMX processor;
    processor.PutOneLayer(composed.data(),
        reinterpret_cast<DWORD*>(header->GetMipLevelData(0)), int(composed.size()),
        ModHDEquipmentLayer::UsesRunLength(header->dwFormat));
    const auto changed = std::count_if(composed.begin(), composed.end(),
        [](DWORD pixel) { return pixel != 0xFF204060; });
    if (!changed)
        throw std::runtime_error("The real 8888 layer did not reach the pixel compositor");
    return {{"success", true}, {"member", "unhumahl_25.at.0.x2.mmp"},
        {"dimensions", {header->dwDimentionX, header->dwDimentionY}},
        {"format", "MMP_FORMAT_8888"}, {"compositor", "CGraphicsProcessorMMX::PutOneLayer"},
        {"rle", ModHDEquipmentLayer::UsesRunLength(header->dwFormat)},
        {"changed_pixels", changed}, {"game_or_world_created", false}};
}
