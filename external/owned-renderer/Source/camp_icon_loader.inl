using CampIconData = PreparedCampIcon;

// The admitted addon allocator gives geometry-facing icon families different
// canvas sizes. Keep those sizes and bottom alignment; never resample an icon.
unsigned camp_icon_canvas(const std::string& name, unsigned variant) noexcept {
    if (!variant) return 256;
    const auto begins = [&](const char* prefix) { return name.compare(0,std::strlen(prefix),prefix)==0; };
    if (begins("unhuma")) return 1024;
    for (const auto* prefix : {"spell","modifier","material","qitem","litem","quitem","prototype00"})
        if (begins(prefix)) return 2048;
    for (const auto* prefix : {"pl","lg","sh","pt","gl","bt"})
        if (begins(prefix)) return 1024;
    return variant == 4 ? 2048 : 1024;
}

class CampIconLoader {
    struct Entry {
        std::shared_ptr<const CampIconData> data;
        std::string failure;
        bool reported = false, pending = true;
    };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, Entry> entries_;
    std::deque<std::string> pending_;
    std::array<std::vector<std::string>,4> visible_; // Grids plus synchronous preview/quick requests.
    std::array<std::deque<std::string>,3> selected_;
    unsigned visible_lane_ = 0, selected_lane_ = 0;
    std::array<std::thread,2> workers_;
    bool stopping_ = false;

    void run(ResourceStoreConfig mounts, bool high_resolution, ComPtr<ID3D11Device> device) noexcept {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        ResourceStore store;
        ResourceFailure opening;
        bool opened = false;
        try { opened = store.open(mounts, opening); }
        catch (...) { opening.message = "owned icon resource scope allocation failed"; }
        for (;;) {
            std::string name;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
                if (stopping_) return;
                // Service both visible sides together; no lane drains ahead
                // of another. Running tasks contain immutable names only.
                for (unsigned step=0;step<visible_.size() && name.empty();++step) {
                    const unsigned lane=(visible_lane_+step)%visible_.size();
                    for (const auto& candidate : visible_[lane])
                        if (entries_.at(candidate).pending) { name=candidate; break; }
                    if (!name.empty()) visible_lane_=(lane+1)%visible_.size();
                }
                for (unsigned step=0;step<selected_.size() && name.empty();++step) {
                    const unsigned lane=(selected_lane_+step)%selected_.size();
                    auto& tab=selected_[lane];
                    while (!tab.empty()) {
                        auto candidate=std::move(tab.front()); tab.pop_front();
                        if (entries_.at(candidate).pending) { name=std::move(candidate); break; }
                    }
                    if (!name.empty()) selected_lane_=(lane+1)%selected_.size();
                }
                while (name.empty() && !pending_.empty()) {
                    auto candidate = std::move(pending_.front()); pending_.pop_front();
                    if (entries_.at(candidate).pending) name = std::move(candidate);
                }
                if (name.empty()) continue;
                entries_.at(name).pending = false;
            }
            std::shared_ptr<const CampIconData> data;
            std::string error;
            try {
                ResourceBlob blob; TextureCpuAsset texture; ResourceFailure why;
                unsigned variant = 0;
                bool resolved = false;
                if (opened && high_resolution) {
                    resolved = store.resolve(name + ".x4.mmp",blob,why);
                    if (resolved) variant = 4;
                    else {
                        resolved = store.resolve(name + ".x2.mmp",blob,why);
                        if (resolved) variant = 2;
                    }
                }
                if (opened && !resolved) resolved = store.resolve(name + ".mmp",blob,why);
                const unsigned canvas = camp_icon_canvas(name,variant);
                if (!opened) error = opening.message;
                else if (!resolved || !parse_texture_asset(blob,texture,why)) error = why.message;
                else if (texture.format != MmpFormat::argb8888 || texture.bit_count != 32 ||
                         texture.width > canvas || texture.height > canvas)
                    error = "icon does not match the admitted BGRA/PNT3 canvas contract";
                else {
                    auto result = std::make_shared<CampIconData>();
                    auto& description=result->description;
                    description.width=description.height=canvas; description.pitch=canvas*2;
                    description.format.dwSize=sizeof(DDPIXELFORMAT);
                    description.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
                    description.format.dwRGBBitCount=16; description.format.dwRBitMask=0x7c00;
                    description.format.dwGBitMask=0x03e0; description.format.dwBBitMask=0x001f;
                    description.format.dwRGBAlphaBitMask=0x8000;
                    SurfaceImage image;
                    image.width=image.height=canvas; image.pitch=canvas*4;
                    image.format.dwSize=sizeof(DDPIXELFORMAT);
                    image.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
                    image.format.dwRGBBitCount=32; image.format.dwRBitMask=0x00ff0000;
                    image.format.dwGBitMask=0x0000ff00; image.format.dwBBitMask=0x000000ff;
                    image.format.dwRGBAlphaBitMask=0xff000000;
                    image.pixels.resize(std::size_t(image.pitch)*canvas);
                    auto cut=std::make_shared<std::vector<std::uint8_t>>(
                        (std::size_t(canvas)*canvas+7)/8,std::uint8_t{0xff});
                    bool opaque=texture.width==canvas && texture.height==canvas;
                    const auto* source=texture.source->data()+texture.mips[0].byte_offset;
                    for (unsigned y=0;y<texture.height;++y)
                        for (unsigned x=0;x<texture.width;++x) {
                            std::uint32_t colour;
                            std::memcpy(&colour,source+(y*texture.width+x)*4,4);
                            const std::uint32_t alpha=(colour&0x80000000u)?0xff000000u:0;
                            // Exactly the native 1555 quantization followed by
                            // the normal upload's value*255/31 expansion.
                            const std::uint32_t expanded=alpha|
                                ((((colour>>19)&31u)*255u/31u)<<16)|
                                ((((colour>>11)&31u)*255u/31u)<<8)|
                                (((colour>>3)&31u)*255u/31u);
                            const std::size_t bit=std::size_t(canvas-texture.height+y)*canvas+x;
                            std::memcpy(image.pixels.data()+bit*4,&expanded,4);
                            if (alpha) (*cut)[bit/8]&=std::uint8_t(~(1u<<(bit%8)));
                            else opaque=false;
                        }
                    description.alpha_fully_opaque=opaque;
                    if (!opaque) description.alpha_cut_mask=std::move(cut);
                    if (FAILED(create_completed_surface_texture(device.Get(),image,result->texture,result->srv)))
                        error="owned icon GPU creation failed";
                    if (error.empty()) data = std::move(result);
                }
            } catch (...) { error = "owned icon decode allocation failed"; }
            std::lock_guard lock(mutex_);
            auto& entry = entries_.at(name);
            entry.data = std::move(data); entry.failure = std::move(error);
        }
    }
