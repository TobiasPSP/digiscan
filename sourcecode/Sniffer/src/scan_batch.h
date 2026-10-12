#pragma once
#include "active_read.h"
namespace active_read {
// One witness lifetime, but independent idle admission and hardware release per probe.
template<class Backend> struct ScanBatch {
  Job<Backend> &job;
  bool running=false,deadlineExpired=false;
  uint8_t address=8,last=119;
  uint32_t request=0,began_us=0;
  bool wasArmed=false;
  explicit ScanBatch(Job<Backend> &j):job(j) {}
  bool start(uint32_t id,uint32_t now_us,bool armed,uint8_t first,uint8_t end) {
    if (running || job.busy() || first<8 || end>119 || first>end || !id) return false;
    running=true;deadlineExpired=false;address=first;last=end;request=id;began_us=now_us;wasArmed=armed;
    next(now_us);return true;
  }
  void next(uint32_t now_us) {
    job.io.address=address;job.io.rawMode=2;job.io.writeSize=0;
    job.transferTimeout_us=500000;job.retainWitness=address<last;
    job.retainPointerNack=false;
    const uint8_t reg=0;job.startPlan(request,now_us,wasArmed,&reg,1);
  }
  bool terminal() const { return !job.retainWitness || job.result.restored ||
    (job.result.code!=Code::Ok && job.result.code!=Code::AddressNack); }
  void cancel(uint32_t now_us,Code reason=Code::Cancelled) {
    if (!running) return;
    job.retainWitness=false;
    if (job.phase==Phase::Done && !job.result.restored) job.finish(reason,now_us);
    else if (job.busy() && job.phase<Phase::Release) job.finish(reason,now_us);
    else job.cancel=true;
  }
  void tick(uint32_t now_us) {
    if (running && !deadlineExpired && uint32_t(now_us-began_us)>=30000000) {
      deadlineExpired=true;cancel(now_us,Code::Timeout);
    }
    job.tick(now_us);
  }
  void delivered(uint32_t now_us) {
    if (!running || job.phase!=Phase::Done) return;
    if (terminal()) { running=false;job.retainWitness=false;job.phase=Phase::Idle; }
    else { ++address;next(now_us); }
  }
};
}
