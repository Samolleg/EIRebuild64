#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ItemDatabase.h"
#include "ItemApproachCatalog.h"

#include "CMappedResource.h"
#include <climits>
#include <cstring>
#include <stdexcept>

namespace
{
using MaterialGraphics = std::map<std::string, int>;
using PrototypeGraphics = std::map<std::string, MaterialGraphics>;

void RequireMaterialGraphics(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}

std::string LowerMaterialCode(std::string code)
{
    for (char &c : code)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return code;
}

MaterialGraphics ParseMaterialGraphics(const std::string &text)
{
    MaterialGraphics result;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const size_t equal = text.find('=', start);
        RequireMaterialGraphics(equal != std::string::npos && equal > start && equal + 1 < end,
            "Expected material_code=graphics_index in quick-item field 28");
        std::string code = text.substr(start, equal - start);
        for (const unsigned char c : code)
            RequireMaterialGraphics((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_', "Invalid material code in quick-item field 28");
        int index = 0;
        for (size_t pos = equal + 1; pos < end; ++pos)
        {
            const unsigned char c = text[pos];
            RequireMaterialGraphics(c >= '0' && c <= '9', "Graphics index must be an unsigned decimal integer");
            const int digit = c - '0';
            RequireMaterialGraphics(index <= (INT_MAX - digit) / 10, "Graphics index exceeds INT_MAX");
            index = index * 10 + digit;
        }
        RequireMaterialGraphics(result.emplace(LowerMaterialCode(code), index).second,
            "Duplicate material code in quick-item field 28");
        start = end + 1;
    }
    return result;
}

struct ItemChunk
{
    unsigned tag;
    const BYTE *data;
    size_t size;
};

std::vector<ItemChunk> ItemChildren(const BYTE *data, size_t size)
{
    std::vector<ItemChunk> result;
    size_t position = 0;
    while (position < size)
    {
        RequireMaterialGraphics(size - position >= 2, "Truncated IDB chunk header");
        const unsigned tag = data[position];
        DWORD encoded = data[position + 1];
        size_t header = 2;
        if (encoded & 1)
        {
            RequireMaterialGraphics(size - position >= 5, "Truncated extended IDB chunk header");
            std::memcpy(&encoded, data + position + 1, sizeof(encoded));
            header = 5;
        }
        const size_t bytes = encoded >> 1;
        RequireMaterialGraphics(bytes <= size - position - header, "IDB chunk exceeds its parent");
        RequireMaterialGraphics(result.size() < 1000000, "IDB chunk count exceeds supported bounds");
        result.push_back({tag, data + position + header, bytes});
        position += header + bytes;
    }
    return result;
}

ItemChunk UniqueItemChild(const ItemChunk &parent, unsigned tag)
{
    ItemChunk selected = {};
    bool found = false;
    for (const auto &child : ItemChildren(parent.data, parent.size))
        if (child.tag == tag)
        {
            RequireMaterialGraphics(!found, "Duplicate IDB table");
            selected = child;
            found = true;
        }
    RequireMaterialGraphics(found, "Missing IDB table");
    return selected;
}

std::string ItemText(const ItemChunk &field)
{
    size_t bytes = field.size;
    while (bytes && field.data[bytes - 1] == 0) --bytes;
    RequireMaterialGraphics(std::memchr(field.data, 0, bytes) == nullptr, "Embedded NUL in IDB text");
    return std::string(reinterpret_cast<const char *>(field.data), bytes);
}

PrototypeGraphics ReadMaterialGraphics(const BYTE *data, size_t size)
{
    const auto root = UniqueItemChild({0, data, size}, 1);
    const auto quick = UniqueItemChild(root, 4);
    PrototypeGraphics result;
    for (const auto &record : ItemChildren(quick.data, quick.size))
    {
        if (record.tag != 1) continue;
        std::string name, value;
        bool hasName = false, hasMap = false;
        for (const auto &field : ItemChildren(record.data, record.size))
        {
            if (field.tag == 0)
            {
                RequireMaterialGraphics(!hasName, "Duplicate quick-item name");
                hasName = true;
                name = ItemText(field);
            }
            else if (field.tag == 28)
            {
                RequireMaterialGraphics(!hasMap, "Duplicate quick-item material map");
                hasMap = true;
                value = ItemText(field);
            }
        }
        if (!hasMap || value.empty()) continue;
        RequireMaterialGraphics(hasName && !name.empty(), "Material map has no quick-item prototype name");
        try
        {
            RequireMaterialGraphics(result.emplace(name, ParseMaterialGraphics(value)).second,
                "Duplicate mapped quick-item prototype");
        }
        catch (const std::exception &error)
        {
            throw std::runtime_error(name + ": " + error.what());
        }
    }
    return result;
}
}
/////////////////////////////////////////////////////////////////////////////////////
SItemDatabase theItemDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SItemDatabase::Load(LPCSTR pszPath, LPCSTR pszFName)
{
    ItemApproachCatalog::Reset();
    quickMaterialGraphics.clear();
    sourceArchive.clear();
    CString szPathName = CString(pszPath) + pszFName;
    CMappedResource res;
    if (res.OpenResource(szPathName) == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName);
    //
    DWORD num;
    LPBYTE pData = res.GetFile("items.idb", &num);
    if (!pData)
        return ReportError("Can't open items database\n");
    //
    try
    {
        sourceArchive = res.FileProvider("items.idb");
        // Read the same effective payload as the normal database loader. Validate
        // duplicates and byte strings before CStructureSaver selects individual fields.
        RequireMaterialGraphics(num > 0 && num <= 64u * 1024u * 1024u, "Invalid effective items.idb size");
        auto maps = ReadMaterialGraphics(pData, num);
        CMemorySave file;

        file.StartSaving();
        file.Write(pData, num);
        file.Seek(0);
        file.StartLoading();

        materials.Load(file, 1);
        weapons.Load(file, 2);
        armors.Load(file, 3);
        quickItems.Load(file, 4);
        questItems.Load(file, 5);
        lootItems.Load(file, 6);

        std::vector<MaterialGraphics> selected(quickItems.GetNumEntries());
        for (int row = 0; row < quickItems.GetNumEntries(); ++row)
        {
            const auto *shape = quickItems[row];
            const auto it = maps.find(shape->szName.GetString());
            if (it == maps.end()) continue;
            for (const auto &choice : it->second)
            {
                bool known = false;
                for (int material = 0; material < materials.GetNumEntries(); ++material)
                    if (LowerMaterialCode(materials[material]->szCodeName.GetString()) == choice.first)
                    {
                        known = true;
                        break;
                    }
                if (!known)
                    throw std::runtime_error(std::string(shape->szName.GetString()) +
                        ": unknown material code " + choice.first + " in field 28");
            }
            selected[row] = std::move(it->second);
            maps.erase(it);
        }
        RequireMaterialGraphics(maps.empty(), "Material map does not match the loaded quick-item database");
        quickMaterialGraphics.swap(selected);
    }
    catch (const std::exception &error)
    {
        quickMaterialGraphics.clear();
        res.CloseFile(pData);
        res.CloseResource();
        // Malformed database input must also stop startup with AntiCrash enabled;
        // GameMain does not check the return value of this loader.
        return ReportError(E_INVALIDARG, "Cannot load inventory material graphics (items.idb): %.800s", error.what());
    }
    //
    res.CloseFile(pData);
    res.CloseResource();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////

int SItemDatabase::GetQuickGraphicsIndex(DWORD shape, DWORD material) const
{
    const auto *entry = quickItems[shape];
    const int graphics = entry ? entry->dwTextureTypeIndex : -1;
    if (shape >= quickMaterialGraphics.size() || quickMaterialGraphics[shape].empty() ||
        material >= static_cast<DWORD>(materials.GetNumEntries()))
        return graphics;
    const auto &choices = quickMaterialGraphics[shape];
    const auto it = choices.find(LowerMaterialCode(materials[material]->szCodeName.GetString()));
    // An absent assignment keeps the original field-5 selection, as in the addon.
    return it == choices.end() ? graphics : it->second;
}
