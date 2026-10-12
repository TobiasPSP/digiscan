#include "analysis.h"
#include <cassert>
#include <cstdio>
#include <limits>
static unsigned property(const char *id) {
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (!strcmp(decoding::properties[i].id,id)) return i;
    assert(false); return 0;
}
int main() {
    decoding::Registry registry; assert(registry.assign(0x75,registry.find("ip2366"),decoding::Assignment::Manual));
    analysis::Decoder d; observer::Observation o{}; o.success=true; o.direction='R';
    assert(d.changeFactor==0 && analysis::Selection().changeFactor==0 && !analysis::Selection().active());
    const unsigned voltage=property("battery_voltage_mv"), power=property("system_power_mw");
    assert(decoding::properties[voltage].atomic_step==100);
    d.emit(voltage,12000,o,true,1); d.emit(voltage,12001,o,true,2);
    assert(d.values[voltage].fieldChanged_ms==2); // Default: even a one-unit change is significant.
    d.clear(); d.setChangeFactor(1);
    d.emit(voltage,12000,o,true,100);
    for (unsigned delta=1;delta<100;++delta) {
        d.emit(voltage,uint16_t(12000+delta),o,true,100+delta);
        assert(d.values[voltage].word==12000+delta && d.values[voltage].fieldChanged_ms==100);
        assert(d.values[voltage].fresh);
    }
    d.emit(voltage,12100,o,true,200); assert(d.values[voltage].fieldChanged_ms==200);
    assert(!d.values[voltage].fresh);
    d.emit(voltage,12001,o,true,201); assert(d.values[voltage].fieldChanged_ms==200);
    d.emit(voltage,12000,o,true,202); assert(d.values[voltage].fieldChanged_ms==202);
    d.setChangeFactor(0.5); d.emit(voltage,12049,o,true,203); assert(d.values[voltage].fieldChanged_ms==202);
    d.emit(voltage,12050,o,true,204); assert(d.values[voltage].fieldChanged_ms==204);
    d.setChangeFactor(10); d.emit(voltage,13049,o,true,205); assert(d.values[voltage].fieldChanged_ms==204);
    d.emit(voltage,13050,o,true,206); assert(d.values[voltage].fieldChanged_ms==206);
    d.setChangeFactor(0); d.emit(voltage,13051,o,true,207); assert(d.values[voltage].fieldChanged_ms==207);
    d.setChangeFactor(1); d.emit(power,100,o,true,300); d.emit(power,199,o,true,301);
    assert(d.values[power].fieldChanged_ms==300); // 99 codes = 990 mW, not 99 mW.
    d.emit(power,200,o,true,302); assert(d.values[power].fieldChanged_ms==302);
    const unsigned flag=property("r3a_thermal_loop"), temp=property("r3a_ic_temp"), ntc=property("ntc_voltage_mv");
    d.setChangeFactor(100); d.emit(flag,30,o,true,400); d.emit(temp,30,o,true,400);
    d.emit(flag,158,o,true,401); d.emit(temp,158,o,true,401);
    assert(d.values[flag].fieldChanged_ms==401 && d.values[temp].fieldChanged_ms==400);
    d.emit(ntc,3300,o,true,410); d.emit(ntc,3301,o,true,411);
    assert(d.values[ntc].fieldChanged_ms==411); // Entering invalid range is always significant.
    o.success=false; d.emit(voltage,13051,o,true,420); assert(d.values[voltage].fieldChanged_ms==420);
    o.success=true; d.emit(voltage,13051,o,true,421); assert(d.values[voltage].fieldChanged_ms==421);
    o.direction='W'; d.emit(voltage,13051,o,true,422); assert(d.values[voltage].fieldChanged_ms==422);
    d.emit(voltage,13051,o,false,423); assert(d.values[voltage].fieldChanged_ms==423);
    const unsigned cells=property("r04_bat_num"); d.emit(cells,4,o,true,430); d.emit(cells,5,o,true,431);
    assert(d.values[cells].fieldChanged_ms==431); // No step: exact field changes.
    d.setChangeFactor(std::numeric_limits<double>::quiet_NaN()); assert(d.changeFactor==100);
    d.setChangeFactor(-1); assert(d.changeFactor==100);
    d.clear(); d.emit(voltage,13052,o,true,0xfffffff0u); assert(d.values[voltage].fresh);
    puts("PASS: atomic steps, cumulative drift, scaling, factors, flags, ranges, outcomes and Clear anchors");
}
