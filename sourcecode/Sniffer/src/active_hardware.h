#pragma once
#include "active_read.h"
#include "capture.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/resets.h"
#include "hardware/regs/i2c.h"
namespace active_read {
struct Hardware {
  bool initializing=false,prepared=false,haveByte=false,stopped=false,restoreStarted=false;
  uint8_t reg=0,byte=0,address=0x75;
  uint32_t expectedStarts=0,expectedStops=0;bool betweenReads=false;
  uint32_t probeAbort=0;
  uint8_t rawMode=0,writeData[258]={};
  uint16_t writeSize=0,writeQueued=0;
  uint16_t readSize=0,readQueued=0,readReceived=0;
  uint8_t readData[256]={};
  void pause() { restoreStarted=betweenReads=false;capture::request(false); }
  bool interfered() { return betweenReads && capture::witnessStarts()!=expectedStarts; }
  bool paused() { return !capture::active(); }
  void witness(bool enabled) { capture::requestWitness(enabled); }
  bool witnessRunning() { return capture::witnessActive(); }
  bool idle() { return capture::witnessIdle(1000) && gpio_get(30) && gpio_get(31); }
  void releasedMux(unsigned pin,gpio_function_t fn) {
    // SDK gpio_set_function clears every override. Change only FUNCSEL instead.
    gpio_set_oeover(pin,GPIO_OVERRIDE_LOW);
    hw_write_masked(&io_bank0_hw->io[pin].ctrl,uint32_t(fn)<<IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB,
        IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS);
  }
  void disconnectPins() {
    for (unsigned pin=30;pin<=31;++pin) {
      // Override output-enable before changing mux, independent of FIFO state.
      gpio_set_oeover(pin,GPIO_OVERRIDE_LOW);gpio_set_dir(pin,false);
      releasedMux(pin,GPIO_FUNC_SIO);gpio_disable_pulls(pin);
    }
  }
  bool begin(uint8_t addressReg,uint32_t &actual_Hz) {
    if (!prepared || !idle() || !(i2c1_hw->enable_status&1)) return false;
    reg=addressReg;haveByte=stopped=false;probeAbort=0;
    expectedStarts=capture::witnessStarts()+((rawMode==1 || rawMode==2)?1:2);expectedStops=capture::witnessStops()+1;betweenReads=false;
    const uint32_t clk_Hz=clock_get_hz(clk_sys),period=(clk_Hz+5000)/10000;
    if (period<16 || period>100000) return false;
    actual_Hz=clk_Hz/period;
    (void)i2c1_hw->clr_intr;
    for (unsigned pin=30;pin<=31;++pin) gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
    writeQueued=0;
    readQueued=readReceived=0;
    if (rawMode==1) {
      // Stream the bounded write through the hardware FIFO in poll().
    } else if (rawMode==2) {
      i2c1_hw->data_cmd=I2C_IC_DATA_CMD_CMD_BITS|I2C_IC_DATA_CMD_STOP_BITS;
    } else if(rawMode==3){
      i2c1_hw->data_cmd=reg;
    } else {
      i2c1_hw->data_cmd=reg;
      i2c1_hw->data_cmd=I2C_IC_DATA_CMD_CMD_BITS|I2C_IC_DATA_CMD_RESTART_BITS|I2C_IC_DATA_CMD_STOP_BITS;
    }
    return true;
  }
  bool prepare() {
    auto *hw=i2c1_hw;
    if (prepared) return (hw->enable_status&1)!=0;
    if (!initializing) {
      disconnectPins();reset_block(RESETS_RESET_I2C1_BITS);unreset_block(RESETS_RESET_I2C1_BITS);
      initializing=true;return false;
    }
    if (initializing) {
      if (!(resets_hw->reset_done&RESETS_RESET_I2C1_BITS)) return false;
      const uint32_t clk_Hz=clock_get_hz(clk_sys),period=(clk_Hz+5000)/10000;
      const uint32_t low=period*3/5,high=period-low;
      if (low>65535 || high>65535 || low<8 || high<8) return false;
      hw->enable=0;
      hw->con=I2C_IC_CON_MASTER_MODE_BITS|I2C_IC_CON_IC_SLAVE_DISABLE_BITS|
          I2C_IC_CON_IC_RESTART_EN_BITS|I2C_IC_CON_TX_EMPTY_CTRL_BITS|
          (I2C_IC_CON_SPEED_VALUE_FAST<<I2C_IC_CON_SPEED_LSB);
      hw->fs_scl_hcnt=high;hw->fs_scl_lcnt=low;
      // RP2350 12.2.11: SS/FS suppression at most 50 ns, not SDK low-count/16.
      uint32_t spike=uint64_t(clk_Hz)*50/1000000000;
      hw->fs_spklen=spike?spike:1;
      hw->sda_hold=(uint64_t(clk_Hz)*3/10000000)+1; // SDK TX hold: 300 ns.
      hw->dma_cr=0;hw->intr_mask=0;hw->tx_tl=hw->rx_tl=0;hw->tar=address;
      (void)hw->clr_intr;
      for (unsigned pin=30;pin<=31;++pin) {
        releasedMux(pin,GPIO_FUNC_I2C);gpio_disable_pulls(pin);
      }
      hw->enable=1;
      // Empty FIFO: monitor START/STOP with all physical output enables inhibited.
      initializing=false;prepared=true;return false;
    }
    return false;
  }
  int poll(uint8_t &value,uint32_t &abort) {
    auto *hw=i2c1_hw;
    if (hw->raw_intr_stat&I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
      probeAbort|=hw->tx_abrt_source;(void)hw->clr_tx_abrt;
      const uint32_t reasons=probeAbort&~I2C_IC_TX_ABRT_SOURCE_TX_FLUSH_CNT_BITS;
      // A scan address NACK completes with the controller's automatic STOP.
      // All other aborts, especially arbitration loss, release immediately.
      if (rawMode!=2 || reasons!=I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS) {
        abort=probeAbort;return -1;
      }
    }
    abort=probeAbort;
    if (probeAbort) {
      if (hw->raw_intr_stat&I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) { stopped=true;(void)hw->clr_stop_det; }
      if (capture::witnessStarts()>expectedStarts || capture::witnessStops()>expectedStops) return -2;
      if (stopped && !hw->txflr && !(hw->status&I2C_IC_STATUS_MST_ACTIVITY_BITS) &&
          capture::witnessStarts()==expectedStarts && capture::witnessStops()==expectedStops) return -1;
      return 0; // Existing transfer deadline bounds missing STOP or stalled witness.
    }
    if (rawMode==1) {
      while (writeQueued<writeSize && (hw->status&I2C_IC_STATUS_TFNF_BITS)) {
        uint32_t command=writeData[writeQueued++];
        if (writeQueued==writeSize) command|=I2C_IC_DATA_CMD_STOP_BITS;
        hw->data_cmd=command;
      }
      haveByte=writeQueued==writeSize;byte=0;
    }
    if(rawMode==3){
      while(hw->rxflr){if(readReceived>=readSize)return -1;readData[readReceived++]=uint8_t(hw->data_cmd);}
      // Bound outstanding reads to the 16-byte hardware RX FIFO capacity.
      while(readQueued<readSize && readQueued-readReceived<16 && (hw->status&I2C_IC_STATUS_TFNF_BITS)){
        uint32_t command=I2C_IC_DATA_CMD_CMD_BITS;
        if(!readQueued)command|=I2C_IC_DATA_CMD_RESTART_BITS;
        if(++readQueued==readSize)command|=I2C_IC_DATA_CMD_STOP_BITS;
        hw->data_cmd=command;
      }
      haveByte=readReceived==readSize;byte=readReceived?readData[0]:0;
    } else if (hw->rxflr) {
      if (haveByte || hw->rxflr!=1) return -1;
      byte=uint8_t(hw->data_cmd);haveByte=true;
    }
    if (hw->raw_intr_stat&I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) { stopped=true;(void)hw->clr_stop_det; }
    if (haveByte && stopped && !hw->txflr && !(hw->status&I2C_IC_STATUS_MST_ACTIVITY_BITS)) {
      // Wait for the private PIO witness to confirm this complete compound read.
      // Reject any intervening START rather than mixing an externally refreshed ADC pair.
      if (capture::witnessStarts()>expectedStarts || capture::witnessStops()>expectedStops) return -2;
      if (capture::witnessStarts()!=expectedStarts || capture::witnessStops()!=expectedStops) return 0;
      value=byte;betweenReads=true;return 1;
    }
    return 0;
  }
  void release() {
    // Reset after physical release; never software-ABORT/STOP after arbitration loss.
    disconnectPins();reset_block(RESETS_RESET_I2C1_BITS);initializing=prepared=false;
  }
  bool released() {
    if (!(resets_hw->reset&RESETS_RESET_I2C1_BITS)) return false;
    for (unsigned pin=30;pin<=31;++pin) {
      const uint32_t oe=(io_bank0_hw->io[pin].ctrl&IO_BANK0_GPIO0_CTRL_OEOVER_BITS)>>IO_BANK0_GPIO0_CTRL_OEOVER_LSB;
      if (gpio_get_function(pin)!=GPIO_FUNC_SIO || gpio_get_dir(pin)!=GPIO_IN ||
          oe!=GPIO_OVERRIDE_LOW || gpio_is_pulled_up(pin) || gpio_is_pulled_down(pin)) return false;
    }
    return true;
  }
  void forceRelease() { release(); }
  void restore(bool enabled) {
    if (!restoreStarted) {
      for (unsigned pin=30;pin<=31;++pin) {
        releasedMux(pin,GPIO_FUNC_PIO1);gpio_disable_pulls(pin);gpio_set_oeover(pin,GPIO_OVERRIDE_NORMAL);
      }
      restoreStarted=true;
    }
    capture::request(enabled);
  }
  bool restored(bool enabled) {
    if (capture::active()!=enabled) return false;
    for (unsigned pin=30;pin<=31;++pin)
      if (gpio_get_function(pin)!=GPIO_FUNC_PIO1 || gpio_is_pulled_up(pin) || gpio_is_pulled_down(pin)) return false;
    return true;
  }
};
}
