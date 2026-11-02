#include "nm108_runtime_client.h"
#include "nm108_hit_batch.h"
#include "euphoria_wound_budget.h"
#include "nm108_balance_state.h"
#include <cassert>
#include <cmath>
#include <string>
#include <cstdio>
// This is an IPC fixture, never a physics backend. It verifies the actual
// cross-process event/mapping protocol without claiming an SDK physics test.
int main(int argc,char** argv){
 if(argc==2){
  std::string name=argv[1];HANDLE m=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
  auto*x=(nm108::Exchange*)MapViewOfFile(m,FILE_MAP_ALL_ACCESS,0,0,sizeof(nm108::Exchange));
  HANDLE req=OpenEventA(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(name+"-request").c_str());
  HANDLE res=OpenEventA(EVENT_MODIFY_STATE,FALSE,(name+"-response").c_str());
  HANDLE owner=OpenProcess(SYNCHRONIZE,FALSE,x->ownerPid);
  if(x->magic!=nm108::Magic||x->version!=19||x->bytes!=sizeof(*x))return 22;
  x->response.status=1;SetEvent(res);HANDLE wait[]={req,owner};
  while(WaitForMultipleObjects(2,wait,FALSE,5000)==WAIT_OBJECT_0){
   const LONG seq=InterlockedCompareExchange((volatile LONG*)&x->requested,0,0);
   auto r=x->request;bool close=r.command==2;x->response.status=close?6:2;
   if(r.command==16){if(r.steps>32)return 23;unsigned expected=0;for(unsigned i=0;i<r.steps;++i){if(x->batch[i].actor!=i||x->batch[i].command!=9)return 24;expected+=x->batch[i].active;}Sleep(40);x->response.frame=expected;}
   InterlockedExchange((volatile LONG*)&x->completed,seq);SetEvent(res);if(close)break;
  }
  UnmapViewOfFile(x);for(HANDLE h:{req,res,owner,m})CloseHandle(h);return 0;
 }
 euphoria::WoundBudget w={};assert(!w.hit(20,10000,0));assert(!w.hit(20,10000,2000));
 bool triggered=false;for(int i=0;i<30;++i)triggered|=w.hit(20,10000,2000+i*10);assert(triggered);
 assert(w.hit(100,100,3000));
 nm108::Hit a={},b={};a.impulse[0]=2;b.impulse[0]=3;a.point[1]=1;b.point[1]=4;
 assert(nm108::MergeHit(a,b));assert(a.impulse[0]==5&&std::fabs(a.point[1]-2.8f)<1e-5f);
 b.impulse[1]=1;assert(!nm108::MergeHit(a,b));
 nm108::ActorState state={};assert(!nm108::SafeLocomotion(state));state.recoveryActive=1;assert(!nm108::SafeLocomotion(state));
 char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);
 nm108::Client client;assert(client.Open(exe,"ipc-fixture.log"));
 nm108::Request batch[32]={};for(unsigned i=0;i<32;++i){batch[i].command=9;batch[i].actor=i;batch[i].active=i+1;}
 LARGE_INTEGER f,start,end;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&start);assert(client.StartBatch(batch,32));QueryPerformanceCounter(&end);
 double ms=double(end.QuadPart-start.QuadPart)*1000/f.QuadPart;assert(ms<20);
 assert(!client.StartBatch(batch,32));assert(client.State()->frame==0);
 nm108::Response response={};assert(client.Finish(response));assert(response.frame==528);assert(client.Revision()==2);
 client.Close();printf("PASS wound accumulation/lethal, impulse torque merge, recovery guard, x86 IPC v19 ordered 32-command batch, immutable pending snapshot; enqueue_ms=%.3f\n",ms);
}
