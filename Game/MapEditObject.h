#pragma once
//////////// MapEditObject.h : header file////////
//												//
//												//
//	last time revised   12/01/99				//
//////////////////////////////////////////////////

#include "stdafx.h"
#include "Plot.h"
#include "PolyLine.h"
#include "MEUnitStats.h"
#include "FofSystem.h"
#include "ChunkFile.h"
#include "GeneralUnitLogic.h"
#include "METraps.h"

// --------------------------------------------------------------------------------------------
// Map editor objects consts
// --------------------------------------------------------------------------------------------

extern CString GetTypeByID(ETypeIDs tidTypeID);
extern ETypeIDs GetIDByType(CString szType);

const int N_ID_PATROL_POINT0 = -2000;
const int N_MAX_NUMBER_OF_PATROL_POINTS = 25;

const int N_ID_ACTION_POINT0 = N_ID_PATROL_POINT0 + N_MAX_NUMBER_OF_PATROL_POINTS;
const int N_MAX_NUMBER_OF_ACTION_POINTS = 5;

const int N_ID_SELECTION_POINT0 = N_ID_ACTION_POINT0 + N_MAX_NUMBER_OF_ACTION_POINTS;
const int N_ID_MAX_SELECTION_POINTS = -(N_ID_SELECTION_POINT0 + 20);

const int N_MT_ID_AREA_POINT0 = N_ID_PATROL_POINT0 - 1000;
const int N_MT_ID_TARGET_POINT0 = N_ID_PATROL_POINT0 - 500;
const int N_MT_MAX_AREA_POINTS = 40;
const int N_MT_MAX_TARGET_POINTS = 40;

const int N_ID_EFFECT_MIRROR = N_MT_ID_AREA_POINT0 - 1;

// --------------------------------------------------------------------------------------------
// structure of pro-template of template
// --------------------------------------------------------------------------------------------
struct SObjectProTemplate
{
    CString szDescriptor; // Flora, etc.
    CString szName;
    CString szTemplate;
    CString szUnitPrototype;

    ETypeIDs tidTypeID; // specific ID of object's type (several types can be united by certain
                        // descriptor, e.g. "Flora")
    SComplection complection;
    int nMaxHP;
    CStringArray szPrimTextures;
    CStringArray szSecTextures;
    CStringArray szBodyParts;
    CStringArray szAnimationSet;

    ~SObjectProTemplate();
    SObjectProTemplate();
    SObjectProTemplate(const int) {};
    SObjectProTemplate(SObjectProTemplate &obj) { *this = obj; };

    void operator=(const SObjectProTemplate &object);
};

// --------------------------------------------------------------------------------------------
// structure of template of map object
// --------------------------------------------------------------------------------------------
struct SObjectTemplate
{
    CString szDescriptor; // Flora, etc.
    CString szName;
    CString szTemplate;
    ETypeIDs tidTypeID; // specific ID of object's type (several types can be united by certain
                        // descriptor, e.g. "Flora")
    SComplection complection;
    int nMaxHP;
    int nID;
    CString szPrimTexture;
    CString szSecTexture;
    CString szParentTemplate;
    CString szUnitPrototype;
    CStringArray szBodyParts;

    SObjectTemplate();
    ~SObjectTemplate();

    SObjectTemplate(const int) {};
    SObjectTemplate(SObjectTemplate &obj) { *this = obj; };

    void operator=(const SObjectTemplate &object);
    void Reset();
};

enum EMEObjectType
{
    MEOT_UNKNOW = 0,
    MEOT_OBJECT,
    MEOT_LIGHT,
    MEOT_SOUND,
    MEOT_PARTICLE,
    MEOT_UNIT,
    MEOT_TORCH,
    MEOT_MAGIC_TRAP,
    MEOT_LEVER,
    MEOT_TRAP
};

// --------------------------------------------------------------------------------------------
// class of basic map editor object
// --------------------------------------------------------------------------------------------
class CMEBasicObject : public CChunkTag
{
public:
    int p_nID;                 // this id may use in worldserver and worldclient
    LPMAPOBJECT p_lpMapObject; // pointer to server object
    CPlot3D p_ptPosition;      // position of object or/and visualisation object
    CQuaternion p_qRotation;   // orientation of object
    CString p_szName;          // object name ( for user & script only )
    CString p_szComments;      // comments ( for user only )
    int p_nMobNumber;          // Number of "Mob range" that have this object

public:
    EMEObjectType m_eType; // type of map editor oject

public:
    virtual LPMAPOBJECT GetClientObject();
    virtual void Selected(bool isSelected = false) {}
    virtual CString GetParentTemplate() { return "error"; }
    virtual void MoveOn(CPlot3D inc);
    virtual void RestateObject() { UpdateObject(); }
    virtual void UpdateObject() {} // update object on server
    virtual void UpdatePosition() { UpdateObject(); }
    virtual void Save() {} // save this object to "Mob file"
    virtual void ReadSubChunk() {};
    void Save(CChunkTag *pParentChunk)
    {
        SetParentChunk(pParentChunk);
        Save();
    }

public:
    void GetXYBV(CPoint *aptVertexs);
    CMEBasicObject();
    virtual ~CMEBasicObject() {}
    void operator=(const CMEBasicObject &obj);
};

