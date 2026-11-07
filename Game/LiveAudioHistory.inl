// Mixer-owned, event-only bounded history. No frame polling, file logging,
// resource resolution or extra random choices are introduced for MCP.
LiveAudioEntry liveAudioHistory[256]{};
unsigned liveAudioNext=0,liveAudioCount=0;
uint64_t liveAudioSequence=0;
const uint64_t liveAudioHistorySince=GetTickCount64();
uint64_t liveMusicStarted=0,liveMusicSequence=0;

LiveAudioEntry LiveVoiceEntry(const SVoice& voice) noexcept {
    LiveAudioEntry value{};
    strncpy_s(value.file,voice.name,_TRUNCATE);
    value.id=voice.virtualManaged?voice.virtualId:voice.sequence;
    value.sequence=voice.liveSequence;value.priority=voice.priority;
    value.speech=voice.speech;value.dialog=voice.dialog;value.looping=voice.looping;
    value.spatial=voice.spatial||voice.partyVoiceEnabled;
    value.sourceId=voice.partyVoiceEnabled?voice.partyVoice.speakerId:0;
    value.scenario=voice.scenarioSpeech;
    value.started=GetTickCount64()-DWORD(GetTickCount()-voice.startTick);
    value.paused=voice.pausedByApp||voice.pausedByModalSpeech||voice.pausedByWorld;
    value.playing=voice.soundInitialized && ma_sound_is_playing(&voice.sound)!=MA_FALSE;
    if(voice.soundInitialized) {
        value.gain=ma_sound_get_volume(&voice.sound);
        if(value.spatial) {
            const auto position=ma_sound_get_position(&voice.sound);
            value.position[0]=position.x;value.position[1]=position.y;value.position[2]=position.z;
        }
    }
    return value;
}
LiveAudioEntry LiveMusicEntry() noexcept {
    LiveAudioEntry value{};value.music=true;value.looping=true;
    strncpy_s(value.file,Streaming.currentPath,_TRUNCATE);
    strncpy_s(value.name,Streaming.currentName,_TRUNCATE);
    value.started=liveMusicStarted;value.sequence=liveMusicSequence;
    value.paused=Streaming.paused||AppAudioSuspended;
    const unsigned slot=Streaming.activeSlot;
    if(slot<2 && Streaming.slotInitialized[slot]) {
        value.gain=ma_sound_get_volume(&StreamingSounds[slot]);
        value.playing=ma_sound_is_playing(&StreamingSounds[slot])!=MA_FALSE;
    }
    return value;
}
void RememberLiveAudio(LiveAudioEntry value) noexcept {
    value.sequence=++liveAudioSequence;
    liveAudioHistory[liveAudioNext]=value;liveAudioNext=(liveAudioNext+1)%256;
    liveAudioCount=(std::min)(liveAudioCount+1,256u);
}
void RememberLiveMusic() noexcept {
    liveMusicStarted=GetTickCount64();liveMusicSequence=liveAudioSequence+1;
    RememberLiveAudio(LiveMusicEntry());
}
