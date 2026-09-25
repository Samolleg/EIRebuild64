#pragma once
// ******************************************************************************** //
// **                            Knowledge level                                 ** //
// ******************************************************************************** //
const BYTE KNOW_NONE = 0;         // know nothing
const BYTE KNOW_HPMP = 1;         // current/max hit & mana points
const BYTE KNOW_SECONDARY = 2;    // + all secondary
const BYTE KNOW_PRIMARY = 3;      // + primary
const BYTE KNOW_RESISTANCES = 4;  // + resistances
const BYTE KNOW_SENSES = 5;       // + senses and percepts
const BYTE KNOW_HL = 6;           // + hit locations (HP + resistances + absorbtions)
const BYTE KNOW_ARMORS = 7;       // + armor layers
const BYTE KNOW_SKILLS = 8;       // + skills
const BYTE KNOW_EXPIRIENCE = 9;   // + expirience
const BYTE KNOW_CHAR_POINTS = 10; // + available character points
const BYTE KNOW_ALL = 10;         // know all information
/////////////////////////////////////////////////////////////////////////////////////
const DWORD RPGCS_PRESET_VALUES = 0x00000001;
const DWORD RPGUS_PRESET_VALUES = 0x00000001;
/////////////////////////////////////////////////////////////////////////////////////
const float FP_RPG_MAX_SPEED = 2; // maximal unit speed in tiles/segment
/////////////////////////////////////////////////////////////////////////////////////
const float RPG_CONST_X = 5;
const float RPG_CONST_Y = 20;
const float RPG_CONST_Z = 10;
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                           locomotion types                                 ** //
// ******************************************************************************** //
const DWORD RPG_NUM_LOCOMOTION_TYPES = 4;
enum ERPGLocomotionTypes
{
    RPGLCM_WALK,
    RPGLCM_LEVITATE,
    RPGLCM_STAY,
    RPGLCM_FLY,
    RPGLCM_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// ** =============================== SKILLS =================================== ** //
// ******************************************************************************** //
const DWORD RPG_NUM_MANUAL_SKILLS = 3;
const DWORD RPG_NUM_ACTUAL_MANUAL_SKILLS = 1;
enum ERPGManualSkills // skills, which must be used explicit
{
    RPGMSKL_SCIENCE,       // general sciencs skill: herbalism + mechanics
    RPGMSKL_STEALING,      // stealing skill
    RPGMSKL_TAME,          // animal taming
    RPGMSKL_LOOT = 101013, // its easier to treat this as skill, number is legacy

    RPGMSKL_FORCE_DWORD = 0x7FFFFFFF
};
const DWORD RPG_NUM_AUTO_SKILLS = 8;
const DWORD RPG_NUM_ACTUAL_AUTO_SKILLS = 6;
enum ERPGAutoSkills // implicit automatic skills
{
    RPGASKL_MELEE,   // general close combat skill
    RPGASKL_ARCHERY, // general ranged combat skill
    RPGASKL_BACKSTAB,
    RPGASKL_ELEMENTAL, // general elemental magic skill
    RPGASKL_SENSE,     // general sense magic skill
    RPGASKL_ASTRAL,    // general astral magic skill
    RPGASKL_STEALTH,
    RPGASKL_AWARENESS,

    RPGASKL_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                                  PERKS                                     ** //
// ******************************************************************************** //
const DWORD RPG_NUM_PERKS = 36; // real number of unique perks
enum ERPGPerks
{
    // weapon (0)
    RPGPRK_SWORD,
    RPGPRK_AXE,
    RPGPRK_DAGGER,
    RPGPRK_SPEAR,
    RPGPRK_HAMMER,
    RPGPRK_BOW,
    RPGPRK_CROSSBOW,
    // magic (7)
    RPGPRK_FIRE,
    RPGPRK_LIGHTNING,
    RPGPRK_ACID,
    RPGPRK_ILLUSION,
    RPGPRK_DIVINATION,
    RPGPRK_ENCHANTMENTS,
    RPGPRK_HEALING,
    RPGPRK_DOMINATION,
    // senses (15)
    RPGPRK_INFRA,
    // secondary attributes (16)
    RPGPRK_HEALTH,
    RPGPRK_MANA,
    RPGPRK_VITALITY,  // health regeneration rate
    RPGPRK_SPIRIT,    // mana (stamina) regeneration rate
                      // speed and load (20)
    RPGPRK_QUICKNESS, // action points
    RPGPRK_LIFT,      // encumbrance
                      // backstab (22)
    RPGPRK_BACKSTAB,
    // primary attributes (23)
    RPGPRK_STRENGTH,
    RPGPRK_DEXTERITY,
    RPGPRK_INTELLIGENCE,

