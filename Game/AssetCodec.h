#pragma once

// Fixed-width asset fields inside the existing CStructureSaver resource container.
// This is independent of SaveSchema and never serializes a native record layout.
#include "BasicChunk.h"
#include "idbFormat.h"
#include "sdbFormat.h"
#include "pdbFormat.h"
#include "udbFormat.h"
#include "qdbFormat.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace AssetCodec
{
class Archive;
inline void Fields(Archive &file, SIDBMaterial &value);
inline void Fields(Archive &file, SIDBItemGeneralShape &value);
inline void Fields(Archive &file, SIDBWeaponShape &value);
inline void Fields(Archive &file, SIDBArmorShape &value);
inline void Fields(Archive &file, SIDBQuickItemShape &value);
inline void Fields(Archive &file, SIDBQuestItemShape &value);
inline void Fields(Archive &file, SIDBLootItemShape &value);
inline void Fields(Archive &file, SSDBModifier &value);
inline void Fields(Archive &file, SSDBSpellPrototype &value);
inline void Fields(Archive &file, SSDBComplexSpell &value);
inline void Fields(Archive &file, SPDBGeneralAbility &value);
inline void Fields(Archive &file, SPDBSkill &value);
inline void Fields(Archive &file, SPDBPerk &value);
inline void Fields(Archive &file, SUDBUnitLoot &value);
inline void Fields(Archive &file, SUDBUnitHitLocation &value);
inline void Fields(Archive &file, SUDBUnitPrototype &value);
inline void Fields(Archive &file, SUDBNPCPrototype &value);
inline void Fields(Archive &file, SUDBRaceModel &value);
inline void Fields(Archive &file, SUDBHitLocationType &value);
inline void Fields(Archive &file, SQDBQuest &value);
inline void Fields(Archive &file, SQDBBriefing &value);

// These counts are part of the installed x86 asset format, not runtime array sizes.
static_assert(RPG_NUM_ATTACK_TYPES == 7 && RPG_NUM_SENSES == 6 &&
              RPG_NUM_MANUAL_SKILLS == 3 && RPG_NUM_AUTO_SKILLS == 8 &&
              RPG_NUM_HIT_LOCATIONS == 6 && UDB_NUM_MAGIC_SCHOOLS == 3 &&
              SDB_NUM_ALLOWANCES == 5, "Asset field counts require an explicit format revision");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Assets require IEEE-754 binary32");

class Archive
{
    CStructureSaver &saver;

    template <class T>
    static constexpr int Width()
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, float>,
                      "Only numeric fields can be packed");
        return (std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>) ? 1 : 4;
    }
    static void PutWord(unsigned char *out, std::uint32_t bits)
    {
        for (int i = 0; i < 4; ++i) out[i] = static_cast<unsigned char>(bits >> (8 * i));
    }
    static std::uint32_t GetWord(const unsigned char *in)
    {
        std::uint32_t bits = 0;
        for (int i = 0; i < 4; ++i) bits |= std::uint32_t(in[i]) << (8 * i);
        return bits;
    }
    template <class T>
    static void Encode(unsigned char *out, T value)
    {
        if constexpr (Width<T>() == 1)
            out[0] = static_cast<unsigned char>(value);
        else
        {
            std::uint32_t bits;
            if constexpr (std::is_same_v<T, float>)
                std::memcpy(&bits, &value, 4); // binary32 bits only, never a C++ record
            else
            {
                static_assert(sizeof(T) <= 4, "No native pointer/size_t/64-bit field in assets");
                bits = static_cast<std::uint32_t>(value);
            }
            PutWord(out, bits);
        }
    }
    template <class T>
    static void Decode(const unsigned char *in, T &value)
    {
        if constexpr (std::is_same_v<T, bool>)
            value = in[0] != 0;
        else if constexpr (Width<T>() == 1)
            value = static_cast<T>(in[0]);
        else
        {
            const std::uint32_t bits = GetWord(in);
            if constexpr (std::is_same_v<T, float>)
                std::memcpy(&value, &bits, 4);
            else if constexpr (std::is_signed_v<T> || std::is_enum_v<T>)
            {
                // Signed x86 fields/enums are two's complement int32.
                const std::int64_t number = bits <= 0x7fffffffU ? bits :
                    static_cast<std::int64_t>(bits) - 0x100000000LL;
                value = static_cast<T>(number);
            }
            else
                value = static_cast<T>(bits);
        }
    }
    template <class T>
    struct Record
    {
        T &value;
        void Serialize(CStructureSaver &saver)
        {
            Archive file(saver);
            Fields(file, value);
        }
    };
    template <class T>
    struct Element
    {
        void operator()(CStructureSaver *saver, chunk_id id, T &value)
        {
            Archive file(*saver);
            file.Add(id, value);
        }
    };

