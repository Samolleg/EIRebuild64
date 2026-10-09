#include "ModTextureMemory.h"


Json CheckMemoryOutputAllocationG3(const std::string& directory) {
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    if(input.at("cases").size()!=3)throw std::runtime_error("Expected three output allocation representatives");
    SMMPFileHeader h;h.dwSignature=MMP_SIGNATURE;h.InitializeDefaultFormat8888();
    h.dwDimentionX=2;h.dwDimentionY=2;h.dwMipMapLevel=2;h.dwUserDataShift=3;
    Json cases=Json::array();
    auto failure=[&](bool throwing){
        ModTextureMemory::MemoryState state;ModTextureMemory::Payload oldPayload;oldPayload.words.push_back(7);oldPayload.bytes=4;
        state.cache.Put("retained",std::move(oldPayload));ModPrimaryAtlas::Aliases aliases;
        SecondaryAtlasCheckManager manager;CTexture old;old.SetManager(&manager);old.SetTypeID(TTID_UNIT_MAIN);old.SetName("jmvatpOOM");
        CTexture* owner=&old;owner->AddRef();int visible=1,attempts=0,layers=0,bind=0;ModTextureMemory::Payload output;
        const bool rebuilt=ModTextureMemory::EnsureCpu(state,"jmvatpOOM",[&](std::string&){
            if(!ModTextureMemory::ReserveOutput(output,h,3,visible,[&](std::vector<DWORD>&,std::size_t)->bool{
                ++attempts;if(throwing)throw std::bad_alloc();return false;
            }))return false;
            ++layers;return true;
        });
        if(rebuilt)ModPrimaryAtlas::EnsurePrivate(aliases,42,owner->GetName(),[](){return true;},[&](const char*){++bind;return true;});
        if(owner!=&old || state.cache.Get("retained")->words[0]!=7)throw std::runtime_error("Failed allocation changed retained owner/cache");
        Json row={{"result",rebuilt},{"visibility_after",visible},{"words_after",output.words.size()},{"bytes_after",output.bytes},
            {"output_attempts",attempts},{"layers",layers},{"cache_entries",state.cache.Size()},{"aliases",aliases.entries.size()},
            {"force_after",state.forceCanonicalCpu},{"owner_refs",owner->GetRefCount()},{"bind_calls",bind}};
        owner->Release();return row;
    };
    auto thrown=failure(true),nullFailure=failure(false);
    if(thrown!=nullFailure)throw std::runtime_error("Output bad_alloc and native NULL-equivalent differ");
    auto failed=thrown;failed.erase("result");failed["id"]="output_failure_only";
    failed["bad_alloc_result"]=thrown["result"];failed["null_result"]=nullFailure["result"];cases.push_back(failed);
    {
        ModTextureMemory::MemoryState state;ModPrimaryAtlas::Aliases aliases;int visible=1,attempts=0;ModTextureMemory::Payload output;
        const bool rebuilt=ModTextureMemory::EnsureCpu(state,"jmvatpOOM",[&](std::string&)->bool{
            try {
                if(!ModTextureMemory::ReserveOutput(output,h,3,visible,[&](std::vector<DWORD>& words,std::size_t count){++attempts;words.resize(count);return true;}))return false;
                throw std::bad_alloc(); // Deterministic unrelated temporary scratch failure.
            }catch(const std::bad_alloc&){return false;}
        });
        cases.push_back({{"id","unrelated_scratch_failure"},{"result",rebuilt},{"visibility_after",visible},{"output_attempts",attempts},
            {"output_words",output.words.size()},{"output_bytes",output.bytes},{"force_after",state.forceCanonicalCpu},
            {"cache_entries",state.cache.Size()},{"aliases",aliases.entries.size()}});
    }
    {
        int visible=1,attempts=0;ModTextureMemory::Payload output;
        const bool reserved=ModTextureMemory::ReserveOutput(output,h,3,visible,[&](std::vector<DWORD>& words,std::size_t count){++attempts;words.resize(count);return true;});
        auto* address=output.words.data();
        output=ModTextureMemory::EncodeInto(std::move(output),h,std::string("\x01\x00\xff",3),std::vector<DWORD>{1,2,3,4},
            [](WORD*,DWORD*,int){throw std::runtime_error("Unexpected16bit output");},
            [](DWORD*,DWORD*,int){throw std::runtime_error("UnexpectedMMX output");});
        cases.push_back({{"id","reserve_then_encode_reuses_buffer"},{"result",reserved},{"visibility_after",visible},{"output_attempts",attempts},
            {"output_words",output.words.size()},{"output_bytes",output.bytes},{"same_output_buffer",address==output.words.data()}});
    }
    return {{"success",true},{"cases",cases},{"productionOutputBoundary",true},{"realTextureRefs",true},{"simulatedFailures",true},
        {"realExhaustion",false},{"globalSettingsWritten",false},{"hostCreated",false},{"worldCreated",false},{"unitCreated",false},
        {"screenCreated",false},{"deviceCreated",false},{"gpuLoads",0},{"audioStarted",false},{"saveRead",false}};
}