// --------------------------------------------------------------------------------------------
// class of map editor object
// --------------------------------------------------------------------------------------------
class CMEObject : public CMEBasicObject
{
public:
    CString p_szQuestInfo;
    ETypeIDs p_tidTypeID; // !!!
    CString p_szTemplate; // !!!
    CString p_szParentTemplate;
    int p_nParentTemplate;
    SComplection p_Complection;
    bool p_isUseInScript; // ??
    bool p_isShadow;
    BYTE p_nPlayer;          // ??
    CString p_szPrimTexture; // !!!
    CString p_szSecTexture;
    CStringArray p_aBodyParts;

protected:
    virtual void WriteData();

public:
    virtual void UpdatePosition();
    virtual void RestateObject();
    virtual CString GetParentTemplate();
    virtual void UpdateObject(); // update object on server
    virtual void Save();         // save this object to "Mob file"
    virtual void ReadSubChunk();

public:
    CMEObject(CMEObject &obj) { *this = obj; }
    CMEObject();
    virtual ~CMEObject();
    void operator=(const CMEObject &obj);
};

// --------------------------------------------------------------------------------------------
// class CMEMagicTrap
// --------------------------------------------------------------------------------------------
class CMEMagicTrap : public CMEObject
{
public:
    SMEMagicTrap p_Specific;

public:
    virtual CString GetParentTemplate() { return "MagicTrap"; }
    virtual void Save();
    virtual void ReadSubChunk();

public:
    CMEMagicTrap();
    CMEMagicTrap(CMEMagicTrap &obj) { *this = obj; }
    void operator=(const CMEMagicTrap &obj);
};

// --------------------------------------------------------------------------------------------
// class CMETrap
// --------------------------------------------------------------------------------------------
class CMETrap : public CMEObject
{
public:
    CMETrap() { m_eType = MEOT_TRAP; }
};

// --------------------------------------------------------------------------------------------
// map editor lever object
// --------------------------------------------------------------------------------------------
class CMELever : public CMEObject
{
public:
    SMELever p_Specific;

public:
    //	virtual CString GetParentTemplate() { return "Lever"; }
    virtual void Save();
    virtual void ReadSubChunk();

public:
    CMELever();
    CMELever(CMELever &obj) { *this = obj; }
    void operator=(const CMELever &obj);
};

// --------------------------------------------------------------------------------------------
// class of map editor torch object
// --------------------------------------------------------------------------------------------
class CMETorch : public CMEObject
{
public:
    float p_fStrenght;
    CPlot3D p_ptLink;
    CString p_szSound;

public:
    virtual void Save(); // save this object to "Mob file"
    virtual void ReadSubChunk();
    virtual void UpdateObject(); // update object on server

public:
    CMETorch(CMETorch &obj) { *this = obj; }
    CMETorch();
    virtual ~CMETorch();
    void operator=(const CMETorch &obj);
};

// --------------------------------------------------------------------------------------------
// class of basic map editor effect
// --------------------------------------------------------------------------------------------
class CMEEffect : public CMEBasicObject
{
public:
    CEffect *p_lpEffect;   // pointer to server effect
    bool m_isEffectEnable; // show visualisation object?

public:
    virtual LPMAPOBJECT GetClientObject();
    virtual void EnableVisualisation(bool bEnable = true);
    virtual void MoveOn(CPlot3D inc);
    static LPMAPOBJECT CreateClientObject(CString szTemplate, CString szTexture, CPlot3D ptPosition,
                                          int nID = -1);

public:
    CMEEffect(CMEEffect &obj) { *this = obj; }
    CMEEffect();
    virtual ~CMEEffect();
    void operator=(const CMEEffect &obj);

protected:
    void CreateClientVisualisationObject(CString szTemplate, CString szTexture);
};

// --------------------------------------------------------------------------------------------
// class of map editor particle source effect
// --------------------------------------------------------------------------------------------
class CMEParticle : public CMEEffect
{
public:
    ETypeIDs p_idType;      // type of this effect
    float p_fEfectStrength; // effect strenght

public:
    virtual CString GetParentTemplate() { return "Particle source"; }
    virtual void UpdateObject(); // update object on server
    virtual void Save();         // save this object to "Mob file"
    virtual void ReadSubChunk();

public:
    CMEParticle(CMEParticle &obj) { *this = obj; }
    CMEParticle();
    virtual ~CMEParticle() {}
    void operator=(const CMEParticle &obj);
};

// --------------------------------------------------------------------------------------------
// class of map editor light source
// --------------------------------------------------------------------------------------------
class CMELight : public CMEEffect
{
public:
    CPlot3D p_ptColor;   // color of light
    bool p_isMakeShadow; // calculate shadow from this light source
    float p_fRange;      // range of light from this light source

public:
    virtual CString GetParentTemplate() { return "Light"; }
    virtual void UpdateObject(); // update object on server
    virtual void Save();         // save this object to "Mob file"
    virtual void ReadSubChunk();

public:
    CMELight(CMELight &obj) { *this = obj; }
    CMELight();
    virtual ~CMELight() {}
    void operator=(const CMELight &obj);
};

