#pragma once
/////////////////////////////////////////////////////////////////////////////////////
// enum ETypeIDs
// type IDs
enum ETypeIDs
{
    TID_EMPTY = 0, // empty type id
    // texture type IDs
    TTID_UNIT_MAIN,    // UM  main unit's texture
    TTID_UNIT_WEAPON,  // UW  unit's weapon texture
    TTID_TREE_MAIN,    // TM  tree main texture
    TTID_HOUSE_MAIN,   // HM  house main texture
    TTID_HOUSE_SECOND, // HS  house additional texture
    TTID_SPELL_MAIN,   // SPM spell main texture
    TTID_STONE_MAIN,   // SM  stone main texture
    TTID_OBJECT_MAIN,  // OM  stuff object main texture
    TTID_WATER_MAIN,   // WM  water main texture
    TTID_WATER_SECOND, // WS  water second texture
    TTID_TERRAIN,      // TER terrain's texture
    TTID_SPELL_SECOND, // SPS spell secondary texture format
    TTID_ANIMATED1,    // AT1 animated texture type 1
    TTID_ANIMATED2,    // AT2 animated texture type 2
    TTID_ANIMATED3,    // AT3 animated texture type 3
    TTID_ANIMATED4,    // AT4 animated texture type 4
    // material type IDs
    MTID_LEATHER,   // LE leather
    MTID_WOOD,      // WO wood
    MTID_METAL,     // ME metal
    MTID_CLOTH,     // CL cloth
    MTID_SKIN,      // SK skin
    MTID_STONE,     // ST stone
    MTID_MAGIC,     // MG magic (for spell effects)
    MTID_WATER,     // WT water - blue with 0.5 alpha
    MTID_CRYSTAL,   // CR crystal - with alpha 0.5
    MTID_RESERVED2, // reserved
    MTID_RESERVED3, // reserved
    MTID_RESERVED4, // reserved
    MTID_RESERVED5, // reserved
    // texture format type IDs
    TFTID_565,        // RGBAlpha = 5650
    TFTID_555,        // RGBAlpha = 5550
    TFTID_5551,       // RGBAlpha = 5551
    TFTID_4444,       // RGBAplha = 4444
    TFTID_8888,       // RGBAlpha = 8888
    TFTID_NOALPHA,    // no alpha in texture
    TFTID_ALPHA1,     // alpha = 1bit
    TFTID_ALPHA4,     // alpha = 4bit
    TFTID_ALPHA8,     // alpha = 8bit
    TFTID_PALETTIZED, // palettized texture format
    TFTID_DXT1,       // DirectX Texture 1
    TFTID_DXT2,       // DirectX Texture 2
    TFTID_DXT3,       // DirectX Texture 3
    TFTID_DXT4,       // DirectX Texture 4
    TFTID_DXT5,       // DirectX Texture 5
    TFTID_UNKNOWN,    // unknown format
    // light type IDs
    LTID_DIRECTIONAL,
    LTID_PARALLELPOINTLIGHT,
    LTID_SPOTLIGHT,
    LTID_POINTLIGHT,
    // map object type IDs
    MOTID_HUMAN,    // (CUnit) human/orc
    MOTID_MONSTER,  // (CUnit) monsters (semi-intelligent)
    MOTID_ANIMAL,   // (CUnit) animals (non-intelligent)
    MOTID_TREE,     // (CTree) trees
    MOTID_BUILDING, // (CBuilding) buildings
    MOTID_STONE,    //
    MOTID_OBJECT,
    MOTID_UNIT,
    MOTID_TORCH,
    MOTID_TRAP,
    MOTID_LEVER,
    // texture general class
    TCTID_STATIC_TEXTURE,
    TCTID_PAINT_TEXTURE,
    TCTID_DYNAMIC_TEXTURE,
    TCTID_SHADOW_TEXTURE,
    // figure type IDs
    FTID_BODY,
    FTID_HEAPBODY,
    FTID_INTERFACE,
    FTID_SKY,
    // specific part IDs
    SPTID_FIGURE_SPECIFIC,
    SPTID_OBJECT3D_SPECIFIC,
    SPTID_MAPOBJECT_SPECIFIC,
    SPTID_UNIT_SPECIFIC,
    SPTID_PLAYER_SPECIFIC,
    SPTID_ITEM_SPECIFIC,
    SPTID_TORCH_SPECIFIC,
    // particle type IDs
    PTID_FIREBALL = 0x2000,
    PTID_CAMPFIRE,
    PTID_FIREBLAST,
    PTID_FIRE,
    PTID_SMOKE,
    PTID_VULCANSMOKE,
    PTID_HEALING,
    PTID_POISONFOG,
    PTID_AGGRESSIONFOG,
    PTID_GEYSER,
    PTID_TORNADO,
    PTID_CASTING,
    PTID_NUKE,
    PTID_BANSHEECASTING,
    PTID_MUSHROOM,
    PTID_BLOOD1, // do not change order! :) or every mob and save will be outdated (effects shift)
    PTID_FIREWALL,
    PTID_FIREARROW,
    PTID_ACIDRAY,
    PTID_BLUEGAS,
    PTID_LINK,
    PTID_SPHEREACID,
    PTID_SPHEREELECTRICITY,
    PTID_SPHEREFIRE,
    PTID_CLAYRING,
    PTID_TELEPORT,
    PTID_ANTIMAGIC,
    PTID_MODIFIER1,
    PTID_MODIFIER2,
    PTID_MODIFIER3,
    PTID_MODIFIER4,
    PTID_MODIFIER5,
    PTID_MODIFIER6,
    PTID_MODIFIER7,
    PTID_MODIFIER8,
    PTID_MODIFIER9,
    PTID_MODIFIER10,
    PTID_MODIFIER11,
    PTID_MODIFIER12,
    PTID_CASTINGFIRE,
    PTID_CASTINGELECTRICITY,
    PTID_CASTINGACID,
    PTID_CASTINGDIVINATION,
    PTID_CASTINGILLUSION,
    PTID_CASTINGDOMINATION,
    PTID_CASTINGENCHANTMENT,
    PTID_CASTINGHEALING,
    PTID_CASTINGFAILED,
    PTID_LIGHTNINGBLAST,
    PTID_BLOOD2,
    PTID_BLOOD3,
    PTID_BLOOD4,
    PTID_A4BLOODRED,
    PTID_A4BLOODGREEN,
    PTID_A4BLOODBLUE,
    PTID_A4BLOODBLACK,
    PTID_ZONEEXIT,
    PTID_PATH,
    PTID_PATHDESTINATION,
    PTID_PATHFAILED,
    PTID_MOSHKA,
    PTID_PORTALSTAR,
    PTID_PORTAL,
    PTID_CYLINDER1,
    PTID_CYLINDER2,
    PTID_FIRESTAR,
    PTID_ACIDSTAR,
    PTID_SPARKS,
    PTID_VISIONSTAR1,
    PTID_VISIONSTAR2,
    PTID_VISIONSTAR3,
    PTID_REGENERATION,
    PTID_SILENCE,
    PTID_FEEBLEMIND,
    PTID_FEETCLOUD1,
    PTID_FEETCLOUD2,
    PTID_VISIONSTAR4,
    PTID_BALLOFSTARS,
    PTID_RICKARROW,
    PTID_CURSESTARS,
    PTID_CURSEHOLDER,
    PTID_STARTTRANS,
    PTID_TRANSFORM,
    // spell system object types
    SSOTID_SPELL_EFFECT = 0x3000,
    SSOTID_SPELL_EFFECT_CONTAINER,
    SSOTID_SPELL,
    SSOTID_SPELL_CONTAINER,
    // item type IDs
    ITID_ITEM_WEAPON,
    ITID_ITEM_ARMOR,
    ITID_ITEM_QUICK,
    ITID_ITEM_LOOT,
    ITID_ITEM_SPELL,
    ITID_ITEM_QUEST,
    // server IDs
    STID_PLAYER,
    // animation system IDs
    ASTID_ANIMATION_TRACK,
    ASTID_ANIMATION_NODE,
    ASTID_ANIMATION_SYSTEM2,
    ASTID_ANIMATION_NODE3,
    ASTID_ANIMATION_SYSTEM3,
    //
    MOTID_KAPKAN,
    //
    TYPEID_FORCE_DWORD = 0x7fffffff
};
/////////////////////////////////////////////////////////////////////////////////////