Json CheckTextureMemoryG3(const std::string& directory) {
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    if(input.at("cases").size()!=4)throw std::runtime_error("Expected four memory atlas representatives");
    auto encode=[](const std::string& user,const std::vector<DWORD>& pixels){
        SMMPFileHeader h;h.dwSignature=MMP_SIGNATURE;h.InitializeDefaultFormat8888();
        h.dwDimentionX=2;h.dwDimentionY=2;h.dwMipMapLevel=2;h.dwUserDataShift=DWORD(user.size());
        return ModTextureMemory::Encode(h,user,pixels,
            [](WORD*,DWORD*,int){throw std::runtime_error("Unexpected 16bit conversion in 8888 literal");},
            [](DWORD*,DWORD*,int){throw std::runtime_error("Unexpected MMX mip in 8888 literal");});
    };
    const auto pixels=input.at("cases")[0].at("pixels").get<std::vector<DWORD>>();
    const std::string user("\x01\x00\xff",3);
    auto payload=encode(user,pixels);
    Json cases=Json::array();
    {
        std::string hex;const char* digits="0123456789abcdef";
        auto* bytes=reinterpret_cast<const BYTE*>(payload.words.data());
        for(std::size_t i=0;i<payload.bytes;++i){hex+=digits[bytes[i]>>4];hex+=digits[bytes[i]&15];}
        DWORD mip;std::memcpy(&mip,bytes+payload.bytes-sizeof(mip),sizeof(mip));
        cases.push_back({{"id","complete_mmp_and_exact_validation"},{"payload_hex",hex},{"length",payload.bytes},
            {"userdata_hex","0100ff"},{"pixels",pixels},{"mip",mip},
            {"exact",ModTextureMemory::Exact(&payload,2,2,MMP_FORMAT_8888,user)},
            {"wrong_mips",ModTextureMemory::Exact(&payload,2,1,MMP_FORMAT_8888,user)},
            {"wrong_userdata",ModTextureMemory::Exact(&payload,2,2,MMP_FORMAT_8888,std::string("\x02\x00\xff",3))}});
    }
    {
        ModTextureMemory::Cache cache;cache.Put("old",payload);
        // One bounded eviction/touch observation at the real capacity, no matrix.
        for(std::size_t i=0;i<ModTextureMemory::Cache::Capacity;++i)cache.Put("entry"+std::to_string(i),payload);
        if(cache.Get("old"))throw std::runtime_error("Oldest CPU payload was not evicted");
        cache.Get("entry1");cache.Put("new",payload);
        const bool touched=cache.Get("entry1")!=nullptr;
        if(cache.Get("entry0"))throw std::runtime_error("LRU touch/eviction order differs");
        cache.Put("entry1",encode("replacement",pixels));
        if(cache.Get("entry1")->Header()->dwUserDataShift!=11)throw std::runtime_error("CPU replacement failed");
        const auto size=cache.Size();cache.Clear();
        cases.push_back({{"id","owning_lru30_touch_replace_reset"},{"capacity",ModTextureMemory::Cache::Capacity},
            {"initial_evicted","old"},{"touched_survives",touched},{"untouched_evicted","entry0"},{"size",size},{"after_clear",cache.Size()}});
    }
    {
        ModTextureMemory::Cache cache;ModPrimaryAtlas::Aliases aliases;
        aliases.Add("jmvatmFixture","canonical");cache.Put("canonical",encode("canonical",pixels));
        cache.Put("jmvatmFixture",encode("direct",pixels));auto resource=encode("resource",pixels);
        std::string selected;
        auto load=[&](SMMPFileHeader* h,bool){selected.assign(static_cast<const char*>(h->GetUsedData()),h->dwUserDataShift);return true;};
        ModTextureMemory::LoadSource(cache,aliases,"jmvatmFixture",true,[&](){return resource.Header();},load);
        const auto res=selected;
        ModTextureMemory::LoadSource(cache,aliases,"jmvatmFixture",true,[]()->SMMPFileHeader*{return nullptr;},load);
        const auto direct=selected;cache.Remove("jmvatmFixture");
        ModTextureMemory::LoadSource(cache,aliases,"jmvatmFixture",true,[]()->SMMPFileHeader*{return nullptr;},load);
        const auto alias=selected;cache.Clear();
        const bool missingPrivate=ModTextureMemory::LoadSource(cache,aliases,"jmvatmFixture",true,[]()->SMMPFileHeader*{return nullptr;},load);
        const bool missingOrdinary=ModTextureMemory::LoadSource(cache,aliases,"ordinary",false,[]()->SMMPFileHeader*{return nullptr;},load);
        cases.push_back({{"id","res_direct_cache_then_private_alias"},{"resource_wins",res},{"direct_private_wins",direct},
            {"alias_source",alias},{"missing_private",missingPrivate},{"missing_ordinary",missingOrdinary},{"disk_accesses",0}});
    }
    {
        ModTextureMemory::MemoryState state;const auto canonical=input.at("cases")[3].at("canonical").get<std::string>();
        int calls=0;bool during=false;
        const bool exact=ModTextureMemory::EnsureCpu(state,canonical,[&](std::string& name){
            ++calls;during=state.forceCanonicalCpu;name=canonical;state.cache.Put(canonical,payload);return true;
        });
        const bool after=state.forceCanonicalCpu;int existingCalls=0;
        if(!ModTextureMemory::EnsureCpu(state,canonical,[&](std::string&){++existingCalls;return false;}))throw std::runtime_error("Existing CPU payload was not reused");
        state.cache.Clear();
        const bool mismatch=ModTextureMemory::EnsureCpu(state,canonical,[&](std::string& name){name="different";state.cache.Put(canonical,payload);return true;});
        state.cache.Clear();
        const bool exception=ModTextureMemory::EnsureCpu(state,canonical,[](std::string&)->bool{throw std::runtime_error("rebuild failure");});
        if(state.forceCanonicalCpu)throw std::runtime_error("Force flag not restored on failure");
        cases.push_back({{"id","force_cpu_before_private_alias"},{"canonical",canonical},{"regenerate_calls",calls},
            {"force_during",during},{"force_after",after},{"exact_rebuilt",exact},{"mismatch",mismatch},{"exception",exception},{"existing_cpu_calls",existingCalls}});
    }
    std::size_t count=0;const auto* rules=ModConfiguration::CombatRules(count);
    bool memoryRule=false,debugRule=false;
    for(std::size_t i=0;i<count;++i){
        if(std::strcmp(rules[i].key,"writeComposeInMemory")==0)memoryRule=!rules[i].fallback;
        if(std::strcmp(rules[i].key,"showJmvDebug")==0)debugRule=!rules[i].fallback;
    }
    if(!memoryRule || !debugRule)throw std::runtime_error("Existing native compositor defaults are not mapped");
    return {{"success",true},{"cases",cases},{"existingNativeDefaultsMapped",true},{"productionMemoryHelpers",true},
        {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},
        {"gpuLoads",0},{"audioStarted",false},{"saveRead",false}};
}
