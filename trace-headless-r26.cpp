#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <string>
#pragma comment(lib,"dbghelp.lib")
int wmain(int argc,wchar_t**argv){
 if(argc!=5)return 2;
 STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={};
 std::wstring cmd=L"\""+std::wstring(argv[1])+L"\" --inspect-addon \""+argv[3]+L"\" \""+argv[4]+L"\"";
 if(!CreateProcessW(argv[1],cmd.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,argv[2],&si,&pi)){printf("CreateProcess %lu\n",GetLastError());return 3;}
 SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);
 SymInitialize(pi.hProcess,"out\\Win32\\Release",FALSE);
 DEBUG_EVENT e={};
 const ULONGLONG deadline=GetTickCount64()+40000;
 while(GetTickCount64()<deadline){
  if(!WaitForDebugEvent(&e,1000))continue;
  DWORD status=DBG_CONTINUE;
  if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){auto&d=e.u.CreateProcessInfo;printf("Main base %p\n",d.lpBaseOfImage);auto loaded=SymLoadModuleExW(pi.hProcess,d.hFile,argv[1],nullptr,(DWORD64)d.lpBaseOfImage,0,nullptr,0);printf("Symbol load %llx error %lu\n",loaded,GetLastError());CloseHandle(d.hFile);}
  if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){auto&d=e.u.LoadDll;SymLoadModuleEx(pi.hProcess,d.hFile,nullptr,nullptr,(DWORD64)d.lpBaseOfDll,0,nullptr,0);if(d.hFile)CloseHandle(d.hFile);}
  if(e.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){
   auto&d=e.u.Exception;DWORD code=d.ExceptionRecord.ExceptionCode;
   printf("Exception %08lx first=%lu at=%p param0=%lu\n",code,d.dwFirstChance,d.ExceptionRecord.ExceptionAddress,d.ExceptionRecord.ExceptionInformation[0]);
   if(code==EXCEPTION_BREAKPOINT){struct Info{ULONG reserved;void*peb;void*reserved2[2];ULONG_PTR pid;void*reserved3;};Info info={};auto query=(LONG(WINAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*))GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess");if(query(pi.hProcess,0,&info,sizeof(info),nullptr)>=0){BYTE value=0;WriteProcessMemory(pi.hProcess,(BYTE*)info.peb+2,&value,1,nullptr);puts("Diagnostic: IsDebuggerPresent cleared to retain ordinary shader path");}}
   if(code==0xe06d7363){DWORD object[8]={};ReadProcessMemory(pi.hProcess,(void*)d.ExceptionRecord.ExceptionInformation[1],object,sizeof(object),nullptr);for(auto pointer:object){char message[256]={};if(ReadProcessMemory(pi.hProcess,(void*)pointer,message,255,nullptr)&&message[0]>=32&&message[0]<127)printf(" object string: %.255s\n",message);}}
   if(code!=EXCEPTION_BREAKPOINT){
    HANDLE t=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.dwThreadId);CONTEXT c={};c.ContextFlags=CONTEXT_FULL;GetThreadContext(t,&c);
    STACKFRAME64 s={};s.AddrPC.Offset=c.Eip;s.AddrStack.Offset=c.Esp;s.AddrFrame.Offset=c.Ebp;s.AddrPC.Mode=s.AddrStack.Mode=s.AddrFrame.Mode=AddrModeFlat;
    for(int i=0;i<28;i++){
     char buffer[sizeof(SYMBOL_INFO)+1024]={};auto*sym=(SYMBOL_INFO*)buffer;sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=1023;DWORD64 delta=0;IMAGEHLP_LINE64 line={sizeof(line)};DWORD off=0;
     printf(" %08llx",s.AddrPC.Offset);if(SymFromAddr(pi.hProcess,s.AddrPC.Offset,&delta,sym))printf(" %s+%llu",sym->Name,delta);else printf(" symerr=%lu",GetLastError());if(SymGetLineFromAddr64(pi.hProcess,s.AddrPC.Offset,&off,&line))printf(" %s:%lu",line.FileName,line.LineNumber);puts("");
     if(!StackWalk64(IMAGE_FILE_MACHINE_I386,pi.hProcess,t,&s,&c,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }CloseHandle(t);status=DBG_EXCEPTION_NOT_HANDLED;
   }
  }
  fflush(stdout);ContinueDebugEvent(e.dwProcessId,e.dwThreadId,status);
  if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){printf("Exit %08lx\n",e.u.ExitProcess.dwExitCode);break;}
 }
 SymCleanup(pi.hProcess);return 0;
}
