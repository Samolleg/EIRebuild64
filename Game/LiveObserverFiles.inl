// Only immutable directory paths cross from the game thread to the pipe worker.
// All enumeration/deletion is off-frame. No recursive removal, glob or shell.
bool SafeFileStem(const std::string& name,size_t maximum=128) {
    return !name.empty()&&name.size()<=maximum&&std::all_of(name.begin(),name.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
    });
}
bool McpSlot(const std::string& name) {return name.size()>4&&name.rfind("mcp_",0)==0&&SafeFileStem(name);}
struct LockedLivePath {
    std::vector<HANDLE> handles;
    bool missing=false;
    explicit LockedLivePath(const std::filesystem::path& input,bool deleting=false,bool lockedParent=false) {
        try {
            const auto path=input.lexically_normal();
            if(!path.is_absolute()||path.root_name().wstring().size()!=2)
                throw std::runtime_error("live file operations require a local absolute drive path");
            auto current=path.root_path();std::vector<std::filesystem::path> parts{current};
            for(const auto& part:path.relative_path()) {
                if(part==L"."||part==L"..")throw std::runtime_error("invalid live directory component");
                current/=part;parts.push_back(current);
            }
            // The caller still owns the entire parent chain. Do not reopen a
            // parent carrying DELETE access without FILE_SHARE_DELETE.
            if(lockedParent)parts={path};
            for(size_t i=0;i<parts.size();++i) {
                const DWORD access=FILE_READ_ATTRIBUTES+((deleting&&i+1==parts.size())?DELETE:0);
                const HANDLE file=CreateFileW(parts[i].c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                    OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
                if(file==INVALID_HANDLE_VALUE) {
                    const DWORD code=GetLastError();
                    if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND){missing=true;return;}
                    throw std::runtime_error("cannot lock live path; access denied or file is in use");
                }
                handles.push_back(file);BY_HANDLE_FILE_INFORMATION info{};
                if(!GetFileInformationByHandle(file,&info))throw std::runtime_error("cannot inspect live path handle");
                if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
                    throw std::runtime_error("live path contains a reparse point; refused");
                if(i+1<parts.size()&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
                    throw std::runtime_error("live path ancestor is not a directory");
            }
        } catch(...) {Close();throw;}
    }
    LockedLivePath(const LockedLivePath&)=delete;
    void Close() noexcept {for(auto i=handles.rbegin();i!=handles.rend();++i)CloseHandle(*i);handles.clear();}
    ~LockedLivePath(){Close();}
    bool Directory() const {
        BY_HANDLE_FILE_INFORMATION info{};
        if(missing||handles.empty()||!GetFileInformationByHandle(handles.back(),&info))
            throw std::runtime_error("cannot inspect live target");
        return (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    }
    void Remove() {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(missing||handles.empty()||!SetFileInformationByHandle(handles.back(),FileDispositionInfo,&disposition,sizeof(disposition)))
            throw std::runtime_error("live target could not be deleted; it may be in use or nonempty");
    }
};
bool PrepareFileJob(Job& job) {
    if(job.method=="save_list"||job.method=="save_delete") {
        if(!ModRuntime::Current())throw std::runtime_error("save operations require a loaded mod");
        if(job.method=="save_delete") {
            const auto& name=job.request.at("params").at("name");
            if(!name.is_string()||!McpSlot(name.get<std::string>()))
                throw std::runtime_error("save_delete only accepts exact mcp_ slot names; current and save* are protected");
        }
        job.fileRoot=std::filesystem::path((LPCSTR)SaveSchema::Slot::Directory());
    } else if(job.method=="screenshot_clear") {
        const auto& params=job.request.at("params");
        if(params.contains("prefix")&&(!params["prefix"].is_string()||
           (!params["prefix"].get<std::string>().empty()&&!SafeFileStem(params["prefix"].get<std::string>(),64))))
            throw std::runtime_error("prefix must be literal ASCII letters, digits, underscores or hyphens, at most 64");
        job.fileRoot=RuntimePaths64::Root()/L"Logs64"/L"live"/L"named";
    } else return false;
    job.response={{"ok",true},{"status","file_io_pending"},{"truncated",false}};return true;
}
json CompleteFileJob(Job& job) {
    namespace fs=std::filesystem;
    const auto& params=job.request.at("params");
    LockedLivePath root(job.fileRoot);
    if(!root.missing&&!root.Directory())throw std::runtime_error("live root is not a directory");
    if(job.method=="save_delete") {
        const auto name=params.at("name").get<std::string>();
        if(!McpSlot(name))throw std::runtime_error("protected save slot");
        const auto path=job.fileRoot/name;LockedLivePath slot(path,true,true);
        if(root.missing||slot.missing)return {{"ok",false},{"error","slot_not_found"},{"name",name},{"truncated",false}};
        if(!slot.Directory())throw std::runtime_error("slot must be a directory");
        // Saves64 has exactly one envelope. Unexpected files/subdirectories are
        // left for the owner, rather than recursively traversing an arbitrary tree.
        for(const auto& entry:fs::directory_iterator(path))
            if(entry.path().filename()!=L"slot.sav")throw std::runtime_error("slot contains unexpected entries; nothing deleted");
        {
            LockedLivePath envelope(path/L"slot.sav",true,true);
            if(!envelope.missing){if(envelope.Directory())throw std::runtime_error("slot.sav is not a regular file");envelope.Remove();}
        }
        slot.Remove();
        return {{"ok",true},{"status","deleted"},{"name",name},{"truncated",false}};
    }
    json rows=json::array();bool truncated=false;size_t deleted=0,skipped=0;
    const auto prefix=params.value("prefix",std::string());
    if(!root.missing)for(const auto& entry:fs::directory_iterator(job.fileRoot)) {
        const auto name=Utf8(entry.path().filename().c_str());
        if(job.method=="save_list") {
            if(rows.size()==200){truncated=true;break;}
            const DWORD attributes=GetFileAttributesW(entry.path().c_str());
            const bool link=attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
            const bool directory=attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            rows.push_back({{"name",name},{"directory",directory},{"reparse_point",link},
                            {"deletable",McpSlot(name)&&directory&&!link}});
        } else {
            const auto stem=Utf8(entry.path().stem().c_str());
            if(entry.path().extension()!=L".png"||!SafeFileStem(stem,64)||stem.rfind(prefix,0)!=0)continue;
            if(deleted+skipped==200){truncated=true;break;}
            const DWORD attributes=GetFileAttributesW(entry.path().c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {++skipped;continue;}
            LockedLivePath file(entry.path(),true,true);
            if(file.missing)continue;
            if(file.Directory())throw std::runtime_error("named screenshot changed to a directory");
            file.Remove();++deleted;
        }
    }
    if(job.method=="save_list")return {{"ok",true},{"slots",std::move(rows)},{"limit",200},{"truncated",truncated}};
    return {{"ok",true},{"status","done"},{"prefix",prefix},{"deleted",deleted},{"skipped_unsafe",skipped},{"truncated",truncated}};
}