    RPGPRK_LASTPERK,

    RPGPRK_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// ** ============================= ATTACK TYPES =============================== ** //
// ******************************************************************************** //
const DWORD RPG_NUM_ATTACK_TYPES = 7; // maximum number of attack types (physical and magical)
enum ERPGAttackTypes
{
    // physical damage
    RPGAT_IMPALLING,  // spear, rapier, arrow
    RPGAT_SLASHING,   // sword, axe
    RPGAT_CRUSHING,   // staff, hammer, stone
                      // magical damage
    RPGAT_TERMICAL,   // fire, cold
    RPGAT_CHEMICAL,   // acid, poison
    RPGAT_ELECTRICAL, // lightning
                      // general damage (cann't be resisted any way!)
    RPGAT_GENERAL,    // general script damage (ASTRAL :)
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// ** ============================ ARMOR LAYERS ================================ ** //
// ******************************************************************************** //
const DWORD RPG_NUM_ARMOR_LAYERS = 3; // maximum number of armor layers
enum ERPGArmorLayerStates
{
    RPGALS_NOTUSED = 0,  // layer not used
    RPGALS_CRIPPLED = 1, // layer is crippled
    RPGALS_DEAD = 2,     // layer is dead ( used for hit locations )
    RPGALS_ACTIVE = 3,   // layer is active
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// ** ============================ HIT LOCATIONS =============================== ** //
// ******************************************************************************** //
const DWORD RPG_NUM_HIT_LOCATIONS = 6; // number of hit locations
enum ERPGHitLocations
{
    RPGHL_HEAD,  // head
    RPGHL_BODY,  // body
    RPGHL_LHAND, // left hand  (or front left leg)
    RPGHL_RHAND, // right hand (or front right leg)
    RPGHL_LLEG,  // left leg   (or back left leg)
    RPGHL_RLEG,  // right leg  (or back right leg)
    RPGHL_ALL,   // use all hit locations
    RPGHL_UNKNOWN
};
const DWORD RPG_NUM_HIT_LOCATION_TYPES = 4;
enum ERPGHitLocationTypes
{
    RPGHLT_HEAD, // head
    RPGHLT_BODY, // body
    RPGHLT_HAND, // hand
    RPGHLT_LEG,  // left leg
    RPGHLT_UNKNOWN
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                      SRPGStats validness flags                             ** //
// ******************************************************************************** //
enum ERPGStatsValids
{
    // common stats valids
    RPGCSV_HP,          // current HP value
    RPGCSV_HPMAX,       // HP max value
    RPGCSV_MP,          // current MP value
    RPGCSV_MPMAX,       // MP max value
    RPGCSV_ABSORBTIONS, // absorbtions are valid (this flag can be set ONLY for non-unit objects)
    RPGCSV_RESISTANCES, // resistances are valid (this flag can be set ONLY for non-unit objects)
                        // unit stats valids
    RPGUSV_PRIMARY, // all primary stats are valid (strength, dexterity, intelligence and wisdom)
    RPGUSV_SPEED,   // all speed revative stats valid (speed, move points)
    RPGUSV_ACTION,  // action points are valid
    RPGUSV_ENCUMBRANCE, // encumbrance are valid
    RPGUSV_SENSES,      // senses are valid
    RPGUSV_PERCEPTS,    // percepts are valid
    RPGUSV_SKILLS       // skills are valid
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                             Senses and Percepts                            ** //
// ******************************************************************************** //
const DWORD RPG_NUM_SENSES = 6;
enum ERPGSenses
{
    RPGS_SIGHT,     // зрение
    RPGS_INFRA,     // множитель к обычному зрению на ночное врем
    RPGS_SENSELIFE, // чувствительность к живому
    RPGS_HEARING,   // слух
    RPGS_SMELL,     // нюх
    RPGS_TRACKING   // выслеживание по следам
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                              Magic schools                                 ** //
// ******************************************************************************** //
const DWORD RPG_NUM_MAGIC_SCHOOLS = 3;
enum ERPGMagicSchools
{
    RPGMS_ELEMENTAL,
    RPGMS_SENSES,
    RPGMS_ASTRAL,

