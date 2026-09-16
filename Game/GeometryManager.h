#pragma once

#include "CMappedResource.h"
#include "Renderer.h"
#include "TLPipeline.h"
#include "GeometryContainer.h"
#include "FigureManager.h"

#include <string>
#include <list>
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
struct SResourceDescriptor
{
    std::string szName;
    CMappedResource *pResource;
};
/////////////////////////////////////////////////////////////////////////////////////
class CGeometryManager : public IGeometryManager
{
private:
    //
    CTLPipeline *pGPU;
    // list of resources
    typedef std::list<SResourceDescriptor> CResList;
    CResList resources;
    // open models
    typedef std::unordered_map<std::string, CSmartPtr<CModelResourceEntry>> CModelResource;
    CModelResource models;
    //
    CRenderer *pRend;
    // geometry containers map
    typedef std::multimap<std::string, CGeometryContainerPtr> CGeometryMap;
    CGeometryMap geometries;

private:
    CGeometryContainer *CreateNewGeometryContainer(const std::string &szName, float fStrength,
                                                   float fGrace, float fTall);
    CGeometryContainer *CreateNewGeometryContainer(const std::string &szName,
                                                   std::vector<std::string> &szNames,
                                                   float fStrength, float fGrace, float fTall);
    bool ChangeRangeGeometry(CGeometryContainer *pGC, CModelResourceEntry *pModel,
                             const std::string &szRangeName, float fStrength, float fGrace,
                             float fTall);
    // figure data loading
    BOOL ReadFigure(CGeometryContainer *pGC, const std::string &szFigureName, float fStrength,
                    float fGrace, float fTall);
    BOOL ReadFigure(CGeometryContainer *pGC, CModelResourceEntry *pModel,
                    const std::string &szFigureName, float fStrength, float fGrace, float fTall);
    void LoadNewFigureData(CGeometryContainer *pGC, const std::string &szFigureName, LPBYTE &pData,
                           float fpStrength, float fpGrace, float fpTall);
    void LoadOldFigureData(CGeometryContainer *pGC, const std::string &szFigureName, LPBYTE &pData,
                           float fpStrength, float fpGrace, float fpTall);
    void LoadNewGeometryData(CGeometryContainer *pGC, const std::string &szFigureName,
                             LPBYTE &pData, float fpStrength, float fpGrace, float fpTall);
    void LoadOldGeometryData(CGeometryContainer *pGC, const std::string &szFigureName,
                             LPBYTE &pData, float fpStrength, float fpGrace, float fpTall);
    // bounding volume datra loading
    void ReadBoundingVolume(SBoundingVolume &bv, LPBYTE &pData, float fStrength, float fGrace,
                            float fTall);
    void LoadNewBoundingVolumeData(SBoundingVolume &bv, LPBYTE &pData, float fStrength,
                                   float fGrace, float fTall);
    void LoadOldBoundingVolumeData(SBoundingVolume &bv, LPBYTE &pData, float fStrength,
                                   float fGrace, float fTall);
    //
    CModelResourceEntry *GetModel(const std::string &szName);

protected:
    // work with resources
    // single resource file
    BOOL AddResource(const std::string &szFileName);
    BOOL RemoveResource(const std::string &szFileName);
    // multiple resource operations
    BOOL RemoveAllResources();
    // open file from the managed resources
    LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);

public:
    CGeometryManager(CRenderer *pRend, const std::string &szResourcePath);
    virtual ~CGeometryManager();
    // for use with geometry container object only
    // get direct x
    CRenderer *GetD3D() { return pRend; }
    // add new range to the geometry (with current complection)
    bool AddGeometryRange(CGeometryContainer *pGC, const std::string &szRangeName);
    //
    virtual int GetNumElements() const { return geometries.size(); }
    virtual int GetUsedMemoryCount() const;
    void CloseFile(LPBYTE pFileImage);
    // get already existed geometry or create and load new one
    virtual IGeometry *GetGeometry(const std::string &szName, float fStrength, float fGrace,
                                   float fTall, bool bNew = false);
    // load new geometry with particular range names
    virtual IGeometry *GetGeometry(const std::string &szName, std::vector<std::string> &szNames,
                                   float fStrength, float fGrace, float fTall, bool bNew = false);
    // release all geometries by calling 'Release' function for each.
    virtual void ReleaseAllGeometries();
    // change complection for entire geometry (for all ranges)
    virtual IGeometry *ChangeComplection(IGeometry *pGC, float fStrength, float fGrace,
                                         float fTall);
    //
    void FreeUnusedResources();
};
