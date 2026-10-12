#pragma once
#include <stddef.h>
#include "active_read.h"
namespace active_read {
// Retain synchronization across a batch, not electrical ownership of the bus.
// Each byte gets its own idle admission and physical release of both pins.
template<class Backend> struct ReadBatch {
  static constexpr unsigned Capacity=128;
  struct Plan { uint8_t address,reg,count; } plans[Capacity];
  Job<Backend> &job;
  bool running=false,wasArmed=false;
  unsigned count=0,index=0;
  uint32_t request=0,began_us=0;
  explicit ReadBatch(Job<Backend> &j):job(j) {}
  static bool valid(const uint8_t *p,size_t n) {
    if(n<10 || p[0]!=22 || p[1]!=0xa5 || !p[6] || p[6]>Capacity || n!=7+3u*p[6])return false;
    for(unsigned i=0;i<p[6];++i){const auto *d=p+7+3*i;
      if(d[0]<8 || d[0]>119 || !d[2] || d[2]>3 || unsigned(d[1])+d[2]>256)return false;
    }
    return true;
  }
  bool start(uint32_t id,uint32_t now_us,bool armed,const uint8_t *p,size_t n) {
    if(running || job.busy() || !id || !valid(p,n))return false;
    count=p[6];index=0;request=id;began_us=now_us;wasArmed=armed;running=true;
    for(unsigned i=0;i<count;++i)plans[i]={p[7+3*i],p[8+3*i],p[9+3*i]};
    next(now_us);return true;
  }
  void next(uint32_t now_us) {
    const auto &p=plans[index];job.io.address=p.address;job.io.rawMode=0;job.io.writeSize=0;
    job.transferTimeout_us=50000;job.retainWitness=index+1<count;
    job.retainPointerNack=true;
    uint8_t regs[3]={p.reg,uint8_t(p.reg+1),uint8_t(p.reg+2)};
    job.startPlan(request,now_us,wasArmed,regs,p.count);
  }
  bool terminal() const {return !job.retainWitness || job.result.restored ||
    (job.result.code!=Code::Ok && job.result.code!=Code::AddressNack && job.result.code!=Code::PointerNack);}
  void cancel(uint32_t now_us,Code reason=Code::Cancelled) {
    if(!running)return;job.retainWitness=false;
    if(job.phase==Phase::Done && !job.result.restored)job.finish(reason,now_us);
    else if(job.busy() && job.phase<Phase::Release)job.finish(reason,now_us);
    else job.cancel=true;
  }
  void tick(uint32_t now_us) {
    if(running && uint32_t(now_us-began_us)>=30000000)cancel(now_us,Code::Timeout);
    job.tick(now_us);
  }
  void delivered(uint32_t now_us) {
    if(!running || job.phase!=Phase::Done)return;
    if(terminal()){running=false;job.retainWitness=false;job.phase=Phase::Idle;}
    else{++index;next(now_us);}
  }
};
}
