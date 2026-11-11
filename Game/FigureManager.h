#pragma once

#include "ResourceManager.h"
#include "Figure.h"

#ifndef _A4
#include "I3DMenuRects.h"
#endif

#include <unordered_map>
/////////////////////////////////////////////////////////////////////////////////////
struct SAnimationNode;
class CAnimationSystem;
class CAnimationNode;
class CAnimationTrack;
class CAnimationSystem2;
struct SADEntry;
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
class CAnimationSystem3;
class CAnimationNode3;
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
/////////////////////////////////////////////////////////////////////////////////////
class CModelResourceEntry : public CRefCount
{
    typedef std::unordered_map<std::string, CMappedResource *> CAnimationResource;
    // resource pointers to close files
    BYTE *pModelFile;
    BYTE *pBonesFile;
    BYTE *pAnimsFile;
    DWORD pAnimsFileSize;
    class CResourceManager *pManager1;
    class CGeometryManager *pManager2;
    // resources
    CMappedResource *pResModel;
    CMappedResource *pResBones;
    CMappedResource *pResAllAnimations;
    CAnimationResource resAnimations;
    std::unordered_map<std::string, std::pair<const BYTE *, DWORD>> animationSources;

public:
    CModelResourceEntry(class CResourceManager *pManager);
    CModelResourceEntry(class CGeometryManager *pManager);
    ~CModelResourceEntry();
    //
    bool OpenModelResources(BYTE *pModel, DWORD modelSize, BYTE *pBones, DWORD bonesSize,
                            BYTE *pAnimations, DWORD animationSize);
    //
    CMappedResource *GetAnimation(const std::string &szName);
    bool GetAnimationSourceBytes(const std::string& szName, const void*& bytes, DWORD& size);
    bool ResolveAnimationName(const std::string& szName, std::string& resolvedName) const;
    CMappedResource *GetModel() { return pResModel; }
    CMappedResource *GetBones() { return pResBones; }
};
/////////////////////////////////////////////////////////////////////////////////////
class CFigureManager : public CResourceManager
{
private:
    typedef std::unordered_map<std::string, CSmartPtr<CModelResourceEntry>> CModelResource;
    CModelResource models;

private:
    virtual BOOL Create();

    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

    void LoadOldBoundingVolume(SBoundingVolume &bv, LPBYTE lpData, float fpStrength, float fpGrace,
                               float fpTall);
    void JustLoadBoundingVolume(SBoundingVolume &bv, LPBYTE lpData, float fpStrength, float fpGrace,
                                float fpTall, DWORD dwSignature);

public:
    CFigureManager();
    virtual ~CFigureManager();
    //
    virtual BOOL AddNewResource(LPCSTR pszResourceFileName);
    LPGAMEBASE CreateData(ETypeIDs tidTypeID, int nNewID = -1);
    //
    LPFIGURE CreateI3DFigure(LPCSTR pszName = 0);
    LPFIGURE CreateFigure(LPCSTR pszMask, LPCSTR pszName, CModelResourceEntry *pModel,
                          float strength, float grace, float tall, BOOL bLoad,
                          BOOL bHeapAllocation = FALSE);
    BOOL LoadBoundingVolume(SBoundingVolume &bv, CModelResourceEntry *pModel, LPCSTR pszMask,
                            LPCSTR pszName, float strength, float grace, float tall);
    BOOL ResizeFigure(LPFIGURE lpFigure, CModelResourceEntry *pModel, float strength, float grace,
                      float tall);
    BOOL MakeAnimationNode(SAnimationNode *lpNode, LPCSTR pszMask, float strength, float grace,
                           float tall);
    BOOL LoadAnimation(SAnimationNode *lpNode);
    BOOL ResizeAnimationNode(SAnimationNode *lpNode, float strength, float grace, float tall);
    BOOL LoadSkeleton(CAnimationSystem *pAnimations, LPCSTR pszFile);
    BOOL LoadSkeleton(CAnimationSystem2 *pAnimationSystem, LPCSTR pszMaskName);
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    BOOL LoadSkeleton(CAnimationSystem3 *pAnimationSystem, LPCSTR pszMaskName);
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
       //
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    BOOL LoadAnimationTrack(CAnimationNode3 *pNode, LPCSTR pszAnimationName);
    BOOL LoadAnimationNodeBone(CAnimationNode3 *pNode);
    BOOL LoadAnimation(CAnimationSystem3 *pAnimationSystem, LPCSTR pszMaskName,
                       const SADEntry *pADEntry);
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    BOOL LoadAnimationTrack(CAnimationNode *lpNode, CAnimationTrack *lpTrack,
                            LPCSTR pszAnimationName);
    BOOL LoadAnimationNodeBone(CModelResourceEntry *pModel, CAnimationNode *lpNode);
    BOOL LoadAnimation(CAnimationSystem2 *pAnimationSystem, LPCSTR pszMaskName,
                       const SADEntry *pADEntry);
    //
    CModelResourceEntry *GetModel(const std::string &szModel);
    CMappedResource *GetModelAnimation(const std::string &szModel, const std::string &szAnimation);

    BOOL LoadMenuRects(SI3DMenuRects &rects);
    //
    //
    //
    void FreeUnusedResources();
    //
    //
    //
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "figures"; };
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CFigureManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_FIGUREMANAGER; }
};
