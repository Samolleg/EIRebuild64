// These methods run on the source render owner. Readback never loads a sector,
// advances tile animation, clears modified flags or changes simulation state.
HRESULT CTerrain::CopyRendererMember(const char* name,void* destination,uint32_t capacity,uint32_t* size)
{
    if(!size||!name)return E_INVALIDARG;*size=0;
    DWORD bytes=0;auto* data=mapRes.GetFile(name,&bytes);
    if(!data)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *size=bytes;
    const HRESULT hr=!destination?S_OK:capacity<bytes?HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER):S_OK;
    if(destination&&SUCCEEDED(hr)){
        memcpy(destination,data,bytes);
        POSITION pos=sectorLRU.GetHeadPosition();
        while(pos){CSector* sector=sectorLRU.GetNext(pos);char sectorName[256];
            GetSectorName(sector->GetMapX(),sector->GetMapY(),const_cast<char*>((LPCSTR)szBaseName),sectorName);
            if(!strcmp(name,sectorName) && sector->IsHaveState(CSector::ESS_GEOMETRY) && sector->IsModifid()){
                sector->CopyRendererSector(destination);break;
            }
        }
    }
    mapRes.CloseFile(data);return hr;
}

HRESULT CTerrain::CopyRendererBaseVertices(uint32_t index,float* rows,uint32_t count)
{
    if(!rows||count!=1089*6||index>=uint32_t(GetSectorsNumber()))return E_INVALIDARG;
    POSITION pos=sectorLRU.GetHeadPosition();
    while(pos){CSector* sector=sectorLRU.GetNext(pos);
        if(uint32_t(sector->GetMapY()*nXSectors+sector->GetMapX())!=index)continue;
        if(!sector->IsHaveState(CSector::ESS_D3D))return S_FALSE;
        const CPlot3D* vertices=sector->GetMeshPtr();const SLVertex3D* normals=sector->GetLVertexPtr();
        for(unsigned v=0;v<1089;++v){rows[v*6]=vertices[v].x;rows[v*6+1]=vertices[v].y;rows[v*6+2]=vertices[v].z;
            rows[v*6+3]=normals[v].vNorm.x;rows[v*6+4]=normals[v].vNorm.y;rows[v*6+5]=normals[v].vNorm.z;}
        return S_OK;
    }
    return S_FALSE;
}

void CTerrain::ReleaseOwnedTerrain(CRenderer* renderer)
{
    if(renderer && renderer->GetD3D())renderer->GetD3D()->GetOwnedRenderer().ReleaseTerrain();
}

bool CTerrain::TryOwnedTerrain(CRenderer* renderer)
{
    auto* dx=renderer->GetD3D();unsigned width=0,height=0;
    if(!dx||!dx->IsOwnedWorldActive()||dx->GetTerrainShadowSize(width,height))return false;
    const size_t count=size_t(GetSectorsNumber());
    if(rendererSectorRows.size()!=count){rendererSectorRows.resize(count);rendererSectorPhases.resize(count);}
    std::vector<EIRenderTerrainSectorV5> rows;rows.reserve(count);
    // These sectors were just loaded, clipped and assigned LOD by Draw.
    // Retain every resident row for release decisions; invisible rows simply
    // contribute no color command. Unloaded geometry stays in one shared map.
    POSITION pos=sectorLRU.GetHeadPosition();
    while(pos){CSector* sector=sectorLRU.GetNext(pos);
        if(!sector->IsHaveState(CSector::ESS_LIGHTED))continue;
        const uint32_t index=uint32_t(sector->GetMapY()*nXSectors+sector->GetMapX());
        auto& row=rendererSectorRows[index];row.sector=index;
        // Draw's source domain is authoritative even when a cached sector's
        // clip bits are from an older frame.
        const int y=sector->GetMapY()-nClipMinSectorY,x=sector->GetMapX();
        const bool visible=y>=0&&y<viewableSectorsMinX.GetSize()&&x>=viewableSectorsMinX[y]&&x<=viewableSectorsMaxX[y];
        if(visible)sector->SetBorderLODsFlags();
        sector->CopyRendererState(row,rendererSectorPhases[index]);
        if(!visible)row.subparts=0;
        rows.push_back(row);
    }
    EIRenderTerrainFrameV5 packet{};packet.source_revision=rendererRevision;
    const SHMatrix& world=renderer->GetGPU()->GetMatrix(MAT_STACK_WORLDSPACE);
    const SHMatrix& clip=renderer->GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    memcpy(packet.object_to_world,&world,sizeof(world));memcpy(packet.object_to_clip,&clip,sizeof(clip));
    packet.camera[0]=theCamera.GetCP().x;packet.camera[1]=theCamera.GetCP().y;packet.camera[2]=theCamera.GetCP().z;
    packet.lod_distances[0]=theSettings.fpTerrainLODs[0];packet.lod_distances[1]=theSettings.fpTerrainLODs[1];
    packet.maximum_lod=nMaxLOD;packet.render_state=renderState;
    packet.wind[0]=windDirection.x;packet.wind[1]=windDirection.y;
    packet.wind_force=fpWindForce;packet.wind_normalized=fpWindForce01;packet.wind_phase=fpWindPhase;
    packet.precise_time=float(theWorld.GetSegmentTimeCounter())/16.f;
    packet.sectors=rows.data();packet.sector_count=uint32_t(rows.size());
    return dx->SubmitOwnedTerrain(packet);
}