public:
    explicit Archive(CStructureSaver &source) : saver(source) {}

    template <class T>
    void Add(chunk_id id, T &value)
    {
        if constexpr (std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, float>)
        {
            unsigned char bytes[Width<T>()] = {};
            if (!saver.IsReading()) Encode(bytes, value);
            saver.AddRaw(id, bytes, Width<T>());
            if (saver.IsReading()) Decode(bytes, value);
        }
        else if constexpr (std::is_same_v<T, CString> || std::is_same_v<T, std::string>)
            saver.Add(id, value); // existing NUL-terminated byte string; no transcoding
        else
        {
            Record<T> record{value};
            saver.Add(id, record);
        }
    }
    template <class T>
    void AddT(chunk_id id, std::vector<T> &values)
    {
        // Preserve the resource vector: repeated child chunk 1, no size_t/count field.
        saver.AddVector(id, values, Element<T>());
    }
    template <class T, std::size_t N>
    void Packed(chunk_id id, T (&values)[N])
    {
        // Fixed flat payload: field i starts at i * wire width.
        unsigned char bytes[N * Width<T>()] = {};
        if (!saver.IsReading())
            for (std::size_t i = 0; i < N; ++i) Encode(bytes + i * Width<T>(), values[i]);
        saver.AddRaw(id, bytes, static_cast<int>(N * Width<T>()));
        if (saver.IsReading())
            for (std::size_t i = 0; i < N; ++i) Decode(bytes + i * Width<T>(), values[i]);
    }
    void Armor(chunk_id id, SIDBArmorAbsorbtion &value)
    {
        // x86 layout: absorbtion@0, seven distributions@4..28, cripple mask@32.
        unsigned char bytes[36] = {};
        if (!saver.IsReading())
        {
            Encode(bytes, value.fpAbsorbtion);
            for (int i = 0; i < 7; ++i) Encode(bytes + 4 + i * 4, value.fpDistributions[i]);
            Encode(bytes + 32, value.dwCrippleMask);
        }
        saver.AddRaw(id, bytes, 36);
        if (saver.IsReading())
        {
            Decode(bytes, value.fpAbsorbtion);
            for (int i = 0; i < 7; ++i) Decode(bytes + 4 + i * 4, value.fpDistributions[i]);
            Decode(bytes + 32, value.dwCrippleMask);
        }
    }
};
inline void Fields(Archive &file, SIDBMaterial &value)
{
    // names
    file.Add(0, value.szName);
    file.Add(1, value.szType);
    file.Add(2, value.szCodeName);
    file.Add(3, value.dwTypeIndex);
    // general modifiers
    file.Add(4, value.fpPrice);
    file.Add(5, value.fpWeight);
    file.Add(6, value.fpManaCapacity);
    file.Add(7, value.dwSpellSlots);
    file.Add(8, value.fpDurability);
    // skill modifier
    file.Add(9, value.dwSkill);
    // damage  modifier
    file.Add(10, value.fpDamageModifier);
    // absorbtion/cripple mask
    file.Packed(11, value.fpAbsorbtionMultipliers);
    file.Add(12, value.dwCrippleMask);
    //
    file.Add(13, value.dwAvailableInShop);
}

inline void Fields(Archive &file, SIDBItemGeneralShape &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.szType);
    // armor shape type for armor: helm, plate, leggings; shirt, boots, gloves
    file.Add(2, value.dwTypeID);
    file.Add(3, value.szMaterialType);
    file.Add(4, value.dwMaterialTypeIndex);
    // graphics data
    file.Add(5, value.dwTextureTypeIndex);
    file.Add(6, value.dwTextureTypeIndex2);
    // general modifiers
    file.Add(7, value.fpPrice);
    file.Add(8, value.fpWeight);
    file.Add(9, value.fpSize);
    file.Add(10, value.fpManaCapacity);
    file.Add(11, value.dwSpellSlots);
    file.Add(12, value.fpDurability);
    file.Add(13, value.dwComponents);
    //
    file.Add(14, value.dwAvailableInShop);
    //
    file.Add(15, value.bDeconstructable);
}

