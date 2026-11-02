// One grenade in a horde, driven like BO1 drives the bridge: every game frame
// each zombie still waiting asks rd_actor_can_activate and, refused, is
// despawned and asked again next frame (cg_euphoria_rd.cpp). Measures how
// long the last zombie waits to react, how many game frames advanced no
// physics at all (the crowd's slow motion), the BO1-thread cost per frame,
// and the fastest body afterwards (an overlapping birth must not explode).
#include "../host/nm108_native_bridge.cpp"
#include <cstdio>
#pragma comment(lib,"winmm.lib")
#define CHECK(x) do{if(!(x)){printf("FAIL line=%u\n",__LINE__);return 1;}}while(0)
static double Ms(LARGE_INTEGER a,LARGE_INTEGER b){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(b.QuadPart-a.QuadPart)*1000.0/f.QuadPart;}
int main(int argc,char** argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 const unsigned n=argc>1?unsigned(atoi(argv[1])):20;const float spacing=argc>2?float(atof(argv[2])):.45f;
 RdWorldDesc desc={39.37f,RD_AXES_Z_UP};auto* w=rd_world_create_ex(&desc);CHECK(w);
 const float floor[]={-6000,-6000,-40,6000,-6000,-40,6000,6000,-40,-6000,6000,-40,-6000,-6000,0,6000,-6000,0,6000,6000,0,-6000,6000,0};
 CHECK(!rd_add_static_convex(w,floor,8));CHECK(!rd_world_set_async(w,1));
 // Two zombies already in physics keep the solver busy (asynchronous), as in a game.
 uint32_t busy[2];
 for(unsigned i=0;i<2;++i){RdTransform root={Host(Position(w->rest[0])+V{30.f+i*3,0,0},w->units),Host(MatrixQ(w->rest[0]))};CHECK(!rd_actor_spawn(w,0,&root,{},busy+i));
  RdVec3 lo[16],hi[16];CHECK(!rd_actor_get_bounds(w,busy[i],lo,hi));float lift=(.25f-(std::min)(lo[3].z,lo[6].z))/w->units;for(auto&p:w->Find(busy[i])->initial.pose)p[14]+=lift;
  CHECK(!rd_actor_set_mode(w,busy[i],RD_MODE_ACTIVE));for(unsigned f=0;f<3;++f)CHECK(rd_world_update(w,1.f/60.f)>=0);}
 const bool shotgun=argc>3&&std::string(argv[3])=="shotgun";
 float masses[16];CHECK(!rd_model_body_masses(w,masses));
 uint32_t actors[32]={};int reactedAt[32];for(unsigned i=0;i<32;++i)reactedAt[i]=-1;
 unsigned stalled=0,frames=0;double worst=0,total=0;float fastest=0;
 timeBeginPeriod(1);
 for(unsigned f=0;f<240;++f){LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
  for(unsigned i=0;i<n;++i){if(reactedAt[i]>=0)continue;
   V pos=Position(w->rest[0])+V{float(i%5)*spacing,float(i/5)*spacing,0};
   RdTransform root={Host(pos,w->units),Host(MatrixQ(w->rest[0]))};CHECK(!rd_actor_spawn(w,0,&root,{},actors+i));
   RdVec3 lo[16],hi[16];CHECK(!rd_actor_get_bounds(w,actors[i],lo,hi));
   float lift=(.25f-(std::min)(lo[3].z,lo[6].z))/w->units;for(auto& p:w->Find(actors[i])->initial.pose)p[14]+=lift;
   if(rd_actor_can_activate(w,actors[i])!=1){CHECK(!rd_actor_despawn(w,actors[i]));continue;}
   CHECK(!rd_actor_set_mode(w,actors[i],RD_MODE_ACTIVE));reactedAt[i]=int(f);
   if(shotgun){
    for(unsigned pellet=0;pellet<8;++pellet){RdHit hit={};hit.body=8;V at=Position(w->Pose(actors[i]&31,10));hit.point=Host(at,w->units);hit.impulse={float(pellet)*.02f,10.f,0.f};CHECK(!rd_actor_hit_profile(w,actors[i],&hit,0.f,75.f,euphoria::ShotgunPush));}
   }else for(unsigned p=0;p<16;++p){V dv={float(i%5)-2.f,float(i/5)-2.f,3.f};float l=sqrtf(Dot(dv,dv));dv=dv*(6.f/l);
    CHECK(!rd_actor_push(w,actors[i],p,Host(dv*masses[p],1),.08f,nm108::ExplosionImpulse));}

  }
  int advanced=rd_world_update(w,1.f/60.f);CHECK(advanced>=0);
  QueryPerformanceCounter(&b);double ms=Ms(a,b);worst=(std::max)(worst,ms);total+=ms;++frames;
  bool waiting=false;for(unsigned i=0;i<n;++i)waiting|=reactedAt[i]<0;
  if(advanced==0&&!w->client.Pending())++stalled;
  if(f>60)for(unsigned i=0;i<n;++i)if(reactedAt[i]>=0){const auto& s=w->client.Actors()[actors[i]&31];fastest=(std::max)(fastest,sqrtf(Dot(V{s.velocity[0],s.velocity[1],s.velocity[2]},V{s.velocity[0],s.velocity[1],s.velocity[2]})));}
  if(!waiting&&f>200)break;
  // A game frame at 60 fps: the solver runs while BO1 renders.
  for(;;){LARGE_INTEGER c;QueryPerformanceCounter(&c);if(Ms(a,c)>=1000.0/60.0)break;Sleep(0);}
 }
 int last=-1;unsigned never=0;for(unsigned i=0;i<n;++i){if(reactedAt[i]<0)++never;last=(std::max)(last,reactedAt[i]);}
 printf("BURST n=%u spacing=%.2f reacted=%u/%u last_reaction_frame=%d stalled_frames=%u bo1_frame_avg_ms=%.2f bo1_frame_worst_ms=%.2f fastest_after_1s=%.1f\n",
  n,spacing,n-never,n,last,stalled,total/frames,worst,fastest);
 printf("SOLVER completed_frames=%u debt_seconds=%.3f pending_steps=%u\n",w->client.State()->frame,w->accumulator,w->pendingSteps);
 const unsigned completedFrames=w->client.State()->frame;
 rd_world_destroy(w);
 if(never||last>2||fastest>12.f||completedFrames<frames*9/10){puts("FAIL: a grenade's horde reacts late, stalls the crowd or explodes");return 2;}
 puts("PASS: the whole horde reacts in the grenade's frame with responsive enqueue and measured solver progress");return 0;
}
