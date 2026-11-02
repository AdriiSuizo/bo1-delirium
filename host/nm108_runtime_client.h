#pragma once
#include "nm108_runtime_protocol.h"
#include <windows.h>
#include <string>
#include <cstring>
#include <cstdio>
namespace nm108 {
// One owned runtime process. The production game can use this client without
// importing original code, sharing TLS slots, or embedding proprietary files.
class Client {
    HANDLE mapping_=nullptr,request_=nullptr,response_=nullptr,process_=nullptr;
    std::string logPath_;
    Exchange* exchange_=nullptr;
    uint64_t revision_=0;
    int32_t sequence_=0;
    bool pending_=false;
    Response state_={};ActorState actors_[MaxActors]={};Rig rig_={};FragmentState fragments_[MaxFragments]={};
    void Snapshot(){MemoryBarrier();state_=exchange_->response;memcpy(actors_,exchange_->actors,sizeof(actors_));rig_=exchange_->rig;memcpy(fragments_,exchange_->fragments,sizeof(fragments_));++revision_;}
    bool Wait(unsigned timeout) {
        HANDLE waits[2]={response_,process_};
        return WaitForMultipleObjects(2,waits,FALSE,timeout)==WAIT_OBJECT_0;
    }
    static void StartupFailure(const char* path,const char* stage,DWORD error,DWORD exit,unsigned status){
        HANDLE log=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log==INVALID_HANDLE_VALUE)return;
        char line[192];int length=sprintf_s(line,"runtime startup failed: stage=%s win32=%lu exit=0x%08lx status=%u\r\n",stage,error,exit,status);
        if(length>0){DWORD written;WriteFile(log,line,length,&written,nullptr);}CloseHandle(log);
    }
public:
    void ReportFailure(const char* stage){
        if(logPath_.empty())return;DWORD exit=0;if(process_)GetExitCodeProcess(process_,&exit);
        FILE*f=nullptr;std::string path=logPath_+".host-errors.log";if(!fopen_s(&f,path.c_str(),"ab")&&f){
            fprintf(f,"IPC failure stage=%s exit=%08lx pending=%u sequence=%i completed=%i command=%u status=%u\n",stage,exit,pending_,sequence_,exchange_?exchange_->completed:0,exchange_?exchange_->request.command:0,exchange_?exchange_->response.status:0);fclose(f);}
    }
    Client()=default;
    Client(const Client&)=delete;Client& operator=(const Client&)=delete;
    ~Client(){Close();}
    bool Open(const char* executable,const char* logPath,bool fixtureFloor=false) {
        if(process_) return false;
        logPath_=logPath;
        char suffix[64];sprintf_s(suffix,"Local\\BO1-NM108-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
        const std::string name=suffix;
        mapping_=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Exchange),name.c_str());
        if(!mapping_ || GetLastError()==ERROR_ALREADY_EXISTS) {Close();return false;}
        exchange_=static_cast<Exchange*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Exchange)));
        request_=CreateEventA(nullptr,FALSE,FALSE,(name+"-request").c_str());
        response_=CreateEventA(nullptr,FALSE,FALSE,(name+"-response").c_str());
        if(!exchange_ || !request_ || !response_) {Close();return false;}
        memset(exchange_,0,sizeof(*exchange_));
        exchange_->magic=Magic;exchange_->version=Version;exchange_->bytes=sizeof(Exchange);
        exchange_->ownerPid=GetCurrentProcessId();
        exchange_->flags=fixtureFloor?FixtureFloor:0;
        SECURITY_ATTRIBUTES attributes={sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE log=CreateFileA(logPath,GENERIC_WRITE,FILE_SHARE_READ,&attributes,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log==INVALID_HANDLE_VALUE) {Close();return false;}
        STARTUPINFOA startup={};startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        startup.wShowWindow=SW_HIDE;startup.hStdOutput=log;startup.hStdError=log;
        // An inheritable input handle is required by STARTF_USESTDHANDLES.
        HANDLE input=CreateFileA("NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);
        startup.hStdInput=input;
        PROCESS_INFORMATION info={};
        std::string command="\""+std::string(executable)+"\" \""+name+"\"";
        // One simulation thread must finish its short solver exchanges even
        // when a background reconstruction occupies all CPU cores. This is
        // AboveNormal, never High/Realtime, and changes only our own child.
        bool started=CreateProcessA(executable,&command[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW|ABOVE_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&startup,&info)!=FALSE;
        DWORD startError=started?0:GetLastError();
        CloseHandle(log);if(input!=INVALID_HANDLE_VALUE) CloseHandle(input);
        if(!started) {StartupFailure(logPath,"create-process",startError,0,0);Close();return false;}
        process_=info.hProcess;CloseHandle(info.hThread);
        if(!Wait(15000) || exchange_->response.status!=static_cast<unsigned>(Status::Ready)) {
            DWORD exit=STILL_ACTIVE;GetExitCodeProcess(process_,&exit);
            StartupFailure(logPath,"ready",0,exit,exchange_->response.status);Close();return false;}
        Snapshot();
        return true;
    }
    const Response* State() const {return exchange_?&state_:nullptr;}
    const ActorState* Actors() const {return exchange_?actors_:nullptr;}
    const FragmentState* Fragments() const {return exchange_?fragments_:nullptr;}
    const Rig* Skeleton() const {return exchange_?&rig_:nullptr;}
    uint64_t Revision()const{return revision_;}
    bool Pending()const{return pending_;}
    bool StartBatch(const Request* requests,unsigned count){
        if(pending_||!exchange_||count>32)return false;
        memcpy(exchange_->batch,requests,count*sizeof(Request));
        Request request={};request.command=16;request.steps=count;return Start(request);
    }
    bool Start(const Request& request){
        if(pending_ || !exchange_ || !process_ || WaitForSingleObject(process_,0)!=WAIT_TIMEOUT || sequence_==INT32_MAX) return false;
        pending_=true;
        exchange_->request=request;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&exchange_->requested),++sequence_);
        SetEvent(request_);
        return true;
    }
    // Pose readers use the private snapshot while the worker writes its next
    // result. Never expose a partially updated shared skeleton to BO1.
    int Poll(Response& output){
        if(!pending_||!exchange_)return -1;
        if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&exchange_->completed),0,0)==sequence_){
            Snapshot();output=state_;pending_=false;return 1;}
        return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT?0:-1;
    }
    bool Finish(Response& output,unsigned timeout=2500){
        const ULONGLONG deadline=GetTickCount64()+timeout;
        for(;;) {
            int result=Poll(output);if(result){if(result<0)ReportFailure("worker-exit");return result>0;}
            const ULONGLONG now=GetTickCount64();
            if(now>=deadline){ReportFailure("deadline");return false;}
            if(!Wait(static_cast<unsigned>(deadline-now))){ReportFailure("wait");return false;}
        }
    }
    bool Submit(const Request& request,Response& output,unsigned timeout=2500){
        if(pending_&&!Finish(output,timeout))return false;
        return Start(request)&&Finish(output,timeout);
    }
    void Close() {
        if(process_) {
            if(WaitForSingleObject(process_,0)==WAIT_TIMEOUT) {
                Request request={};request.command=static_cast<unsigned>(Command::Close);
                Response response={};Submit(request,response,1000);
                if(WaitForSingleObject(process_,2000)==WAIT_TIMEOUT) {
                    // This is exactly the child created by this Client. Never
                    // enumerate or close other GTA/BO1/user processes.
                    TerminateProcess(process_,24);WaitForSingleObject(process_,1000);
                }
            }
            CloseHandle(process_);process_=nullptr;
        }
        if(exchange_) {UnmapViewOfFile(exchange_);exchange_=nullptr;}
        if(mapping_) {CloseHandle(mapping_);mapping_=nullptr;}
        if(request_) {CloseHandle(request_);request_=nullptr;}
        if(response_) {CloseHandle(response_);response_=nullptr;}
        sequence_=0;
        pending_=false;
    }
};
}