inline void Fields(Archive &file, SIDBWeaponShape &value)
{
    Fields(file, static_cast<SIDBItemGeneralShape &>(value));

    file.Add(21, value.dwActionPoints);
    file.Packed(22, value.dwStatModifiers);
    file.Add(23, value.fpRange);
    file.Add(24, value.fpDamageMin);
    file.Add(25, value.fpDamageRandom);
    file.Packed(26, value.fpDamageDistributions);
    file.Packed(27, value.cManualSkillModifier);
    file.Packed(28, value.cAutoSkillModifier);
    //
    file.Add(29, value.fAttackBonus);
    file.Add(30, value.fDefenceBonus);
}

inline void Fields(Archive &file, SIDBArmorShape &value)
{
    Fields(file, static_cast<SIDBItemGeneralShape &>(value));

    file.Armor(21, value.main);
    file.Armor(22, value.second);

    file.Add(23, value.bApplyWounds);

    file.Add(24, value.dwWearOrder);
    file.Packed(25, value.cManualSkillModifier);
    file.Packed(26, value.cAutoSkillModifier);
}

inline void Fields(Archive &file, SIDBQuickItemShape &value)
{
    Fields(file, static_cast<SIDBItemGeneralShape &>(value));

    file.Add(21, value.idbType);
    file.Add(22, value.dwSkill);
    file.Add(23, value.fDamage);
    file.Add(24, value.rpgDamageType);
    file.Add(25, value.szSpell);

    file.Packed(26, value.cManualSkillModifier);
    file.Packed(27, value.cAutoSkillModifier);
}

inline void Fields(Archive &file, SIDBQuestItemShape &value)
{
    Fields(file, static_cast<SIDBItemGeneralShape &>(value));
    file.Add(21, value.dwScriptID);
    file.AddT(22, value.szZones);
}

inline void Fields(Archive &file, SIDBLootItemShape &value)
{
    Fields(file, static_cast<SIDBItemGeneralShape &>(value));

    file.Add(21, value.litType);
    file.Add(22, value.itInstructionType);
    file.Add(23, value.dwMaterialIndex);
}

inline void Fields(Archive &file, SSDBModifier &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.dwCode);
    file.Add(2, value.fpPrice);
    file.Add(3, value.smtType);
    file.Add(4, value.fpMana);
    file.Add(5, value.fpValue);
    file.Add(6, value.dwComplexity);
    file.Add(7, value.szShop);
    file.Add(8, value.dwAvailableInShop);
}

inline void Fields(Archive &file, SSDBSpellPrototype &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.szCodeName);
    file.Add(2, value.szSubtype);
    file.Add(3, value.fpPrice);
    file.Add(4, value.msSchool);
    file.Add(5, value.fpMana);
    file.Add(6, value.dwSlots);
    file.Add(7, value.fpSpeed);
    file.Add(8, value.fpRange);
    file.Add(9, value.fpArea);
    file.Add(10, value.fpEffect);
    file.Add(11, value.dwTargeting);
    file.Add(12, value.dwNumTargets);
    file.Add(13, value.dwDuration);
    file.Add(14, value.dwActions);
    file.Add(15, value.dwRequireTrace);
    file.AddT(16, value.basemods);
    file.AddT(17, value.mods);
    file.Add(18, value.dwTextureTypeIndex);
    file.Add(19, value.mstSubtype);

    file.Packed(20, value.bRuneAllowed);

    file.Add(21, value.dwComplexity);

    file.Add(22, value.dwAvailableInShop);
    //
    file.Add(23, value.fLightR);
    file.Add(24, value.fLightG);
    file.Add(25, value.fLightB);
    file.Add(26, value.fLightRadius);
    file.Add(27, value.fFadeoutTime);
}

inline void Fields(Archive &file, SSDBComplexSpell &value)
{
    file.Add(1, value.szPrototype);
    file.AddT(2, value.required);
    file.AddT(3, value.optional);
    file.Add(4, value.szPower);
    file.Add(5, value.dwAvailableInShop);
}

inline void Fields(Archive &file, SPDBGeneralAbility &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.szCode);
    file.Add(2, value.dwTextureTypeIndex);
}

inline void Fields(Archive &file, SPDBSkill &value)
{
    Fields(file, static_cast<SPDBGeneralAbility &>(value));

    file.AddT(10, value.szBaseAttributes);
}

