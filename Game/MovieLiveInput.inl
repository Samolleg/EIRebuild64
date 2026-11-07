// Host-thread movie input facts. The worker reads only the atomic presence bit;
// it never follows a Player, decoder or stack-frame pointer.
namespace {
struct LiveMovieInput {
    Movie* movie=nullptr;
    bool *done=nullptr,*interrupted=nullptr,*holding=nullptr;
    bool skipMovie=false,skipFrame=false;
};
LiveMovieInput* movieInput=nullptr;
std::atomic<bool> liveMovieInputPresent{false};
}
struct LiveMovieAccess {
    static LiveMovieState Read(Movie* movie,const LiveMovieInput* input) {
        LiveMovieState value;
        if(!movie || !movie->player)return value;
        const auto state=movie->player->GetState();
        value.active=input?!*input->done:(state==State::Playing || state==State::Paused);
        value.id=movie->liveSerial;value.name=movie->liveName.substr(0,512);
        value.loading=movie->presentation.loading;value.state=int(state);
        value.holding=input && *input->holding;
        value.skippable=value.active && input && (input->skipMovie || (value.holding && input->skipFrame));
        value.audio=movie->player->AudioSnapshot();value.audioSequence=movie->liveAudioSequence;
        return value;
    }
    static Movie* Next(Movie* movie) {return movie->next;}
    static bool Skip(LiveMovieInput& input) {
        if(*input.done || !input.movie || !input.movie->player ||
           (!input.skipMovie && (!*input.holding || !input.skipFrame)))return false;
        input.movie->player->Skip();
        *input.done=*input.interrupted=true;return true;
    }
};
namespace {
struct LiveMovieInputScope {
    LiveMovieInput value;
    LiveMovieInput* previous=movieInput;
    LiveMovieInputScope(Movie& movie,bool& done,bool& interrupted,bool& holding,bool skipMovie,bool skipFrame)
        :value{&movie,&done,&interrupted,&holding,skipMovie,skipFrame} {
        movieInput=&value;liveMovieInputPresent.store(true,std::memory_order_release);
    }
    ~LiveMovieInputScope() {
        movieInput=previous;liveMovieInputPresent.store(previous!=nullptr,std::memory_order_release);
    }
};
}
bool HasLiveMovieInput() noexcept {return liveMovieInputPresent.load(std::memory_order_acquire);}
LiveMovieState ReadLiveMovieState() {
    if(movieInput)return LiveMovieAccess::Read(movieInput->movie,movieInput);
    for(auto* movie=firstMovie;movie;movie=LiveMovieAccess::Next(movie)) {
        auto value=LiveMovieAccess::Read(movie,nullptr);if(value.active)return value;
    }
    return {};
}
bool SkipLiveMovieInput() {return movieInput && LiveMovieAccess::Skip(*movieInput);}
