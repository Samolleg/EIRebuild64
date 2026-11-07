// The mixer copies a bounded native snapshot once. JSON is emitted in slices
// using the observer's existing deadline; the pipe worker never reads audio.
json AudioEntry(const MiniaudioOneShotSfx::LiveAudioEntry& value,uint64_t now) {
    const char* source=value.video?"movie":value.music?"music":value.scenario?"scenario_voice":value.sourceId?"party_voice":value.spatial?"world_sound":"flat_sound";
    json result={{"id",value.id},{"sequence",value.sequence},{"file",Text(value.file,MAX_PATH)},
        {"name",value.music?Text(value.name,128):Text(value.file,128)},
        {"kind",value.video?"video":value.music?"music":value.speech?"speech":"sound"},
        {"source",{{"kind",source},{"unit_id",value.sourceId?json(value.sourceId):json(nullptr)}}},
        {"playing",value.playing},{"paused",value.paused},{"looping",value.looping},{"priority",value.priority},
        {"volume",{{"gain",value.gain},{"percent",value.gain*100.f},
            {"basis","mixer_gain_before_device_mute_and_builtin_distance_attenuation"}}},
        {"age_seconds",double(now>=value.started?now-value.started:0)/1000.0}};
    result["position"]=value.spatial?json::array({value.position[0],value.position[1],value.position[2]}):json(nullptr);
    return result;
}
bool AudioState(Job& job,Clock::time_point deadline) {
    const auto& params=job.request.at("params");const size_t limit=GetLimit(params,200,50);
    if(params.contains("seconds") && !params.at("seconds").is_number())throw std::runtime_error("seconds must be a number");
    const double seconds=params.value("seconds",5.0);
    if(!std::isfinite(seconds)||seconds<0||seconds>60)throw std::runtime_error("seconds must be between 0 and 60");
    if(!job.phase) {
        if(!job.audio)throw std::runtime_error("audio snapshot allocation missing");
        MiniaudioOneShotSfx::ReadLiveAudioSnapshot(*job.audio);job.phase=1;job.item=0;
        const auto& audio=*job.audio;
        job.response={{"ok",true},{"current",json::array()},{"recent",json::array()},
            {"seconds",seconds},{"limit",limit},{"engine_initialized",audio.engineInitialized},{"output_available",audio.outputAvailable},
            {"background_muted",AgentBackground::Enabled()},{"history_capacity",256},
            {"history_since_uptime_ms",audio.historySince},{"snapshot_uptime_ms",audio.now},
            {"events_overwritten",audio.overwritten},{"recent_basis","mixer_start_accepted; includes starts_held_by_pause"},
            {"current_truncated",false},{"recent_truncated",false},{"truncated",false}};
        const auto movie=Video::ReadLiveMovieState();
        if(movie.active && movie.audio.present) {
            auto video=AudioEntry(MiniaudioOneShotSfx::LiveAudioEntry{},audio.now);
            video["file"]=Text(movie.name.c_str(),512);video["name"]=video["file"];video["kind"]="video";
            video["source"]={{"kind","movie"},{"unit_id",nullptr}};video["sequence"]=movie.audioSequence;
            video["playing"]=movie.audio.playing;video["paused"]=!movie.audio.playing;video["looping"]=movie.loading;
            video["volume"]["gain"]=movie.audio.gain;video["volume"]["percent"]=movie.audio.gain*100.f;
            video["age_seconds"]=nullptr;job.response["current"].push_back(std::move(video));
        }
        if(audio.overwritten && audio.recentCount &&
           double(audio.now>=audio.recent[0].started?audio.now-audio.recent[0].started:0)<seconds*1000.0)job.response["recent_truncated"]=true;
    }
    const auto& audio=*job.audio;
    while(Clock::now()<deadline) {
        if(job.phase==1) {
            if(job.item>=audio.currentCount){job.phase=2;job.item=0;continue;}
            if(job.response["current"].size()>=limit){job.response["current_truncated"]=true;job.phase=2;job.item=0;continue;}
            job.response["current"].push_back(AudioEntry(audio.current[job.item++],audio.now));
        } else {
            if(job.item>=audio.recentCount) {
                job.response["truncated"]=job.response["current_truncated"].get<bool>()||job.response["recent_truncated"].get<bool>();return true;
            }
            const auto& value=audio.recent[audio.recentCount-1-job.item++];
            if(double(audio.now>=value.started?audio.now-value.started:0)>seconds*1000.0)continue;
            if(job.response["recent"].size()>=limit){job.response["recent_truncated"]=true;job.item=audio.recentCount;continue;}
            job.response["recent"].push_back(AudioEntry(value,audio.now));
        }
    }
    return false;
}
