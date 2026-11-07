// A synchronous movie owns the host thread, possibly in the middle of load or
// another MCP callback. Use a separate job; never re-enter Step/replace active
// or inspect a partially constructed world from this nested message loop.
std::shared_ptr<Job> movieJob;
json MovieJson(const Video::LiveMovieState& value) {
    return {{"active",value.active},{"id",value.id},{"file",Text(value.name.c_str(),512)},
        {"skippable",value.skippable},{"holding",value.holding},{"loading",value.loading},
        {"player_state",value.state},{"has_audio",value.audio.present}};
}
json MovieUi(const Video::LiveMovieState& value) {
    return {{"ok",true},{"screen","movie"},{"kind","movie"},{"mode",value.holding?"holding":"playing"},
        {"screen_id",value.id},{"modal",true},{"waiting_for_input",value.holding},{"transition_pending",false},
        {"cutscene",true},{"cutscene_skippable",value.skippable},{"movie",MovieJson(value)},
        {"width",theInterface.GetWidth()},{"height",theInterface.GetHeight()},{"coordinate_space","native_canvas_pixels"},
        {"buttons",json::array()},{"windows",json::array({{{"id",value.id},{"screen","movie"},{"kind","movie"},
            {"title",Text(value.name.c_str(),512)},{"modal",true},{"active",true}}})},
        {"windows_truncated",false},{"truncated",false}};
}
bool MovieStep(Job& job,Clock::time_point deadline) {
    if(IsCommand(job.method))saveIsolated=true;
    if(job.method=="time_scale")return TimeScale(job);
    if(job.method=="audio_state")return AudioState(job,deadline);
    if(job.method=="quit") {
        if(!NGame::CanQuit())throw std::runtime_error("game refused to quit");
        saveIsolated=true;job.quitRequested=true;
        job.response={{"ok",true},{"status","accepted"},{"truncated",false}};return true;
    }
    const auto movie=Video::ReadLiveMovieState();
    if(!Video::HasLiveMovieInput() || !movie.active) {
        job.response=Error("movie_ended_before_request; command_not_retried");return true;
    }
    if(job.method=="ui_state"){job.response=MovieUi(movie);job.response["limit"]=GetLimit(job.request.at("params"),200,100);return true;}
    if(job.method=="status") {
        job.response={{"ok",true},{"pid",GetCurrentProcessId()},{"status_scope","movie_only"},
            {"main_thread_responding",true},{"map_loaded",nullptr},{"god",nullptr},{"screen","movie"},
            {"cutscene",true},{"window_waiting",movie.holding},{"ui",MovieUi(movie)},{"movie",MovieJson(movie)},
            {"squad_order_blockers",json::array({{{"reason","movie_playback"}}})},
            {"operation",OperationJson()},{"background_agent",AgentBackground::Enabled()},
            {"time_scale",1},{"time_scale_supported_max",1},{"camera",nullptr},{"camera_reason","movie_playback"},{"truncated",false}};
        return true;
    }
    const auto& params=job.request.at("params");
    if(job.method!="cutscene_skip" && job.method!="ui_key" && job.method!="ui_click") {
        job.response=Error("movie_playback_active; only status, ui_state, audio_state, ui_key, ui_click, cutscene_skip, time_scale and quit are safe here");return true;
    }
    saveIsolated=true;
    if(job.method=="ui_key")UiKey(params.at("key")); // same supported keyboard vocabulary
    if(job.method=="ui_click") {
        if(params.contains("id"))throw std::runtime_error("movie_has_no_button_ids; use cutscene_skip or x,y");
        for(const char* key:{"x","y"}) {
            const auto& value=params.at(key);const int bound=*key=='x'?theInterface.GetWidth():theInterface.GetHeight();
            if(!value.is_number_integer() || value<0 || value>=bound)
                throw std::runtime_error("UI coordinates must be integer native-canvas pixels");
        }
    }
    GameJournal::Source source("mcp");
    if(!Video::SkipLiveMovieInput())throw std::runtime_error("movie_has_no_skip_at_this_stage");
    job.response={{"ok",true},{"status","done"},{"path","Movie_player_skip_input"},{"movie",MovieJson(movie)},{"truncated",false}};
    return true;
}
void CancelMovieJob() noexcept {
    if(!movieJob)return;
    movieJob->cancelled=true;CleanupCapture(*movieJob);movieJob->ready.notify_one();movieJob.reset();
}
bool PumpMovieJob(bool acceptNew) {
    if(!movieJob && acceptNew) {
        std::unique_lock<std::mutex> lock(queueMutex,std::try_to_lock);
        if(lock.owns_lock() && queued)movieJob=std::move(queued);
    }
    if(!movieJob)return false;
    if(movieJob->cancelled){CancelMovieJob();return true;}
    if(movieJob->readyToFinish) {
        if(movieJob->quitRequested && movieJob->replySent.load(std::memory_order_acquire)) {
            if(!NGame::QuitProgram(0))ChannelError("movie_quit_refused",0);
            movieJob.reset();return true;
        }
        if(Finish(*movieJob) && !movieJob->quitRequested)movieJob.reset();return true;
    }
    const auto started=Clock::now();bool done=false;
    try {done=MovieStep(*movieJob,started+FrameBudget);}
    catch(const std::exception& error){CleanupCapture(*movieJob);movieJob->response=Error(error.what());done=true;}
    catch(...){CleanupCapture(*movieJob);movieJob->response=Error("movie request failed");done=true;}
    if(Clock::now()-started>std::chrono::milliseconds(1) && !IsCommand(movieJob->method))movieJob->budgetExceeded=true;
    if(movieJob->cancelled){CancelMovieJob();return true;}
    if(done) {
        if(movieJob->budgetExceeded)movieJob->response["budget_exceeded"]=true;
        movieJob->readyToFinish=true;
        if(Finish(*movieJob) && !movieJob->quitRequested)movieJob.reset();
    }
    return true;
}
