#pragma once
#include <stdint.h>
namespace active_read {
enum class Code : uint8_t { Ok, BusBusy, AddressNack, PointerNack, ArbitrationLost,
  Timeout, Cancelled, RestoreFailed, HardwareAbort, CaptureFault, Interference };
enum class Phase : uint8_t { Idle, Pause, Witness, Transfer, Release, Restore, Done };
enum class Admission { New, Replay, Reject };
struct RequestGate {
  uint32_t session=0,last=0;bool cached=false;
  Admission admit(uint32_t token,uint32_t id,bool busy,bool pending,bool ready) {
    if (!token || !id) return Admission::Reject;
    if (token!=session) {
      if (busy || pending || !ready) return Admission::Reject;
      session=token;last=0;cached=false;
    }
    if (id==last && cached && !busy) return Admission::Replay;
    if (busy || pending || !ready || int32_t(id-last)<=0) return Admission::Reject;
    last=id;cached=false;return Admission::New;
  }
};
struct Result {
  uint32_t request=0,abort=0,clock_Hz=0,idle_us=0,duration_us=0;
  Code code=Code::Ok; uint8_t count=0,bytes[3]={},reg=0x50;
  bool wasArmed=false,restored=false;
};
inline Code abortCode(uint32_t bits) {
  if (bits&(1u<<12)) return Code::ArbitrationLost;
  if (bits&1) return Code::AddressNack;
  if (bits&(1u<<3)) return Code::PointerNack;
  return Code::HardwareAbort;
}
// Backend methods are nonblocking. All deadlines use monotonic microseconds.
template<class Backend> struct Job {
  Backend &io; Phase phase=Phase::Idle; Result result;
  uint32_t began_us=0,phase_us=0; bool cancel=false;
  uint32_t transferTimeout_us=50000;
  bool retainWitness=false;
  bool retainPointerNack=false; // Opt-in for property batches, never scan probes.
  uint8_t registers[3]={0x50,0x51,0},requested=2;
  explicit Job(Backend &backend):io(backend) {}
  bool busy() const { return phase!=Phase::Idle && phase!=Phase::Done; }
  bool start(uint32_t id,uint32_t now_us,bool armed) {
    if (busy() || !id) return false;
    result=Result{};result.request=id;result.wasArmed=armed;result.reg=registers[0];
    began_us=phase_us=now_us;cancel=false;phase=Phase::Pause;
    io.pause();return true;
  }
  bool startPlan(uint32_t id,uint32_t now_us,bool armed,const uint8_t *regs,uint8_t count) {
    if (busy() || !id || !regs || !count || count>3) return false;
    for (unsigned i=1;i<count;++i) if (unsigned(regs[i-1])+1!=regs[i]) return false;
    requested=count;
    for (unsigned i=0;i<count;++i) registers[i]=regs[i];
    return start(id,now_us,armed);
  }
  void finish(Code code,uint32_t now_us) {
    result.code=code;io.release();phase=Phase::Release;phase_us=now_us;
  }
  void tick(uint32_t now_us) {
    if (!busy()) return;
    if (cancel && phase<Phase::Release) finish(Code::Cancelled,now_us);
    switch (phase) {
    case Phase::Pause:
      if (io.paused()) { io.witness(true);phase=Phase::Witness;phase_us=now_us; }
      else if (uint32_t(now_us-phase_us)>=100000) finish(Code::CaptureFault,now_us);
      break;
    case Phase::Witness:
      if (io.interfered()) finish(Code::Interference,now_us);
      else if (io.prepare() && io.idle()) {
        result.idle_us+=uint32_t(now_us-phase_us);
        result.reg=registers[result.count];
        if (!io.begin(result.reg,result.clock_Hz)) finish(Code::HardwareAbort,now_us);
        else { phase=Phase::Transfer;phase_us=now_us; }
      } else if (uint32_t(now_us-phase_us)>=2000000) {
        result.idle_us+=uint32_t(now_us-phase_us);finish(Code::BusBusy,now_us);
      }
      break;
    case Phase::Transfer: {
      uint8_t byte=0;uint32_t abort=0;
      int outcome=io.poll(byte,abort);
      result.abort|=abort;
      if (outcome==-2) finish(Code::Interference,now_us);
      else if (outcome<0) finish(abortCode(abort),now_us);
      else if (outcome>0) {
        result.bytes[result.count++]=byte;result.reg=registers[result.count-1];
        io.release();phase=Phase::Release;phase_us=now_us;
      } else if (uint32_t(now_us-phase_us)>=transferTimeout_us) finish(Code::Timeout,now_us);
      break;
    }
    case Phase::Release:
      if (io.released()) {
        if (result.code==Code::Ok && result.count<requested && !cancel) {
          phase=Phase::Witness;phase_us=now_us;
        } else {
          if (cancel && result.code==Code::Ok) result.code=Code::Cancelled;
          if (retainWitness && !cancel && (result.code==Code::Ok || result.code==Code::AddressNack || (retainPointerNack && result.code==Code::PointerNack))) {
            result.duration_us=uint32_t(now_us-began_us);phase=Phase::Done;break;
          }
          io.witness(false);phase=Phase::Restore;phase_us=now_us;
        }
      } else if (uint32_t(now_us-phase_us)>=5000) {
        result.code=Code::RestoreFailed;io.forceRelease();io.witness(false);
        phase=Phase::Restore;phase_us=now_us;
      }
      break;
    case Phase::Restore:
      if (!io.witnessRunning()) {
        io.restore(result.wasArmed && result.code!=Code::RestoreFailed && result.code!=Code::CaptureFault);
        if (io.restored(result.wasArmed && result.code!=Code::RestoreFailed && result.code!=Code::CaptureFault)) {
          result.restored=result.code!=Code::RestoreFailed && result.code!=Code::CaptureFault;
          result.duration_us=uint32_t(now_us-began_us);phase=Phase::Done;
        }
      }
      if (phase!=Phase::Done && uint32_t(now_us-phase_us)>=100000) {
        io.restore(false);result.restored=false;result.code=Code::RestoreFailed;
        result.duration_us=uint32_t(now_us-began_us);phase=Phase::Done;
      }
      break;
    default:break;
    }
  }
};
}