    RPGMS_UNKNOWN,

    RPGMS_FORCE_DWORD = 0x7FFFFFFF
};
const DWORD RPG_NUM_MAGIC_SUBTYPES = 8;
enum ERPGMagicSubtypes
{
    // elemental school
    RPGMST_FIRE,
    RPGMST_LIGHTNING,
    RPGMST_ACID,
    //  senses school
    RPGMST_ILLUSION,
    RPGMST_DIVINATION,
    // astral school
    RPGMST_ENCHANTMENTS,
    RPGMST_HEALING,
    RPGMST_DOMINATION,

    RPGMST_UNKNOWN,

    RPGMST_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                                Weapon types                                ** //
// ******************************************************************************** //
const DWORD RPG_NUM_WEAPON_TYPES = 7;
enum ERPGWeaponTypes
{
    RPGWST_SWORD,
    RPGWST_AXE,
    RPGWST_DAGGER,
    RPGWST_SPEAR,
    RPGWST_HAMMER,
    RPGWST_BOW,
    RPGWST_CROSSBOW,
    RPGWST_UNKNOWN,
    RPGWST_HANDS,
    RPGWST_HANDS1,
    RPGWST_HANDS2,
    RPGWST_HANDS3,
    RPGWST_HANDS4,
    RPGWST_HANDS5,
    RPGWST_HANDS6,
    RPGWST_HANDS7,
    RPGWST_MAGIC,

    RPGWST_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                                Armor types                                 ** //
// ******************************************************************************** //
const DWORD RPG_NUM_ARMOR_TYPES = 7;
enum ERPGArmorTypes
{
    RPGAST_HELM,
    RPGAST_PLATE,
    RPGAST_LEGGINGS,
    RPGAST_SHIRT,
    RPGAST_PANTS,
    RPGAST_BOOTS,
    RPGAST_GLOVES,
    RPGAST_UNKNOWN,