public:
    std::shared_ptr<const CampIconData> request(const std::string& name, bool visible = false) {
        if (!workers_[0].joinable()) {
            ResourceStoreConfig mounts;
            if (!bridge_camp_texture_mounts(mounts)) {
                reject("icon_texture_manager_mount_contract_unavailable");
                return {};
            }
            if (!icon_device) return {};
            ComPtr<ID3D11Device> device = icon_device;
            for (auto& worker : workers_)
                worker=std::thread([this,mounts,high_resolution=addon_icon_lookup,device]() mutable {
                    run(std::move(mounts),high_resolution,std::move(device));
                });
        }
        std::lock_guard lock(mutex_);
        auto [where, inserted] = entries_.try_emplace(name);
        if (inserted) { pending_.push_back(name); wake_.notify_all(); }
        auto& entry = where->second;
        if (visible && entry.pending && std::find(visible_[3].begin(),visible_[3].end(),name)==visible_[3].end())
            visible_[3].push_back(name);
        if (!entry.failure.empty() && !entry.reported) {
            entry.reported = true;
            const std::string detail = name + ": " + entry.failure;
            #ifdef EID3D11_EMBEDDED
            bridge_report_event("camp_icon_omitted", detail.c_str());
#else
            bridge_append_probe_event("camp_icon_omitted", detail.c_str());
#endif
        }
        return entry.data;
    }
    void begin_priority_frame(const std::array<bool,3>& reset) {
        std::lock_guard lock(mutex_);
        for (auto& lane : visible_) lane.clear();
        for (unsigned lane=0;lane<selected_.size();++lane) if (reset[lane]) selected_[lane].clear();
    }
    void prioritize(unsigned lane, const std::vector<std::string>& visible,
        const std::vector<std::string>& selected) {
        for (const auto& name : visible) request(name);
        for (const auto& name : selected) request(name);
        std::lock_guard lock(mutex_);
        visible_[lane]=visible;
        auto& tab = selected_[lane];
        for (const auto& name : selected) if (entries_.at(name).pending &&
            std::find(tab.begin(),tab.end(),name)==tab.end()) tab.push_back(name);
        wake_.notify_all();
    }
    void shutdown() noexcept {
        { std::lock_guard lock(mutex_); stopping_ = true; wake_.notify_all(); }
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        entries_.clear(); pending_.clear();
        for (auto& lane : visible_) lane.clear();
        visible_lane_=selected_lane_=0;
        for (auto& tab : selected_) tab.clear();
        stopping_ = false;
    }
    ~CampIconLoader() { shutdown(); }
} camp_icon_loader;