inline void Fields(Archive &file, SPDBPerk &value)
{
    Fields(file, static_cast<SPDBGeneralAbility &>(value));

    file.Add(10, value.szPerkRequired);
    file.Add(11, value.szSkillRequired);
    file.Add(12, value.rpgAutoSkill);
    file.Add(13, value.rpgManualSkill);
    file.Add(14, value.dwSkillLevel);
    file.Add(15, value.fStrength);
    file.Add(16, value.fDexterity);
    file.Add(17, value.fIntelligence);
    file.Add(18, value.dwCost);
    file.Add(19, value.dwModifier);
    file.Add(20, value.dwMult);
    file.Add(21, value.dwAdd);
    file.Add(22, value.bActive);
    file.Add(23, value.nPerkExclusive);
}

inline void Fields(Archive &file, SUDBUnitLoot &value)
{
    file.Add(0, value.fpProbability);
    file.Add(1, value.dwMask);
    file.Add(2, value.fpPriceMin);
    file.Add(3, value.fpPriceMax);
}

inline void Fields(Archive &file, SUDBUnitHitLocation &value)
{
    file.Add(0, value.szHLType);
    file.Add(1, value.dwHLTypeIndex);
    file.Add(2, value.fpWeight);
    file.Add(3, value.fpVitality);
}

inline void Fields(Archive &file, SUDBUnitPrototype &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.szRace);
    file.Add(2, value.dwRaceIndex);
    // graphics data
    file.Add(3, value.dwSkinIndex);
    file.Add(4, value.dwHair);
    file.Add(5, value.fStrength);
    file.Add(6, value.fGrace);
    file.Add(7, value.fTall);
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    file.Add(8, value.szSFXPath);
    //
    file.Add(9, value.fpMaxHP);
    file.Add(10, value.fpMaxMP);
    //
    file.Add(11, value.fpAbsorbtionMultiplier);
    file.Add(12, value.fpActionPoints);
    file.Add(13, value.fpMovePoints);
    // attack
    file.Add(14, value.fpAttackRange);
    file.Add(15, value.fpAttackToHit);
    file.Add(16, value.fpAttackParry);
    file.Add(17, value.fpAttackWeight);
    file.Add(18, value.rpgWeaponType);
    // damage
    file.Add(19, value.fpDamageMin);
    file.Add(20, value.fpDamageRandom);
    // skills
    file.Add(21, value.fpDefaultSkill);
    file.Add(22, value.fpSkillSteal);
    file.Add(23, value.fpSkillTame);
    file.Add(24, value.fpPeripheral);
    // senses & percepts
    file.Packed(25, value.senses);
    file.Packed(26, value.percepts);
    // loot
    file.Add(27, value.ulCommon);
    file.Add(28, value.ulRare);
    // always drop
    file.AddT(29, value.szDrops);
    // skills in magic schools
    file.Packed(30, value.magics);
    // spells
    file.AddT(31, value.szSpells);
    // wears and weapons
    file.AddT(32, value.szWears);
    file.Add(33, value.szWeapon);
    //
    file.Add(34, value.fInfoWindowScale);
    file.Add(35, value.fAltitude);
    //
    file.Add(36, value.fpAttackToHitRandom);
    // graphics dialog-related data
    file.Add(37, value.fDialogCameraDistance);
    file.Add(38, value.fDialogCameraHeight);
    //
    file.Add(39, value.rpgAttackWeaponType);
    //
    file.Add(40, value.fDetonation);
    //
    file.Add(41, value.nLevel);
    //
    file.Add(42, value.szWeapon2);
    //
    file.Add(43, value.fExp);
}

inline void Fields(Archive &file, SUDBNPCPrototype &value)
{
    file.Add(0, value.szName);
    //
    file.Add(1, value.dwCharacterPoints);
    file.Add(2, value.fExpirience);
    // primary stats
    file.Add(3, value.fStrength);
    file.Add(4, value.fDexterity);
    file.Add(5, value.fIntelligence);
    // skills & perks
    file.Packed(6, value.cManualSkills);
    file.Packed(7, value.cAutoSkills);
    file.AddT(8, value.szPerks);
    //
    file.AddT(9, value.szWeapons);
    file.AddT(10, value.szQItems);
    file.AddT(11, value.szSpells);
    file.Add(12, value.fExpToDistribute);
    file.Add(13, value.dwMoney);
    file.Add(14, value.bCanUseAsVoicePrototype);
}