    RPGAST_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                                Armor types                                 ** //
// ******************************************************************************** //
enum ERPGToolTypes
{
    RPGTOOL_NONE = 0,
    RPGTOOL_HAND = 1,
    RPGTOOL_PICKLOCK = 2,
    RPGTOOL_CROWBAR = 4,
    RPGTOOL_KEY = 8
};
/////////////////////////////////////////////////////////////////////////////////////
// флаги из UserStats::bFlags
enum ERPGStatFlags
{
    RPGSTAT_BACKSTAB = 1,
    RPGSTAT_CRITICAL = 2,
    RPGSTAT_ELECTRIC = 4,
    RPGSTAT_SHRINK = 8, // HP изменились из-за слетания спела strenghten
    RPGSTAT_SHOWHPFLY = 16,
    RPGSTAT_CRIT = 32,
    RPGSTAT_PARRY = 64,
    RPGSTAT_BLOCK = 128
};
/////////////////////////////////////////////////////////////////////////////////////
enum ECanRun
{
    ECR_CAN_RUN,
    ECR_INJURED,
    ECR_OVERLOADED
};
/////////////////////////////////////////////////////////////////////////////////////
enum ERPGSpellID : int
{
	SPELL_ID_FIREARROW = 0,
	SPELL_ID_LIGHTNING = 1,
	SPELL_ID_ACIDRAY = 2,
	SPELL_ID_FIREBALL = 3,
	SPELL_ID_INVOKE_LGHTNG = 4,
	SPELL_ID_COLUMN_ACID = 5,
	SPELL_ID_FIREWALL = 6,
	SPELL_ID_WALL_LGHTNG = 7,
	SPELL_ID_POISONFOG = 8,
	SPELL_ID_PROT_FIRE = 9,
	SPELL_ID_PROT_ELECTRIC = 10,
	SPELL_ID_PROT_ACID = 11,
	SPELL_ID_EAGLE_SIGHT = 12,
	SPELL_ID_INFRAVISION = 13,
	SPELL_ID_DETECT_LIFE = 14,
	SPELL_ID_INVISIBILITY = 15,
	SPELL_ID_SILENCE = 16,
	SPELL_ID_LICHDOM = 17,
	SPELL_ID_FIREWORKS = 18,
	SPELL_ID_STENCH = 19,
	SPELL_ID_CLAIRVOYENCE = 20,
	SPELL_ID_POSSESSION = 21,
	SPELL_ID_SHAPECHANGE = 22,
	SPELL_ID_VISIONFOG = 23,
	SPELL_ID_HEALING = 24,
	SPELL_ID_STUN = 25,
	SPELL_ID_ANTIMAGIC = 26,
	SPELL_ID_LINK = 27,
	SPELL_ID_TELEPORT = 28,
	SPELL_ID_STRENGTHEN = 29,
	SPELL_ID_WEAKEN = 30,
	SPELL_ID_REGENERATION = 31,
	SPELL_ID_FEEBLEMIND = 32,
	SPELL_ID_SPEED = 33,
	SPELL_ID_SLOW = 34,
	SPELL_ID_COMPREHEND = 35,
	SPELL_ID_CHARM = 36,
	SPELL_ID_ENLARGE = 37,
	SPELL_ID_SHRINK = 38,
	SPELL_ID_CAMPFIRE = 39,
	SPELL_ID_RICKMAGIC = 40,
	SPELL_ID_CURSEMAGIC = 41,
	SPELL_ID_ENSORCELL = 42,
	SPELL_ID_INSPIRATION = 43,
	SPELL_ID_GAMECHANGER = 44,
	SPELL_ID_NECRO = 45,
	SPELL_ID_DESTROYING = 46,
	//SPELL_ID_ACIDWAVE = 47,
	//SPELL_ID_MULTILIT = 48,
	//SPELL_ID_FIRERAZE = 49,
	//SPELL_ID_PANIC = 50,
	//SPELL_ID_STORM = 51,
	//SPELL_ID_TORNADO = 52,
	SPELL_ID_TEST = 47,
	SPELL_ID_TEST_2 = 54,
	SPELL_ID_TEST_3 = 55,
	SPELL_ID_VAMP = 53,
	SPELL_ID_FIREBALLDARK = 57,
	SPELL_ID_FIREBALLLIGHT = 58,
	SPELL_ID_FIREARROWDARK = 59,
	SPELL_ID_FIREARROWLIGHT = 60,
	SPELL_ID_FIREWALLDARK = 61,
	SPELL_ID_FIREWALLLIGHT = 62,
	SPELL_ID_LIGHTNINGDARK = 63,
	SPELL_ID_LIGHTNINGLIGHT = 64,
	SPELL_ID_INVOKE_LGHTNGDARK = 65,
	SPELL_ID_INVOKE_LGHTNGLIGHT = 66,
	SPELL_ID_WALL_LGHTNGDARK = 67,
	SPELL_ID_WALL_LGHTNGLIGHT = 68,
	SPELL_ID_ACIDRAYDARK = 69,
	SPELL_ID_ACIDRAYLIGHT = 70,
	SPELL_ID_COLUMN_ACIDDARK = 71,
	SPELL_ID_COLUMN_ACIDLIGHT = 72,
	SPELL_ID_POISONFOGDARK = 73,
	SPELL_ID_POISONFOGLIGHT = 74
};