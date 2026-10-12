#include "preview_platform.h"
#include "analysis.h"
#include "detail_text.h"
#include "profile_storage.h"
#include <assert.h>
unsigned slot(uint8_t address,const char *id) {
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoding::properties[i].count && decoding::properties[i].address==address && !strcmp(decoding::properties[i].id,id)) return i;
    assert(false); return 0;
}
int main() {
    decoding::Registry registry;
    int ip2366=registry.find("ip2366"),ip2368=registry.find("ip2368"),ip2369=registry.find("ip2369"),only;
    assert(ip2366>=0 && ip2368>=0 && ip2369>=0 && decoding::ProfileCount==3);
    assert(registry.candidates(0x75,only)==3);
    registry.observe(0x75,true); assert(registry.bindings[0x75].profile==-1 && registry.nextPrompt()==0x75);
    registry.bindings[0x75].deferred=true; assert(registry.nextPrompt()==-1);
    registry.observe(0x40,false); assert(registry.bindings[0x40].profile==-1);
    assert(registry.assign(0x40,ip2366,decoding::Assignment::Manual));
    assert(registry.assign(0x41,ip2366,decoding::Assignment::Manual));
    analysis::Decoder decoder;
    auto add=[&](uint8_t address,uint8_t reg,uint8_t byte,uint32_t ms,bool success=true) {
        observer::Observation o; o.address=address; o.reg=reg; o.associated=true; o.dataBytes=1; o.preview[0]=byte; o.direction='R'; o.success=success; o.id=ms;
        decoder.add(o,ms);
    };
    add(0x40,0x6e,0xfd,1); add(0x41,0x6e,0x12,2); add(0x40,0x6f,7,3); add(0x41,0x6f,0x34,4);
    unsigned a=slot(0x40,"battery_current_code_ma"),b=slot(0x41,"battery_current_code_ma");
    assert(decoder.values[a].word==2045 && decoder.values[b].word==0x3412);
    assert(!analysis::sameSource(decoder.values[a],decoder.values[b],true));
    auto expect2366=[&](const char *id,const char *expected) {
        char rendered[192]; analysis::formatValue(decoder.values[slot(0x40,id)],rendered,sizeof(rendered));
        assert(!strcmp(rendered,expected));
    };
    add(0x40,0x0d,0,100); expect2366("r0d_pdo_select","Automatic selection");
    add(0x40,0x0d,5,101); expect2366("r0d_pdo_select","20 V");
    for (uint8_t code:{uint8_t(6),uint8_t(7)}) {
        add(0x40,0x0d,code,102); char output[96];
        analysis::formatValue(decoder.values[slot(0x40,"r0d_pdo_select")],output,sizeof(output));
        assert(strstr(output,"UNDEFINED"));
    }
    add(0x40,0x0a,0x20,103); expect2366("r0a_set_batlow","2600 mV");
    add(0x40,0x0a,0xa0,104); expect2366("r0a_set_batlow","3000 mV");
    add(0x40,0x33,0x86,105); expect2366("r33_chg_vbus","20 V");
    add(0x40,0x06,2,106); expect2366("r06_itk","100 mA");
    add(0x40,0x06,4,107); expect2366("r06_itk","200 mA");
    add(0x40,0x06,20,108); expect2366("r06_itk","1000 mA");
    add(0x40,0x06,1,109); expect2366("r06_itk","50 mA RANGE");
    add(0x40,0x06,21,110); expect2366("r06_itk","1050 mA RANGE");
    add(0x40,0x6e,0xa3,111); add(0x40,0x6f,7,112); expect2366("battery_current_code_ma","1955 mA");
    add(0x40,0x6e,0xd1,113); add(0x40,0x6f,7,114); expect2366("battery_current_code_ma","2001 mA");
    add(0x40,0x70,0x76,115); add(0x40,0x71,5,116); expect2366("system_current_code_ma","1398 mA");
    add(0x40,0x70,0x7e,117); add(0x40,0x71,5,118); expect2366("system_current_code_ma","1406 mA");
    add(0x40,0x74,0xc8,119); add(0x40,0x75,0x0a,120); expect2366("system_power_mw","27600 mW");
    add(0x40,0x74,9,121); add(0x40,0x75,0x0b,122); expect2366("system_power_mw","28250 mW");
    add(0x40,0x17,0,123); expect2366("r17_ntc_en","NO");
    add(0x40,0x17,0x80,124); expect2366("r17_ntc_en","YES");
    add(0x40,0x3a,0x80|30,125); expect2366("r3a_thermal_loop","YES"); expect2366("r3a_ic_temp","30 degC");
    puts("PASS: IP2366 DUT voltage maps, current magnitudes, USB power, trickle range and flag polarity");
    add(0x40,0x6e,1,5); add(0x41,0x31,0,6,false); add(0x40,0x6f,2,7);
    assert(decoder.values[a].complete && decoder.values[a].word==513);
    decoder.clearAddress(0x40); assert(!decoder.used[a] && decoder.used[b]);
    for (unsigned i=0;i<decoder.historyCount;++i) assert(decoding::properties[decoder.row(false,i)->property].address==0x41);
    assert(registry.assign(0x40,ip2369,decoding::Assignment::Manual));
    add(0x40,0x6f,7,8); auto draft=slot(0x40,"r6e_battery_current_code");
    assert(!decoder.values[draft].complete); // Never reuse the previous profile's low byte.
    add(0x40,0x04,7,9); char text[192]; analysis::formatValue(decoder.values[slot(0x40,"r04_bat_num")],text,sizeof(text));
    assert(strstr(text,"RANGE"));
    add(0x40,0x0c,5,10); analysis::formatValue(decoder.values[slot(0x40,"r0c_vbus_snk_power")],text,sizeof(text));
    assert(strstr(text,"UNDEFINED"));
    auto expect=[&](const char *id,const char *expected) {
        analysis::formatValue(decoder.values[slot(0x40,id)],text,sizeof(text)); assert(!strcmp(text,expected));
    };
    add(0x40,0x50,0x33,20); add(0x40,0x51,0x34,21);
    expect("r50_battery_voltage_code","13363 mV");
    add(0x40,0x52,0x46,22); add(0x40,0x53,0x34,23);
    expect("r52_system_voltage_code","13382 mV");
    for (uint8_t reg:{uint8_t(0x54),uint8_t(0x56),uint8_t(0x6e),uint8_t(0x70)}) {
        add(0x40,reg,0xfd,24); add(0x40,reg+1,7,25);
    }
    expect("r54_usb_c_mos_current_code","2045 mA"); expect("r56_usb_a_mos_current_code","2045 mA");
    expect("r6e_battery_current_code","2045 mA"); expect("r70_system_current_code","2045 mA");
    add(0x40,0x74,2,26); add(0x40,0x75,1,27); expect("r74_system_power_code","2580 mW");
    add(0x40,0x78,0x1e,28); add(0x40,0x79,5,29); expect("r78_ntc_voltage_code","1310 mV");
    add(0x40,0x02,0xaa,30); expect("r02_vset","4200 mV");
    add(0x40,0x03,0x61,31); expect("r03_iset","9700 mA");
    add(0x40,0x06,4,32); expect("r06_itk","200 mA");
    add(0x40,0x08,0x20,33); expect("r08_istop","100 mA");
    add(0x40,0x10,4,34); expect("r10_temp_loop","100 degC");
    add(0x40,0x16,10,35); expect("r16_standby_time","10 s");
    add(0x40,0x3a,49,36); expect("r3a_ic_temp","49 degC"); expect("r3a_temp_loop","NO");
    add(0x40,0x3a,0x80|49,37); expect("r3a_ic_temp","49 degC"); expect("r3a_temp_loop","YES");
    add(0x40,0x77,0,38); expect("r77_ntc_excitation_current","20 uA");
    add(0x40,0x77,0x80,39); expect("r77_ntc_excitation_current","80 uA");
    add(0x40,0x24,0x96,40); expect("r24_pdo_current_24","3000 mA");
    add(0x40,0x29,0x3c,41); expect("r29_pdo_current_29","3000 mA");
    add(0x40,0x17,0x80,42); expect("r17_ntc_en","YES");
    add(0x40,0x17,0,43); expect("r17_ntc_en","NO");
    add(0x40,0x0a,0xa0,44); expect("r0a_set_batlow","3000 mV");
    add(0x40,0x0a,3,45); expect("r0a_adjust_data","300 mV");
    puts("PASS: IP2369 reference conversions: 13363 mV, mA, 10 mW scaling, Celsius/thermal mask, uA, seconds, PDO/charge settings and flag polarity");
    decoder.clearAddress(0x40); assert(registry.assign(0x40,ip2368,decoding::Assignment::Manual));
    add(0x40,0x50,0x33,50); add(0x40,0x51,0x34,51); expect("r50_battery_voltage","13363 mV");
    add(0x40,0x54,0xfd,52); add(0x40,0x55,7,53); expect("r54_input_current","2045 mA");
    add(0x40,0x74,0x40,54); add(0x40,0x75,0x0d,55);
    unsigned power=slot(0x40,"r74_system_power"); assert(!decoder.used[power]);
    add(0x40,0x76,3,56); expect("r74_system_power","200000 mW");
    assert(decoder.values[power].word==200000 && decoder.values[power].complete);
    add(0x40,0x74,0xff,57); add(0x40,0x75,0xff,58); add(0x40,0x76,0xff,59);
    expect("r74_system_power","16777215 mW");
    add(0x40,0x74,1,60); add(0x40,0x76,2,61); assert(!decoder.values[power].complete);
    add(0x40,0x74,1,62); add(0x40,0x75,2,63,false); add(0x40,0x76,3,64); assert(!decoder.values[power].complete);
    add(0x40,0x74,1,65); add(0x40,0x75,2,1066); add(0x40,0x76,3,1067); assert(!decoder.values[power].complete);
    add(0x40,0x74,1,1070); decoder.gap(); add(0x40,0x75,2,1071); add(0x40,0x76,3,1072); assert(!decoder.values[power].complete);
    add(0x40,0x33,2,1080); expect("r33_charge_voltage","5 V");
    add(0x40,0x33,7,1081); expect("r33_charge_voltage","20 V");
    add(0x40,0x32,0x40,1082); expect("r32_input_mode","High-voltage input");
    add(0x40,0x35,0x40,1083); expect("r35_input_mos","YES");
    add(0x40,0x04,40,1084); expect("r04_fcap","8000 mAh");
    add(0x40,0x30,99,1085); expect("r30_soc","99 %");
    add(0x40,0x77,0,1086); expect("r77_ntc_bias","20 uA");
    add(0x40,0x77,0x80,1087); expect("r77_ntc_bias","80 uA");
    add(0x40,0x03,60,1088); expect("r03_iset","60 code");
    analysis::Selection selection; selection.anyRegister=true; selection.registers[0x75]=true;
    assert(analysis::matches(selection,decoder.values[power]));
    puts("PASS: IP2368 units, distinct status mappings, 24-bit power/full range, missing/failed/expired/gapped components and middle-register selection");
    decoder.clearAddress(0x40); assert(registry.assign(0x40,-1,decoding::Assignment::Disabled));
    registry.observe(0x40,true); assert(registry.bindings[0x40].profile==-1 && registry.bindings[0x40].mode==decoding::Assignment::Disabled);
    registry.reset(); decoder.clear();
    for (unsigned n=0;n<5;++n) assert(registry.assign(0x40+n,ip2366,decoding::Assignment::Manual));
    unsigned before=registry.activeCount(); assert(!registry.assign(0x45,ip2366,decoding::Assignment::Manual));
    assert(registry.activeCount()==before && registry.bindings[0x45].profile==-1);
    registry.reset(); decoder.clear(); Preferences::saved.clear(); Preferences::fail=false;
    decoding::ProfileStorage storage; storage.load(registry);
    assert(registry.assign(0x40,ip2369,decoding::Assignment::Manual)); registry.bindings[0x40].dirty=true;
    storage.save(registry,false); assert(Preferences::saved.empty());
    storage.save(registry,true); assert(Preferences::saved["a40"]=="ip2369" && !registry.bindings[0x40].dirty);
    assert(registry.assign(0x41,-1,decoding::Assignment::Disabled)); registry.bindings[0x41].dirty=true; storage.save(registry,true);
    registry.reset(); storage.load(registry); assert(registry.bindings[0x40].profile==ip2369 && registry.bindings[0x41].mode==decoding::Assignment::Disabled);
    Preferences::saved["a42"]="removed_profile"; storage.load(registry); registry.observe(0x42,true);
    assert(registry.bindings[0x42].missingSaved && registry.nextPrompt()==0x42);
    registry.bindings[0x40].dirty=true; Preferences::fail=true; storage.save(registry,true);
    assert(registry.saveFailed && registry.bindings[0x40].dirty);
    Preferences::fail=false; registry.saveFailed=false; storage.resetPending=true; registry.reset(); storage.save(registry,true);
    assert(Preferences::saved.empty());
    puts("PASS: ambiguous assignment, nonstandard/multiple addresses, independent pairs, profile invalidation, undefined/range values, capacity rejection, persistent IDs, disabled/missing profiles and save failures");
    unsigned booleanCount=0;
    for (int profile:{ip2366,ip2368,ip2369}) {
        registry.reset();
        assert(registry.assign(0x40,profile,decoding::Assignment::Manual));
        for (unsigned i=0;i<decoding::PropertyCount;++i) {
            const auto &p=decoding::properties[i];
            if (!p.count || p.address!=0x40 || !p.bit0) continue;
            assert((p.mask>>p.shift)==1 && p.bit1 && p.meaning0 && p.meaning1);
            ++booleanCount;
            for (unsigned bit=0;bit<2;++bit) {
                analysis::Value v{}; v.property=i; v.complete=true; v.success=true; v.word=bit<<p.shift;
                char heading[192],formatted[192];
                details::value(v,heading,sizeof(heading));
                assert(heading[0]==char('0'+bit) && (heading[1]==0 || heading[1]==' '));
                assert(!strstr(heading,"code"));
                analysis::formatValue(v,formatted,sizeof(formatted));
                const char *expected=bit?p.bit1:p.bit0;
                assert(!strncmp(formatted,expected,strlen(expected)));
            }
        }
    }
    assert(booleanCount>0);
    printf("PASS: both states of %u boolean fields across all three profiles; raw 0/1 detail headings\n",booleanCount);
}