inline void Fields(Archive &file, SUDBRaceModel &value)
{
    file.Add(0, value.szRace);
    file.Add(1, value.tidTypeID);
    //
    file.Add(2, value.fpRegenerateHP);
    file.Add(3, value.fpRegenerateMP);
    //
    file.Add(4, value.dwLanguage);
    file.Add(5, value.dwLocomotion);
    file.Add(6, value.fpVisionArc);
    // speed in different movement type
    file.Packed(7, value.fpSpeeds);
    //
    file.Add(8, value.fpAttackDistance);
    file.Add(9, value.dwAIClassStay);
    file.Add(10, value.dwAIClassLie);
    //
    file.Packed(11, value.attacks);
    //
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        file.Add(12 + i, value.hls[i]);
    //
    file.Add(30, value.szMaskName);
    file.AddT(31, value.szPrimaryTextures);
    file.AddT(32, value.szSecondaryTextures);
    file.Add(33, value.fpCenterShift);
    // sounds data. NOTE: all fields with numbers will be filled during initializing process
    // SFXes
    file.Add(34, value.szSFXPath);
    // steps
    file.AddT(35, value.szSteps);
    //
    file.Packed(36, value.fAnimationCoeffs);
    //
    file.Add(37, value.dwIdleProbability);
    file.Add(38, value.dwAttackProbability);
    //
    file.Packed(39, value.defences);
    //
    file.Add(40, value.dwBloodType);
    file.Add(41, value.dwCastingType);
    file.Add(42, value.nFootprintType);
    file.Add(43, value.dwNumLegSegments);
    //
    file.Add(44, value.szNativeSkinType);
    file.Add(45, value.bFirstStepRight);
    file.Add(46, value.fHeadHeight);
    //
    file.Add(47, value.nMouthPointX);
    file.Add(48, value.nMouthPointY);
    file.Add(49, value.nMouthRectSizeX);
    file.Add(50, value.nMouthRectSizeY);
}

inline void Fields(Archive &file, SUDBHitLocationType &value)
{
    file.Add(0, value.szName);
    file.Packed(1, value.fpResistances);
    file.Packed(2, value.fpAbsorbtions);
    file.Add(3, value.dwCrippleMask);
    file.Add(4, value.hlt);
}

inline void Fields(Archive &file, SQDBQuest &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.fExp);
    file.Add(2, value.fExpTotal);
    file.Add(3, value.dwZone);
    file.Add(4, value.szDescription);
    file.Add(5, value.fMoney);
    file.Add(6, value.dwNumber);

    file.AddT(7, value.places);
}

inline void Fields(Archive &file, SQDBBriefing &value)
{
    file.Add(0, value.szName);
    file.Add(1, value.fExp);
    file.Add(2, value.fMoney);
    file.AddT(3, value.szItems);
    file.Add(4, value.szDescription);
    file.AddT(5, value.szRemoveItems);
    file.AddT(6, value.szGiveQuests);
    file.AddT(7, value.szCompleteQuests);
    file.AddT(8, value.szAvailableZones);
    file.AddT(9, value.szCompleteZones);
    file.Add(10, value.nSpecial);
}

// Only these database families are changed by P18. Unrelated databases retain their codecs.
template <class T>
inline void DatabaseRecords(CStructureSaver &saver, chunk_id id, std::vector<T> &values)
{
    if constexpr (std::is_same_v<T, SIDBMaterial> ||
                  std::is_same_v<T, SIDBWeaponShape> ||
                  std::is_same_v<T, SIDBArmorShape> ||
                  std::is_same_v<T, SIDBQuickItemShape> ||
                  std::is_same_v<T, SIDBQuestItemShape> ||
                  std::is_same_v<T, SIDBLootItemShape> ||
                  std::is_same_v<T, SSDBSpellPrototype> ||
                  std::is_same_v<T, SSDBModifier> ||
                  std::is_same_v<T, SUDBUnitPrototype> ||
                  std::is_same_v<T, SUDBNPCPrototype> ||
                  std::is_same_v<T, SUDBRaceModel> ||
                  std::is_same_v<T, SUDBHitLocationType> ||
                  std::is_same_v<T, SQDBQuest> ||
                  std::is_same_v<T, SQDBBriefing>)
    {
        Archive file(saver);
        file.AddT(id, values);
    }
    else
        saver.AddT(id, values);
}
} // namespace AssetCodec