#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GeometryManager.h"
#include "figFormat.h"
#include "StringProcessing.h"
#include "Polygon.h"

#include <string>
#include <vector>
#include <io.h>
/////////////////////////////////////////////////////////////////////////////////////
CGeometryManager::CGeometryManager(CRenderer *pNewRend, const std::string &szResourcePath)
    : pRend(pNewRend)
{
    pGPU = pRend->GetGeometryProcessor()->GetGeometryProcessor();
    //
    AddResource(szResourcePath + "figures.res");
    AddResource(szResourcePath + "menus.res");
}
/////////////////////////////////////////////////////////////////////////////////////
CGeometryManager::~CGeometryManager()
{
    RemoveAllResources();
    //
    /*
for ( auto pos =  models.begin(); pos != models.end(); ++pos )
delete pos->second;
    */
    //
    pRend = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// add new resource file and open it
BOOL CGeometryManager::AddResource(const std::string &szFileName)
{
    // first, check for such resource
    for (auto iter = resources.begin(); iter != resources.end(); ++iter)
    {
        if (iter->szName == szFileName)
            return TRUE;
    }
    // add
    if (_access(szFileName.c_str(), 4) == -1)
        return FALSE;
    resources.push_back(SResourceDescriptor());
    SResourceDescriptor &descriptor = resources.back();
    descriptor.szName = szFileName;
    descriptor.pResource = dbgnew CMappedResource();
    if (!descriptor.pResource->OpenResource(szFileName.c_str()))
    {
        resources.pop_back();
        ReportWarning("Can't open resource file \"%s\"\n", szFileName);
        return FALSE;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CGeometryManager::RemoveResource(const std::string &szFileName)
{
    for (auto iter = resources.begin(); iter != resources.end(); ++iter)
    {
        if (iter->szName == szFileName)
        {
            iter->pResource->CloseResource();
            delete iter->pResource;
            resources.erase(iter);
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CGeometryManager::RemoveResources()
// close all opened resources and remove all of it from 'resources' array
BOOL CGeometryManager::RemoveAllResources()
{
    // close all resources
    for (auto iter = resources.begin(); iter != resources.end(); ++iter)
    {
        iter->pResource->CloseResource();
        delete iter->pResource;
    }
    // clear list
    resources.clear();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// try to open file by name in all managed resources
LPBYTE CGeometryManager::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    for (auto iter = resources.begin(); iter != resources.end(); ++iter)
    {
        LPBYTE pData = iter->pResource->GetFile(pszFileName, pnDataSize);
        if (pData)
            return pData;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGeometryManager::CloseFile(LPBYTE pFileImage)
{
    for (auto iter = resources.begin(); iter != resources.end(); ++iter)
        iter->pResource->CloseFile(pFileImage);
}
/////////////////////////////////////////////////////////////////////////////////////
// release all geometries.
// if geometry is referenced, this will not be deleted, but missed from manager
// in nature, this geometry will be deleted only in unreferenced case
void CGeometryManager::ReleaseAllGeometries()
{
    geometries.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// get geometry by name and complection
// if such geometry doesn't exist, create new and fill with data
IGeometry *CGeometryManager::GetGeometry(const std::string &szName, float fStrength, float fGrace,
                                         float fTall, bool bNew)
{
    if (bNew)
        return CreateNewGeometryContainer(szName, fStrength, fGrace, fTall);

    std::pair<CGeometryMap::iterator, CGeometryMap::iterator> range = geometries.equal_range(
        szName);

    if (range.first == range.second) // ther is no one geometry containers with such name
        return CreateNewGeometryContainer(szName, fStrength, fGrace, fTall);
    else // geometry containers with such name are exist
    {
        // iterate among found containers in order to find appropriate complection
        for (auto iter = range.first; iter != range.second; ++iter)
        {
            CPlot3D pt;
            pt.x = iter->second->GetStrength() - fStrength;
            pt.y = iter->second->GetGrace() - fGrace;
            pt.z = iter->second->GetTall() - fTall;
            if (fabs2(pt) < 0.00001)
                return iter->second;
        }
        // ther is no appropriate complection - create new container and fill it with data
        return CreateNewGeometryContainer(szName, fStrength, fGrace, fTall);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
IGeometry *CGeometryManager::GetGeometry(const std::string &szName,
                                         std::vector<std::string> &szNames, float fStrength,
                                         float fGrace, float fTall, bool bNew)
{
    if (bNew)
        return CreateNewGeometryContainer(szName, szNames, fStrength, fGrace, fTall);

    std::pair<CGeometryMap::iterator, CGeometryMap::iterator> range = geometries.equal_range(
        szName);

    if (range.first == range.second) // ther is no one geometry containers with such name
        return CreateNewGeometryContainer(szName, szNames, fStrength, fGrace, fTall);
    else // geometry containers with such name are exist
    {
        // iterate among found containers in order to find appropriate complection
        for (auto iter = range.first; iter != range.second; ++iter)
        {
            CPlot3D pt;
            pt.x = iter->second->GetStrength() - fStrength;
            pt.y = iter->second->GetGrace() - fGrace;
            pt.z = iter->second->GetTall() - fTall;
            if (fabs2(pt) < 0.00001)
            {
                CGeometryContainer *pGC = iter->second;
                CModelResourceEntry *pModel = GetModel(pGC->GetName());
                for (auto striter = szNames.begin(); striter != szNames.end(); ++striter)
                {
                    if (!pGC->IsHaveRange(*striter))
                    {
                        if (pModel)
                            ReadFigure(pGC, pModel, *striter, pGC->GetStrength(), pGC->GetGrace(),
                                       pGC->GetTall());
                        else
                            ReadFigure(pGC, *striter, pGC->GetStrength(), pGC->GetGrace(),
                                       pGC->GetTall());
                    }
                }
                return pGC;
            }
        }
        // ther is no appropriate complection - create new container and fill it with data
        return CreateNewGeometryContainer(szName, szNames, fStrength, fGrace, fTall);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// add new geometry range
// if requestd range already exist, just return with 'true'
bool CGeometryManager::AddGeometryRange(CGeometryContainer *pGC, const std::string &szRangeName)
{
    if (pGC->IsHaveRange(szRangeName))
        return true;

    CModelResourceEntry *pModel = GetModel(pGC->GetName());
    if (pModel)
        return ReadFigure(pGC, pModel, szRangeName, pGC->GetStrength(), pGC->GetGrace(),
                          pGC->GetTall()) != 0;
    else
        return ReadFigure(pGC, szRangeName, pGC->GetStrength(), pGC->GetGrace(), pGC->GetTall()) !=
               0;
}
/////////////////////////////////////////////////////////////////////////////////////
IGeometry *CGeometryManager::ChangeComplection(IGeometry *pGeometry, float fStrength, float fGrace,
                                               float fTall)
{
    if (pGeometry->GetRef() > 2)
        return GetGeometry(pGeometry->GetName(), fStrength, fGrace, fTall);
    //
    CGeometryContainer *pGC = static_cast<CGeometryContainer *>(pGeometry);
    if (!pGC)
        return nullptr;
    // get all range names
    std::vector<std::string> szNames;
    pGC->GetRangeNames(szNames);
    // change complection values for geometry
    pGC->SetNewComplection(fStrength, fGrace, fTall);
    // change geometry for each range
    CModelResourceEntry *pModel = GetModel(pGeometry->GetName());
    for (auto iter = szNames.begin(); iter != szNames.end(); ++iter)
        ChangeRangeGeometry(pGC, pModel, *iter, fStrength, fGrace, fTall);
    //
    return pGeometry;
}
/////////////////////////////////////////////////////////////////////////////////////
// create new geometry container and call functions to load data
CGeometryContainer *CGeometryManager::CreateNewGeometryContainer(const std::string &szMaskName,
                                                                 float fStrength, float fGrace,
                                                                 float fTall)
{
    // create new container
    CModelResourceEntry *pModel = GetModel(szMaskName);
    CGeometryContainer *pGC = dbgnew CGeometryContainer(this, szMaskName, fStrength, fGrace, fTall,
                                                        pModel);
    BYTE *pData = 0;
    if (pModel)
        pData = pModel->GetModel()->GetFile(szMaskName.c_str());
    else
        pData = GetFile((szMaskName + ".lnk").c_str());
    if (!pData)
        return reinterpret_cast<CGeometryContainer *>(
            ReportError("Can't find link file for \"%s\"\n", szMaskName.c_str()));
    // number of animation nodes
    DWORD num = *reinterpret_cast<DWORD *>(pData); //*((DWORD*)pData);
    pData += sizeof(DWORD);
    // read node names and read figures from this node names
    std::string szNodeName, szParentNodeName;
    for (int i = 0; i < num; ++i)
    {
        // read node name
        LoadString(szNodeName, pData);
        LoadString(szParentNodeName, pData);
        // read figure data from node name
        if (pModel)
            ReadFigure(pGC, pModel, szNodeName, fStrength, fGrace, fTall);
        else
            ReadFigure(pGC, szNodeName, fStrength, fGrace, fTall);
    }
    // register new container in the map
    geometries.emplace(std::pair<const std::string, CGeometryContainerPtr>(szMaskName, pGC));
    // return just created container
    return pGC;
}
/////////////////////////////////////////////////////////////////////////////////////
// create new geometry container and call functions to load data
CGeometryContainer *CGeometryManager::CreateNewGeometryContainer(const std::string &szMaskName,
                                                                 std::vector<std::string> &szNames,
                                                                 float fStrength, float fGrace,
                                                                 float fTall)
{
    // create new container
    CModelResourceEntry *pModel = GetModel(szMaskName);
    CGeometryContainer *pGC = dbgnew CGeometryContainer(this, szMaskName, fStrength, fGrace, fTall,
                                                        pModel);
    // read figures from requested names
    if (pModel)
    {
        for (auto iter = szNames.begin(); iter != szNames.end(); ++iter)
            ReadFigure(pGC, pModel, *iter, fStrength, fGrace, fTall);
    }
    else
    {
        for (auto iter = szNames.begin(); iter != szNames.end(); ++iter)
            ReadFigure(pGC, *iter, fStrength, fGrace, fTall);
    }
    // register new container in the map
    geometries.emplace(std::pair<const std::string, CGeometryContainerPtr>(szMaskName, pGC));
    // return just created container
    return pGC;
}
/////////////////////////////////////////////////////////////////////////////////////
// read figure data
BOOL CGeometryManager::ReadFigure(CGeometryContainer *pGC, const std::string &szFigureName,
                                  float fStrength, float fGrace, float fTall)
{
    BYTE *pData = GetFile((pGC->GetName() + szFigureName + ".fig").c_str());
    if (!pData)
        return FALSE;
    //
    if ((ptr_cast<SFigureFormatHeader>(pData)->dwSignature & FIG_SIGNATURE_MASK) ==
        FIG_SIGNATURE_FIGN)
        LoadNewFigureData(pGC, szFigureName, pData, fStrength, fGrace, fTall);
    else
        LoadOldFigureData(pGC, szFigureName, pData, fStrength, fGrace, fTall);

    return TRUE;
}
BOOL CGeometryManager::ReadFigure(CGeometryContainer *pGC, CModelResourceEntry *pModel,
                                  const std::string &szFigureName, float fStrength, float fGrace,
                                  float fTall)
{
    BYTE *pData = pModel->GetModel()->GetFile(szFigureName.c_str());
    if (!pData)
        return FALSE;
    //
    if ((ptr_cast<SFigureFormatHeader>(pData)->dwSignature & FIG_SIGNATURE_MASK) ==
        FIG_SIGNATURE_FIGN)
        LoadNewFigureData(pGC, szFigureName, pData, fStrength, fGrace, fTall);
    else
        LoadOldFigureData(pGC, szFigureName, pData, fStrength, fGrace, fTall);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load figure data in new format
void CGeometryManager::LoadNewFigureData(CGeometryContainer *pGC, const std::string &szFigureName,
                                         LPBYTE &pData, float fpStrength, float fpGrace,
                                         float fpTall)
{
    SRangeExternals &ext = pGC->GetRangeExternals(szFigureName);
    SFigureFormatHeader hdr;
    CXYZW4Array plots4;
    //
    memcpy(&hdr, pData, sizeof(hdr));
    pData += sizeof(hdr);
    // process material and texture type IDs
    ext.tidMaterial = *ptr_cast<ETypeIDs>(pData);
    pData += sizeof(ETypeIDs);
    ext.tidTexture = *ptr_cast<ETypeIDs>(pData);
    pData += sizeof(ETypeIDs);
    // process bounding volume
    // skip bounding volume
    if (hdr.dwSignature == FIG_SIGNATURE_FIG8)
    {
        pData += 8 * sizeof(CPlot3D);
        pData += 8 * sizeof(CPlot3D);
        pData += 8 * sizeof(CPlot3D);
        pData += 8 * sizeof(float);
    }
    else
    {
        pData += sizeof(CPlot3D);
        pData += sizeof(CPlot3D);
        pData += sizeof(CPlot3D);
        pData += sizeof(float);
    }
    // process plots4
    plots4.SetSize(hdr.dwNumVertices);
    if (hdr.dwSignature == FIG_SIGNATURE_FIG8)
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            float temp[2][4];
            // assemble 'x' components
            // strength
            float *weaks = reinterpret_cast<float *>(pData);
            float *strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].x[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].x[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].x[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].x[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'x' components are complete
            plots4[i].x[0] += fpTall * (temp[0][0] - plots4[i].x[0]);
            plots4[i].x[1] += fpTall * (temp[0][1] - plots4[i].x[1]);
            plots4[i].x[2] += fpTall * (temp[0][2] - plots4[i].x[2]);
            plots4[i].x[3] += fpTall * (temp[0][3] - plots4[i].x[3]);
            //
            pData += 8 * 4 * sizeof(float);
            //
            // assemble 'y' components
            // strength
            weaks = reinterpret_cast<float *>(pData);
            strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].y[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].y[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].y[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].y[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'y' components are complete
            plots4[i].y[0] += fpTall * (temp[0][0] - plots4[i].y[0]);
            plots4[i].y[1] += fpTall * (temp[0][1] - plots4[i].y[1]);
            plots4[i].y[2] += fpTall * (temp[0][2] - plots4[i].y[2]);
            plots4[i].y[3] += fpTall * (temp[0][3] - plots4[i].y[3]);
            //
            pData += 8 * 4 * sizeof(float);
            //
            // assemble 'z' components
            // strength
            weaks = reinterpret_cast<float *>(pData);
            strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].z[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].z[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].z[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].z[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'z' components are complete
            plots4[i].z[0] += fpTall * (temp[0][0] - plots4[i].z[0]);
            plots4[i].z[1] += fpTall * (temp[0][1] - plots4[i].z[1]);
            plots4[i].z[2] += fpTall * (temp[0][2] - plots4[i].z[2]);
            plots4[i].z[3] += fpTall * (temp[0][3] - plots4[i].z[3]);
            //
            pData += 8 * 4 * sizeof(float);
            //
            // assemble 'w' components - just set it to the '1'
            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }
    else
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            memcpy(plots4[i].x, pData, sizeof(plots4[i].x));
            pData += sizeof(plots4[i].x);

            memcpy(plots4[i].y, pData, sizeof(plots4[i].y));
            pData += sizeof(plots4[i].y);

            memcpy(plots4[i].z, pData, sizeof(plots4[i].z));
            pData += sizeof(plots4[i].z);

            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }

    // calculate bounding volume
    if (plots4.GetSize())
        pGPU->CalculateBoundingVolume(&ext.bv, plots4.GetData(), plots4.GetSize());
    // process norms4
    SXYZW4 *norms4 = ptr_cast<SXYZW4>(pData);
    pData += hdr.dwNumNormales * sizeof(SXYZW4);
    // process maps
    CPlot2D *maps = ptr_cast<CPlot2D>(pData);
    pData += hdr.dwNumMaps * sizeof(CPlot2D);
    // process indices
    WORD *indices = ptr_cast<WORD>(pData);
    pData += hdr.dwNumIndices * sizeof(WORD);
    // process components
    SVertexComponents *components = ptr_cast<SVertexComponents>(pData);
    pData += hdr.dwNumComponents * sizeof(SVertexComponents);
    // process lightcomponents
    SGvNvComponents *lightcomponents = ptr_cast<SGvNvComponents>(pData);
    pData += hdr.dwNumLightComponents * sizeof(SGvNvComponents);
    //
    pGC->AddRange(szFigureName, plots4.GetData(), hdr.dwNumVertices, norms4, hdr.dwNumNormales,
                  maps, hdr.dwNumMaps, components, hdr.dwNumComponents, lightcomponents,
                  hdr.dwNumLightComponents, indices, hdr.dwNumIndices);
}
/////////////////////////////////////////////////////////////////////////////////////
// load figure data in old format
void CGeometryManager::LoadOldFigureData(CGeometryContainer *pGC, const std::string &szFigureName,
                                         LPBYTE &pData, float fpStrength, float fpGrace,
                                         float fpTall)
{
    SRangeExternals &ext = pGC->GetRangeExternals(szFigureName);
    CXYZW4Array plots4;
    float valSmall;
    float valTall;
    float *smalls;
    float *talls;
    DWORD num;
    int i;
    // process material and texture type IDs
    ext.tidMaterial = *ptr_cast<ETypeIDs>(pData);
    pData += sizeof(ETypeIDs);
    ext.tidTexture = *ptr_cast<ETypeIDs>(pData);
    pData += sizeof(ETypeIDs);
    // skip bounding volume
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(float);
    // process plots4
    DWORD dwNumVertices = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    plots4.SetSize(num);
    for (i = 0; i < int(num); i++)
    {
        // +0 <- weak
        // +4 <- strong
        // +8 <- fat
        // first - small components, then - tall components
        // processing x component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th x value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].x[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st x value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].x[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd x value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].x[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd x value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].x[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing y component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th y value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].y[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st y value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].y[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd y value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].y[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd y value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].y[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing z component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th z value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].z[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st z value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].z[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd z value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].z[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd z value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].z[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing w component (4)
        plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
    }
    // calculate bounding volume
    if (plots4.GetSize())
        pGPU->CalculateBoundingVolume(&ext.bv, plots4.GetData(), plots4.GetSize());
    // process norms4
    DWORD dwNumNormales = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    SXYZW4 *norms4 = ptr_cast<SXYZW4>(pData);
    pData += num * sizeof(SXYZW4);
    // process maps
    DWORD dwNumMaps = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    CPlot2D *maps = ptr_cast<CPlot2D>(pData);
    pData += num * sizeof(CPlot2D);
    // process faces (just skip)
    num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    pData += num * sizeof(CFace);
    // process components
    DWORD dwNumComponents = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    SVertexComponents *components = ptr_cast<SVertexComponents>(pData);
    pData += num * sizeof(SVertexComponents);
    // process lightcomponents
    DWORD dwNumLightComponents = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    SGvNvComponents *lightcomponents = ptr_cast<SGvNvComponents>(pData);
    pData += num * sizeof(SGvNvComponents);
    // process indices
    DWORD dwNumIndices = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    WORD *indices = ptr_cast<WORD>(pData);
    pData += num * sizeof(WORD);
    //
    pGC->AddRange(szFigureName, plots4.GetData(), dwNumVertices, norms4, dwNumNormales, maps,
                  dwNumMaps, components, dwNumComponents, lightcomponents, dwNumLightComponents,
                  indices, dwNumIndices);
}
/////////////////////////////////////////////////////////////////////////////////////
// read bounding volume data
void CGeometryManager::ReadBoundingVolume(SBoundingVolume &bv, LPBYTE &pData, float fStrength,
                                          float fGrace, float fTall)
{
    if ((ptr_cast<SFigureFormatHeader>(pData)->dwSignature & FIG_SIGNATURE_MASK) ==
        FIG_SIGNATURE_FIGN)
    {
        pData += sizeof(SFigureFormatHeader) + 2 * sizeof(ETypeIDs);
        LoadNewBoundingVolumeData(bv, pData, fStrength, fGrace, fTall);
    }
    else
    {
        pData += 2 * sizeof(ETypeIDs);
        LoadOldBoundingVolumeData(bv, pData, fStrength, fGrace, fTall);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// load bounding volume in old format
void CGeometryManager::LoadOldBoundingVolumeData(SBoundingVolume &bv, LPBYTE &pData,
                                                 float fStrength, float fGrace, float fTall)
{
    CPlot3D bbCenter, bbMin, bbMax;
    CPlot3D ptSmall, ptTall;
    CPlot3D *ptSmalls;
    CPlot3D *ptTalls;
    float valTall;
    float *smalls;
    float *talls;
    float radius;
    // process bounding volumes ( small: weak, strong, fat; tall: weak, strong, fat )
    // bbCenter
    ptSmalls = (CPlot3D *)pData;
    ptTalls = ((CPlot3D *)pData) + 3;
    bbCenter = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fStrength;
    bbCenter += (ptSmalls[2] - bbCenter) * fGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fStrength;
    ptTall += (ptTalls[2] - ptTall) * fGrace;
    bbCenter += (ptTall - bbCenter) * fTall;
    pData += 6 * sizeof(CPlot3D);
    // bbMin
    ptSmalls = (CPlot3D *)pData;
    ptTalls = ((CPlot3D *)pData) + 3;
    bbMin = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fStrength;
    bbMin += (ptSmalls[2] - bbMin) * fGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fStrength;
    ptTall += (ptTalls[2] - ptTall) * fGrace;
    bbMin += (ptTall - bbMin) * fTall;
    pData += 6 * sizeof(CPlot3D);
    // bbMax
    ptSmalls = (CPlot3D *)pData;
    ptTalls = ((CPlot3D *)pData) + 3;
    bbMax = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fStrength;
    bbMax += (ptSmalls[2] - bbMax) * fGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fStrength;
    ptTall += (ptTalls[2] - ptTall) * fGrace;
    bbMax += (ptTall - bbMax) * fTall;
    pData += 6 * sizeof(CPlot3D);
    // radius
    smalls = (float *)pData;
    talls = ((float *)pData) + 3;
    radius = smalls[0] + (smalls[1] - smalls[0]) * fStrength;
    radius += (smalls[2] - radius) * fGrace;
    valTall = talls[0] + (talls[1] - talls[0]) * fStrength;
    valTall += (talls[2] - valTall) * fGrace;
    radius += (valTall - radius) * fTall;
    pData += 6 * sizeof(float);
    // set this data to bounding volume
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
}
/////////////////////////////////////////////////////////////////////////////////////
// load bounding volume in new format
void CGeometryManager::LoadNewBoundingVolumeData(SBoundingVolume &bv, LPBYTE &pData,
                                                 float fStrength, float fGrace, float fTall)
{
    CPlot3D bbCenter, bbMin, bbMax;
    float radius;
    // process bounding volume
    CPlot3D ptTemp[2];
    // center
    CPlot3D *plots = reinterpret_cast<CPlot3D *>(pData);
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    bbCenter = ptTemp[0] + fGrace * (ptTemp[1] - ptTemp[0]);
    plots = reinterpret_cast<CPlot3D *>(pData + 4 * sizeof(CPlot3D));
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    ptTemp[0] += fGrace * (ptTemp[1] - ptTemp[0]);
    bbCenter += fTall * (ptTemp[0] - bbCenter);
    //
    pData += 8 * sizeof(CPlot3D);
    // min
    plots = reinterpret_cast<CPlot3D *>(pData);
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    bbMin = ptTemp[0] + fGrace * (ptTemp[1] - ptTemp[0]);
    plots = reinterpret_cast<CPlot3D *>(pData + 4 * sizeof(CPlot3D));
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    ptTemp[0] += fGrace * (ptTemp[1] - ptTemp[0]);
    bbMin += fTall * (ptTemp[0] - bbMin);
    //
    pData += 8 * sizeof(CPlot3D);
    // max
    plots = reinterpret_cast<CPlot3D *>(pData);
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    bbMax = ptTemp[0] + fGrace * (ptTemp[1] - ptTemp[0]);
    plots = reinterpret_cast<CPlot3D *>(pData + 4 * sizeof(CPlot3D));
    ptTemp[0] = plots[0] + fStrength * (plots[1] - plots[0]);
    ptTemp[1] = plots[2] + fStrength * (plots[3] - plots[2]);
    ptTemp[0] += fGrace * (ptTemp[1] - ptTemp[0]);
    bbMax += fTall * (ptTemp[0] - bbMax);
    //
    pData += 8 * sizeof(CPlot3D);
    // radius
    float fpTemp[2];
    float *rds = reinterpret_cast<float *>(pData);
    fpTemp[0] = rds[0] + fStrength * (rds[1] - rds[0]);
    fpTemp[1] = rds[2] + fStrength * (rds[3] - rds[2]);
    radius = fpTemp[0] + fGrace * (fpTemp[1] - fpTemp[0]);
    rds = reinterpret_cast<float *>(pData + 4 * sizeof(float));
    fpTemp[0] = rds[0] + fStrength * (rds[1] - rds[0]);
    fpTemp[1] = rds[2] + fStrength * (rds[3] - rds[2]);
    fpTemp[0] += fGrace * (fpTemp[1] - fpTemp[0]);
    radius += fTall * (fpTemp[0] - radius);
    //
    pData += 8 * sizeof(float);
    // set data
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryManager::ChangeRangeGeometry(CGeometryContainer *pGC, CModelResourceEntry *pModel,
                                           const std::string &szRangeName, float fStrength,
                                           float fGrace, float fTall)
{
    BYTE *pData = 0;
    if (pModel)
        pData = pModel->GetModel()->GetFile(szRangeName.c_str());
    else
        pData = GetFile((pGC->GetName() + szRangeName + ".fig").c_str());
    if (!pData)
        return FALSE;
    //
    if ((ptr_cast<SFigureFormatHeader>(pData)->dwSignature & FIG_SIGNATURE_MASK) ==
        FIG_SIGNATURE_FIGN)
        LoadNewGeometryData(pGC, szRangeName, pData, fStrength, fGrace, fTall);
    else
        LoadOldGeometryData(pGC, szRangeName, pData, fStrength, fGrace, fTall);

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// load figure data in new format
void CGeometryManager::LoadNewGeometryData(CGeometryContainer *pGC, const std::string &szFigureName,
                                           LPBYTE &pData, float fpStrength, float fpGrace,
                                           float fpTall)
{
    SRangeExternals &ext = pGC->GetRangeExternals(szFigureName);
    SFigureFormatHeader hdr;
    CXYZW4Array plots4;
    //
    memcpy(&hdr, pData, sizeof(hdr));
    pData += sizeof(hdr);
    // process material and texture type IDs
    pData += sizeof(ETypeIDs);
    pData += sizeof(ETypeIDs);
    // process bounding volume
    // skip bounding volume
    pData += 8 * sizeof(CPlot3D);
    pData += 8 * sizeof(CPlot3D);
    pData += 8 * sizeof(CPlot3D);
    pData += 8 * sizeof(float);
    // process plots4
    plots4.SetSize(hdr.dwNumVertices);
    for (int i = 0; i < int(hdr.dwNumVertices); ++i)
    {
        float temp[2][4];
        // assemble 'x' components
        // strength
        float *weaks = reinterpret_cast<float *>(pData);
        float *strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        plots4[i].x[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
        plots4[i].x[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
        plots4[i].x[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
        plots4[i].x[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
        // strength
        weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
        temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
        temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
        temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
        // tall => all 'x' components are complete
        plots4[i].x[0] += fpTall * (temp[0][0] - plots4[i].x[0]);
        plots4[i].x[1] += fpTall * (temp[0][1] - plots4[i].x[1]);
        plots4[i].x[2] += fpTall * (temp[0][2] - plots4[i].x[2]);
        plots4[i].x[3] += fpTall * (temp[0][3] - plots4[i].x[3]);
        //
        pData += 8 * 4 * sizeof(float);
        //
        // assemble 'y' components
        // strength
        weaks = reinterpret_cast<float *>(pData);
        strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        plots4[i].y[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
        plots4[i].y[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
        plots4[i].y[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
        plots4[i].y[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
        // strength
        weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
        temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
        temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
        temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
        // tall => all 'y' components are complete
        plots4[i].y[0] += fpTall * (temp[0][0] - plots4[i].y[0]);
        plots4[i].y[1] += fpTall * (temp[0][1] - plots4[i].y[1]);
        plots4[i].y[2] += fpTall * (temp[0][2] - plots4[i].y[2]);
        plots4[i].y[3] += fpTall * (temp[0][3] - plots4[i].y[3]);
        //
        pData += 8 * 4 * sizeof(float);
        //
        // assemble 'z' components
        // strength
        weaks = reinterpret_cast<float *>(pData);
        strongs = reinterpret_cast<float *>(pData + 4 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 8 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 12 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        plots4[i].z[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
        plots4[i].z[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
        plots4[i].z[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
        plots4[i].z[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
        // strength
        weaks = reinterpret_cast<float *>(pData + 16 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 20 * sizeof(float));
        temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        weaks = reinterpret_cast<float *>(pData + 24 * sizeof(float));
        strongs = reinterpret_cast<float *>(pData + 28 * sizeof(float));
        temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
        temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
        temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
        temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
        // grace
        temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
        temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
        temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
        temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
        // tall => all 'z' components are complete
        plots4[i].z[0] += fpTall * (temp[0][0] - plots4[i].z[0]);
        plots4[i].z[1] += fpTall * (temp[0][1] - plots4[i].z[1]);
        plots4[i].z[2] += fpTall * (temp[0][2] - plots4[i].z[2]);
        plots4[i].z[3] += fpTall * (temp[0][3] - plots4[i].z[3]);
        //
        pData += 8 * 4 * sizeof(float);
        //
        // assemble 'w' components - just set it to the '1'
        plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
    }
    // calculate bounding volume
    if (plots4.GetSize())
        pGPU->CalculateBoundingVolume(&ext.bv, plots4.GetData(), plots4.GetSize());
    //
    pGC->ChangeRangeGeometry(szFigureName, plots4.GetData(), hdr.dwNumVertices);
}
/////////////////////////////////////////////////////////////////////////////////////
// load figure data in old format
void CGeometryManager::LoadOldGeometryData(CGeometryContainer *pGC, const std::string &szFigureName,
                                           LPBYTE &pData, float fpStrength, float fpGrace,
                                           float fpTall)
{
    SRangeExternals &ext = pGC->GetRangeExternals(szFigureName);
    CXYZW4Array plots4;
    float valSmall;
    float valTall;
    float *smalls;
    float *talls;
    DWORD num;
    int i;
    // process material and texture type IDs
    pData += sizeof(ETypeIDs);
    pData += sizeof(ETypeIDs);
    // skip bounding volume
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(CPlot3D);
    pData += 6 * sizeof(float);
    // process plots4
    DWORD dwNumVertices = num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    plots4.SetSize(num);
    for (i = 0; i < int(num); i++)
    {
        // +0 <- weak
        // +4 <- strong
        // +8 <- fat
        // first - small components, then - tall components
        // processing x component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th x value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].x[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st x value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].x[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd x value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].x[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd x value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].x[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing y component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th y value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].y[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st y value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].y[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd y value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].y[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd y value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].y[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing z component (4)
        smalls = (float *)pData;
        talls = ((float *)pData) + 12;
        pData += 24 * sizeof(float);
        // 0th z value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].z[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st z value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].z[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd z value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].z[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd z value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].z[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing w component (4)
        plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
    }
    // calculate bounding volume
    if (plots4.GetSize())
        pGPU->CalculateBoundingVolume(&ext.bv, plots4.GetData(), plots4.GetSize());
    //
    pGC->ChangeRangeGeometry(szFigureName, plots4.GetData(), dwNumVertices);
}
/////////////////////////////////////////////////////////////////////////////////////
int CGeometryManager::GetUsedMemoryCount() const
{
    int nMemoryCount = 0;
    for (auto pos = geometries.begin(); pos != geometries.end(); ++pos)
        nMemoryCount += pos->second->GetMemoryCount();
    return nMemoryCount;
}
/////////////////////////////////////////////////////////////////////////////////////
CModelResourceEntry *CGeometryManager::GetModel(const std::string &szName)
{
    auto pos = models.find(szName);
    if (pos == models.end())
    {
        char buff[512];
        strcpy(buff, szName.c_str());
        strcat(buff, ".mod");
        BYTE *pModel = GetFile(buff);
        strcpy(buff, szName.c_str());
        strcat(buff, ".bon");
        BYTE *pBones = GetFile(buff);
        strcpy(buff, szName.c_str());
        strcat(buff, ".anm");
        BYTE *pAnims = GetFile(buff);
        if (pModel) // we need at rest a model to open resource (bones and animations are optional)
        {
            models[szName] = dbgnew CModelResourceEntry(this);
            models[szName]->OpenModelResources(pModel, pBones, pAnims);
            pos = models.find(szName);
        }
    }
    //
    if (pos != models.end())
        return pos->second;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGeometryManager::FreeUnusedResources()
{
    std::vector<std::string> toErase;
    for (auto pos = models.begin(); pos != models.end(); ++pos)
    {
        if (pos->second->GetRef() == 1)
            toErase.push_back(pos->first);
    }
    for (auto it = toErase.begin(); it != toErase.end(); ++it)
        models.erase(*it);
}
/////////////////////////////////////////////////////////////////////////////////////