// --------------------------------------------------------------------------------------------
// class of map editor sound source
// --------------------------------------------------------------------------------------------
class CMESound : public CMEEffect
{
public:
    CStringArray p_aResNames; // names of sound in resoures
    int p_nR;                 // first radius
    int p_nR2;                // second radius
    int p_nSoundsNumb;        // max number of sounds, plays in one time
    int p_nMin;               // minimun volue of range
    int p_nMax;               // maximum volue of range
    CPolyLine *p_lpContur;    // contur of first sounds area
    CPolyLine *p_lpContur2;   // contur of second sounds area
    bool p_isAmbient;         // is this sound ambient?
    bool p_isMusic;           // need music fadeout

public:
    virtual CString GetParentTemplate() { return "Sound"; }
    virtual void EnableVisualisation(bool bEnable = true);

public:
    void MoveContur(CPlot3D inc);
    void RemoveContur();
    void CreateContur();
    void UpdateContur();

public:
    virtual void UpdateObject(); // update object on server
    virtual void Save();         // save this object to "Mob file"
    virtual void ReadSubChunk();

public:
    CMESound(CMESound &obj) { *this = obj; };
    CMESound();
    virtual ~CMESound();
    void operator=(const CMESound &obj);
};

// --------------------------------------------------------------------------------------------
class CGeneralLogicChunk : public CChunkTag
{
public:
    SGeneralUnitLogic *pLogic;
    void Save();

public:
    virtual void ReadSubChunk();
};

// --------------------------------------------------------------------------------------------
class CPatrolPointChunk : public CChunkTag
{
public:
    SPatrolPoint *pPoint;
    void Save();

public:
    virtual void ReadSubChunk();
};

// --------------------------------------------------------------------------------------------
class CActionPointChunk : public CChunkTag
{
public:
    SActionPoint *pAction;
    void Save();

public:
    virtual void ReadSubChunk();
};

// --------------------------------------------------------------------------------------------
class CMEUnit : public CMEObject
{
protected:
    int nCurAlarm;

public:
    SGeneralUnitLogic p_sLogic[N_NUMBER_OF_ALARM];
    CString p_szUnitPrototype;
    CMEUnitSpecific p_UnitSpecific;

public:
    virtual void Selected(bool isSelected = false);
    virtual void UpdateObject(); // update object on server
    virtual void Save();         // save this object to "Mob file"
    virtual void ReadSubChunk();

public:
    CMEUnit(CMEUnit &obj) { *this = obj; }
    CMEUnit();
    virtual ~CMEUnit() {}
    void operator=(const CMEUnit &obj);
};

// --------------------------------------------------------------------------------------------
// class SDiplomation :
// --------------------------------------------------------------------------------------------
struct SDiplomation
{
public:
    EDiplomacy aDipl[N_MAX_PLAYERS][N_MAX_PLAYERS];
    CStringArray aPlayers;

public:
    SDiplomation();
    void operator=(const SDiplomation &dipl);
};

// --------------------------------------------------------------------------------------------
// class SWorldSetting :
// --------------------------------------------------------------------------------------------
struct SWorldSetting
{
public:
    CPlot3D wind_dir;
    float wind_str;
    float time;
    float ambient;
    float sun_light;

public:
    SWorldSetting()
    {
        sun_light = 0.6f;
        ambient = 0.2f;
        time = 14.0f;
        wind_dir = CPlot3D(0, 1, 0);
        wind_str = 0.0f;
    }
    void operator=(const SWorldSetting &ws);
};

typedef CArray<SObjectTemplate, SObjectTemplate &> CTemplateArray;
typedef CArray<SObjectProTemplate, SObjectProTemplate &> CProTemplateArray;

typedef CHashMap<int, CMEBasicObject *> CBasicObjectMap;
typedef CHashMapIterator<int, CMEBasicObject *> CBasicObjectIterator;
typedef CArray<CMEBasicObject *, CMEBasicObject *> CBasicObjectArray;

typedef CHashMap<int, SObjectTemplate *> CTemplateMap;
typedef CHashMap<int, SObjectProTemplate *> CProTemplateMap;

inline D3DVECTOR Plot2Vector(CPlot3D point)
{
    return D3DVECTOR(point.x, point.y, point.z);
}

inline COLORREF Plot2ColorRef(CPlot3D point)
{
    return RGB((BYTE)(point.x * 255.0f), (BYTE)(point.y * 255.0f), (BYTE)(point.z * 255.0f));
}

inline CPlot3D ColorRef2Plot(COLORREF color)
{
    return CPlot3D(float(GetRValue(color)) / 255.0f, float(GetGValue(color)) / 255.0f,
                   float(GetBValue(color)) / 255.0f);
}
