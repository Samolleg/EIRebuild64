// Service entry into the *loaded* scenario's own compiled thread, never a copy
// of its combat logic. Called only by LiveObserver after --background checks.
bool CScript::McpArenaThread(const char* name,const char* marker,float value,bool tail,bool validateOnly)
{
    if(!AgentBackground::Enabled())throw std::runtime_error("arena thread requires --background");
    auto* authored=GetScriptByName(name);
    if(!authored||authored->conditions.GetSize()!=1||!authored->conditions[0].pActions)
        throw std::runtime_error("loaded arena script does not have the expected entry thread");
    auto* thread=authored->conditions[0].pActions;
    int start=0,found=-1,lastWait=-1;
    for(int i=0;i<thread->commands.GetSize();++i) {
        const auto& command=thread->commands[i];
        if(command.command==SC_SLEEP)lastWait=i;
        if(marker&&command.command==SC_GSSETVAR&&command.szParams.GetSize()==1&&
           command.szParams[0]==marker&&command.params.GetSize()==2&&
           command.params[0]==0&&command.params[1]==value&&command.funcParameters.GetSize()==0) {
            found=i;break;
        }
    }
    if(marker&&found<0)throw std::runtime_error("loaded arena stage marker differs; nothing queued");
    if(tail)start=(marker&&value==3)?found:lastWait>=0?lastWait+1:found;
    else if(thread->commands.GetSize()&&thread->commands[0].command==SC_KILLSCRIPT)start=1;
    if(start<0||start>=thread->commands.GetSize())throw std::runtime_error("invalid arena thread entry");
    if(validateOnly)return true;
    CScriptThreadState state;state.RunThread(thread,&authored->vars);state.nCurCommand=start;
    // Queue, rather than running an unbounded scenario/voice loop in a MCP frame.
    runningThreads.AddHead(state);return true;
}
