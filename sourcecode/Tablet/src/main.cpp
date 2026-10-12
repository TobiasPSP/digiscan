// Tablet application composition root. The RP2350 owns bus capture and electrical
// acquisition; this ESP32-S3 owns transport, profile decoding and presentation.
// Touch callbacks schedule bounded jobs. pump() advances commands/acknowledgements
// and loop() services LVGL, avoiding long bus operations inside event callbacks.
// TABLET_PREVIEW substitutes hardware only: the same widgets/models render in
// native regression fixtures. It is not an alternate simplified mock interface.
#ifdef TABLET_PREVIEW
#include "preview_platform.h"
#else
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <esp_random.h>
#endif
#include <lvgl.h>
#include <new>
#include <atomic>
#include <initializer_list>
#include "observations.h"
#include "analysis.h"
#include "discovery.h"
#include "recorder.h"
#include "discovery_details.h"
#include "active_plan.h"
#include "profile_storage.h"
#include "mono_font.h"
#include "detail_text.h"
#include "ui_update.h"
#include "transport_v5.h"
#include "highlight.h"
#include "presentation_clock.h"
#include "generated_reports.h"
#include "report_batch.h"
#include "generated_experiments.h"
#include "manual_read.h"
#include "profile_disk.h"

namespace {
constexpr int W=320, H=480;
constexpr uint32_t Bg=0x141719, Ink=0xe1e8eb, Muted=0x84949c;
constexpr uint32_t Cyan=0x60d4e3, Green=0x89e5ad, Amber=0xffcd70, Red=0xff7c81;
// Arduino_GFX 1.5.9 constructor: CS, SCK, D0, D1, D2, D3. These are internal
// display signals on JC3248W535C_I_Y, not the external DUT probe/UART connector.
Arduino_ESP32QSPI bus(45,47,21,48,40,39);
Arduino_AXS15231B panel(&bus,GFX_NOT_DEFINED,0,false,W,H);
HardwareSerial uart(1);
bringup::Receiver receiver;
observer::Control control;
observer::Store *store;
decoding::Registry registry;
decoding::ProfileStorage profileStorage;
analysis::Decoder decoder;
analysis::Decoder *discoveryDecoder=nullptr;
discovery::Session *discover=nullptr;
bool discoveryGatesMonitor();
recording18::Session *recorder=nullptr;
recording18::PostCapture *recorderPost=nullptr;
recorder_wire::Timing currentTiming;
bool haveCurrentTiming=false;
bool recorderBusy();
bool recorderAcquiring();
bool recorderWatching();
bool recorderControl();
void recorderGap(bool expectedPause=false);
void recorderTransportGap();
void recorderObserve(const observer::Observation &o);
bool recorderPacket(const bringup::Packet &p);
void recorderTick();
void refreshRecorder();
void createRecorder();
analysis::Decoder &monitorDecoder() { return discoveryGatesMonitor()?*discoveryDecoder:decoder; }
void decoderGap(bool expectedPause=false) { decoder.gap(); if(discoveryDecoder) discoveryDecoder->gap(); if(discover)discover->gap(); recorderGap(expectedPause); }
void receiveTransaction(const uint8_t *payload,size_t length);
void discoveryTick();
void cancelDiscoveryPrompt();
void sourceFilter(const analysis::Selection &candidate);
analysis::Selection selection=[] { analysis::Selection s; if (decoding::PropertyCount) s.uniqueOrder=analysis::UniqueOrder::LatestChange; return s; }();
analysis::Sequence sequence;
presentation::Clock visualClock;
uint16_t *fb;
uint8_t *draw;
void *lvglPool;
char *detailText;
uint8_t *transactionWire;
bool uartReady=false, touchReady=false, seen=false, delivering=false, haveEpoch=false, linkWasOnline=false;
bool sessionReady=false, needsSync=true;
bool busConfirmed=false, startupCapturePending=true, sessionMutation=false;
void startTick();
void refreshStart();
void startSettings(lv_event_t *);
void newSession(lv_event_t *);
void startLink(lv_event_t *);
uint32_t activePending=0,activeAt_ms=0,activeGeneration=0;
uint32_t activeSession=0;
acquisition::Batch reportBatch;
bool reportBatchDispatch=false;
uint32_t extraPending=0;
bool extraSessionBusy=false,graphDispatch=false;
bool profileUploadBusy=false;
enum class BurstOwner { None, Reports, Graph };
BurstOwner burstOwner=BurstOwner::None;
uint32_t burstPending=0,burstAt=0,burstGeneration=0;
unsigned burstProperties[512]={},burstCount=0,burstNext=0,burstChunk=0,burstReceived=0;
bool burstCancel=false,burstCancelSent=false;
bool activeBusy() { return profileUploadBusy || burstOwner!=BurstOwner::None || activePending || reportBatch.running || extraPending || extraSessionBusy || recorderBusy(); }
bool burstPacket(const bringup::Packet &);
bool burstStart(BurstOwner,const unsigned *,unsigned);
void burstTick();
void burstStop();
void graphBurstDone(bool);
bool extraPacket(const bringup::Packet &packet);
void extraTick();
void createExtras();
void refreshExtras();
void graphLinkLost();
bool graphSourceFilter(uint8_t *command);
void reportBatchTick();
void reportBatchResult(bool ok,bool safe);
unsigned activeProperty=0;
unsigned activeRequestedProperty=0;
decoding::Property activeDefinition={};
bool activeRefreshDetail=false;
lv_obj_t *activeLabel=nullptr;
lv_obj_t *activeButtonCaption=nullptr;
char activeMessage[192]="Tap Read for one active acquisition. Capture state is restored afterward.";
void activePacket(const bringup::Packet &packet);
void activeUiTick();
uint32_t statusAt=0, touchBus=0, touchBounds=0, rxOverflow=0, startupRxErrors=0;
std::atomic<uint32_t> pendingUartErrors{0};
std::atomic<uint32_t> pendingErrorKinds[6];
uint32_t errorKinds[6]={}, transitionErrors=0, transitionBad=0, resetCount=0;
uint32_t tabletActualBaud=115200;
const char *rateStage="IDLE", *rateFailure="NONE";
uint8_t status[200]={};
uint32_t uartBaud=115200, rateRequest=0, rateToken=0, rateAt_ms=0, rateTarget=115200, keepaliveAt_ms=0;
bool rateConfirm=false, awaitingBaudBeacon=false, transportRecovering=false;
bool defaultBaudAttempted=false;
bool requestBaud(uint32_t target);
uint8_t rxChunk[256]; size_t rxAt=0, rxCount=0;
enum class View { Sniff, Unique, Diag, Discover };
View view=View::Unique, rendered=View::Diag;
bool discoveryGatesMonitor() {
    return discover && discover->enabled && discover->hasDecisions();
}
bool uniqueView() { return view==View::Unique || view==View::Discover; }
bool discoveryDialog=false, discoveryPauseOwned=false, discoveryPausedVerified=false;
bool discoveryResume=false, clearDiscoveryRequested=false;
bool discoveryDeferred=false;
uint32_t discoveryQueryStatus=0;
lv_obj_t *speed, *linkIcon, *state, *footer, *list, *diag, *modal=nullptr;
lv_obj_t *controls[2], *viewChoice, *runLabel, *rows[128], *labels[128];
lv_obj_t *ranks[128], *values[128], *headings[2], *filterButton;
lv_obj_t *addressInput, *registerInput, *readCheck, *writeCheck, *displayChoice, *orderChoice, *filterError, *keyboard;
lv_obj_t *unkeyedCheck, *outcomeChoice, *baudChoice, *factorChoice;
constexpr double sensitivityFactors[]={0,0.1,0.25,0.5,1,2,5,10,20,50,100};
lv_obj_t *focusedInput=nullptr, *replaceInput=nullptr;
bool inputJustFocused=false, inputReplacing=false;
char addressText[256]={}, registerText[768]={};
const observer::Observation *visibleRaw[128]={};
const analysis::Value *visibleDecoded[128]={};
bool visibleRecent[128]={};
unsigned visibleCount=0;
uint32_t statusPackets=0, clearQueryStatus=0;
uint32_t syncQueryStatus=0;
struct FieldUi { lv_obj_t *background=nullptr, *name=nullptr, *value=nullptr; unsigned property=0; };
struct GroupUi { lv_obj_t *root=nullptr; unsigned count=0; uint32_t signature=0; FieldUi fields[8]; };
GroupUi groups[128];
uint32_t revision=0;
bool touchHeld=false, rowHeld=false, pressedValid=false, pressedRaw=false;
unsigned pressedIndex=0;
analysis::Value pressedValue;
uint32_t listPasses=0, fieldRootsCreated=0;
analysis::Selection renderedSelection;
bool listInitialized=false, renderedActive=false;
lv_obj_t *monitorScreen=nullptr,*reportScreen=nullptr;
lv_obj_t *extraScreens[3]={};
lv_obj_t *readScreen=nullptr;
lv_obj_t *recorderScreen=nullptr;
unsigned screenIndex=0;
lv_obj_t *reportChoice=nullptr;
reporting::Cache reportCache;
reporting::Swipe swipe;
bool reportActive=false, sliding=false;
void navigateReport(bool next);
void refreshReports();
void createReports();
void reportSettings(lv_event_t *);
void reportPolicy();
void invalidateReports();
bool dropdownOpen() { return (viewChoice && lv_dropdown_is_open(viewChoice)) || (reportChoice && lv_dropdown_is_open(reportChoice)); }
uint32_t listAt_ms=0, listWait_ms=0;
uint32_t groupAt_ms=0;
uint32_t highlightAt_ms=0;
bool groupedOnce=false, decodedRecent[decoding::PropertyCount]={}, rawRecent[observer::UniqueCapacity]={};
bool decodedPublished[decoding::PropertyCount]={};
unsigned rawPublishedCount=0;
bool sameSelection(const analysis::Selection &a,const analysis::Selection &b) {
    return a.anyAddress==b.anyAddress && a.anyRegister==b.anyRegister && a.read==b.read && a.write==b.write &&
        a.display==b.display && a.uniqueOrder==b.uniqueOrder && a.changeFactor==b.changeFactor && a.includeUnkeyed==b.includeUnkeyed && a.outcome==b.outcome &&
        !memcmp(a.addresses,b.addresses,sizeof(a.addresses)) && !memcmp(a.registers,b.registers,sizeof(a.registers));
}
void updateVisualClock() {
    const bool pauseRequested=control.busy() && (control.action==observer::Action::Pause || control.action==observer::Action::DiscoverPause);
    visualClock.setRunning(status[0] && !pauseRequested,millis());
}
uint32_t visualNow_ms() { updateVisualClock(); return visualClock.now(millis()); }
void scheduleVisualExpiry(uint32_t timestamp_ms,uint32_t duration_ms) {
    uint32_t age_ms=uint32_t(visualNow_ms()-timestamp_ms);
    if (age_ms<duration_ms && duration_ms-age_ms<listWait_ms) listWait_ms=duration_ms-age_ms;
}
bool connected() { return seen && uint32_t(millis()-statusAt)<(uartBaud==115200?8000u:3000u); }
uint32_t val(unsigned offset) { return bringup::le32(status+offset); }
uint32_t tick_ms() { return millis(); }
void clearTabletDiagnostics() {
    receiver.clearDiagnostics();
    sequence.gaps=sequence.startupGaps=sequence.resets=0; sequence.synced=false;
    touchBus=touchBounds=rxOverflow=startupRxErrors=statusPackets=0;
    transitionErrors=transitionBad=0; pendingUartErrors.exchange(0);
    for (unsigned i=0;i<6;++i) { errorKinds[i]=0; pendingErrorKinds[i].exchange(0); }
    rateStage="IDLE"; rateFailure="NONE"; ++resetCount;
}
void fatal(const char *why) { for (;;) { Serial.printf("FATAL: %s\n",why); delay(2000); } }
void flush(lv_display_t *d,const lv_area_t *a,uint8_t *pixels) {
    int w=a->x2-a->x1+1, h=a->y2-a->y1+1;
    for (int y=0;y<h;++y) memcpy(fb+(a->y1+y)*W+a->x1,pixels+y*w*2,w*2);
    if (lv_display_flush_is_last(d)) panel.draw16bitRGBBitmap(0,0,fb,W,H);
    lv_display_flush_ready(d);
}
void touch(lv_indev_t *,lv_indev_data_t *data) {
    if(profileUploadBusy || sessionMutation){data->state=LV_INDEV_STATE_RELEASED;return;}
    static int lastX=0,lastY=0;
    static bool previousPressed=false;
    data->state=LV_INDEV_STATE_RELEASED;
    touchHeld=false;
    if (!touchReady) return;
    static const uint8_t cmd[11]={0xb5,0xab,0xa5,0x5a,0,0,0,8,0,0,0};
    Wire.beginTransmission(0x3b); Wire.write(cmd,sizeof(cmd));
    uint8_t result=Wire.endTransmission(true);
    size_t n=result==0?Wire.requestFrom(uint8_t(0x3b),size_t(8),true):0;
    if (result || n!=8) {
        ++touchBus; while (Wire.available()) Wire.read();
        previousPressed=false; swipe.down=false; swipe.horizontal=true; pressedValid=rowHeld=false;
        return;
    }
    uint8_t raw[8]; for (auto &b:raw) b=Wire.read();
    bringup::TouchPoint p;
    if (!bringup::parseTouch(raw,8,p)) {
        ++touchBounds; previousPressed=false; swipe.down=false; swipe.horizontal=true; pressedValid=rowHeld=false;
        return;
    }
    if (p.pressed) {
        if (!previousPressed) swipe=reporting::Swipe{};
        lastX=p.x_px;lastY=p.y_px;
        if ((!modal || screenIndex==0) && !dropdownOpen() && !sliding) {
            if (!swipe.down && lastY>=(reportActive?124:132)) swipe.start(lastX,lastY); else swipe.move(lastX,lastY);
            if (swipe.horizontal) { pressedValid=false; rowHeld=false; }
        }
        touchHeld=true; data->state=LV_INDEV_STATE_PRESSED; data->point.x=p.x_px; data->point.y=p.y_px;
    } else if (swipe.down) {
        int direction=swipe.end(lastX,lastY);
        if (direction && (!modal || screenIndex==0) && !dropdownOpen() && !sliding) navigateReport(direction>0);
    }
    previousPressed=p.pressed;
}
void pump() {
    if (!uartReady) return;
    const uint32_t rateTimeout_ms=!rateConfirm && uartBaud==115200?8000u:2500u;
    if ((rateRequest && uint32_t(millis()-rateAt_ms)>rateTimeout_ms) ||
        (uartBaud!=115200 && seen && uint32_t(millis()-statusAt)>3000)) {
        if (!strcmp(rateFailure,"NONE")) rateFailure=rateRequest?(rateConfirm?(awaitingBaudBeacon?"NO NEW-RATE BEACON":"NO CONFIRM ACK"):"NO OLD-RATE ACK"):"STATUS TIMEOUT";
        rateStage="FALLBACK / SYNC";
        uart.flush(false); uart.updateBaudRate(115200); uart.flush(false);
        uartBaud=115200; tabletActualBaud=uart.baudRate(); rateRequest=0; rateConfirm=false; awaitingBaudBeacon=false;
        if (baudChoice) lv_dropdown_set_selected(baudChoice,0);
        receiver.reconnect(); decoderGap(); sessionReady=false; delivering=false; needsSync=true;
        rxAt=rxCount=0;
        transportRecovering=true;
        control.abort("UART FALLBACK / SYNCING");
    }
    uint32_t errors=pendingUartErrors.exchange(0);
    for (unsigned i=0;i<6;++i) errorKinds[i]+=pendingErrorKinds[i].exchange(0);
    if (errors) {
        if (rateRequest || transportRecovering) transitionErrors+=errors;
        if (sequence.synced) rxOverflow+=errors; else startupRxErrors+=errors;
        decoderGap(); receiver.inputGap();
        if (!rateRequest && !transportRecovering) {
            control.abort("UART RX ERROR / CHECK DIAG"); sessionReady=false; delivering=false;
            if (seen && linkWasOnline) needsSync=false;
        }
    }
    if (linkWasOnline && !connected()) {
        graphLinkLost();
        sequence.reconnect(); decoderGap(); receiver.reconnect(); linkWasOnline=false;
        sessionReady=false; delivering=false; needsSync=true;
        control.abort("WAITING FOR SNIFFER");
    }
    bringup::Packet p; unsigned budget=8192;
    while (budget-- && (rxAt<rxCount || uart.available())) {
        if (rxAt==rxCount) {
            size_t count=size_t(uart.available()); if (count>sizeof(rxChunk)) count=sizeof(rxChunk);
            rxCount=uart.read(rxChunk,count); rxAt=0; if (!rxCount) break;
        }
        uint32_t badBefore=receiver.bad;
        bool accepted=receiver.feed(rxChunk[rxAt++],p);
        if (rateRequest || transportRecovering) transitionBad+=receiver.bad-badBefore;
        if (receiver.bad!=badBefore) {decoderGap();recorderTransportGap();}
        if (!accepted) continue;
        uint32_t before=sequence.resets;
        if (sequence.packet(p.sequence)) {decoderGap();recorderTransportGap();}
        if (sequence.resets!=before) {
            graphLinkLost();
            delivering=false; sessionReady=false; needsSync=true;
            control.abort("SNIFFER RESTARTED / SYNCING");
        }
        if (burstPacket(p)) continue;
        if (recorderPacket(p)) continue;
        if (extraPacket(p)) continue;
        if (p.type==6 || (p.type==2 && activePending && bringup::le32(p.payload)==activePending && (p.payload[4]==13 || p.payload[4]==15))) {
            activePacket(p);continue;
        }
        if (p.type==3) {
            // Capture epochs mark continuity breaks (including active-read restore),
            // not a request to erase completed observations or report readings.
            if (haveEpoch && bringup::le32(p.payload+4)!=val(4)) decoderGap(!p.payload[0]);
            haveEpoch=true; memset(status,0,sizeof(status)); memcpy(status,p.payload,p.length); seen=true; statusAt=millis(); ++statusPackets;
            if (p.length==200 && val(136)>=114000 && val(136)<=116400 && uartBaud==115200) transportRecovering=false;
            linkWasOnline=true;
            if (rateRequest && rateConfirm && awaitingBaudBeacon && p.length==200) {
                const uint32_t actual=val(136), error=actual>rateTarget?actual-rateTarget:rateTarget-actual;
                if (uint64_t(error)*100<=rateTarget && uart.availableForWrite()>=32) {
                    uint8_t command[5]={11}, wire[32]; bringup::put32(command+1,rateToken);
                    rateRequest=++control.nextId; if (!rateRequest) rateRequest=++control.nextId;
                    size_t n=bringup::command(rateRequest,command,5,wire,sizeof(wire));
                    uart.write(uint8_t(0)); uart.write(wire,n); rateAt_ms=millis(); awaitingBaudBeacon=false;
                    rateStage="BEACON / WAIT CONFIRM ACK";
                }
            }
            if (!status[0]) decoderGap(true);
            if (needsSync && !control.busy() && !rateRequest && !activeBusy() && !status[2]) {
                if (p.length!=200 || (val(132)&15)!=15) { needsSync=false; control.abort("V6 SNIFFER REQUIRED"); continue; }
                needsSync=false; control.begin(observer::Action::Synchronize);
            }
        } else if (p.type==2) {
            if (rateRequest && bringup::le32(p.payload)==rateRequest && p.payload[4]==(rateConfirm?11:10)) {
                if (p.payload[5]) { rateRequest=0; rateConfirm=false; rateStage="REJECTED"; rateFailure="PEER REJECTED"; control.abort("UART RATE REJECTED"); continue; }
                if (!rateConfirm) {
                    rateToken=rateRequest;
                    // RP2350 guarantees a quiet interval after draining its old-rate ACK.
                    // flush(false) waits for local TX and clears the ESP driver RX/FIFO.
                    uart.flush(false); uart.updateBaudRate(rateTarget); uart.flush(false); uartBaud=rateTarget;
                    rxAt=rxCount=0;
                    const uint32_t actual=uart.baudRate(), error=actual>rateTarget?actual-rateTarget:rateTarget-actual;
                    tabletActualBaud=actual;
                    if (uint64_t(error)*100>rateTarget) { rateAt_ms=millis()-8001; rateFailure="LOCAL BAUD MISMATCH"; continue; }
                    receiver.reconnect(); rateConfirm=true;
                    rateStage="OLD ACK / WAIT BEACON";
                    awaitingBaudBeacon=true; rateAt_ms=millis();
                } else {
                    rateRequest=0; rateConfirm=false; keepaliveAt_ms=millis(); rateStage="CONFIRMED"; control.message="UART RATE CONFIRMED";
                    if (baudChoice) lv_dropdown_set_selected(baudChoice,uartBaud==2000000?2:uartBaud==1000000?1:0);
                }
                continue;
            }
            if (control.pendingId && bringup::le32(p.payload)==control.pendingId && control.opcode==5 && p.payload[4]==5 && !p.payload[5])
                delivering=control.action==observer::Action::Enable || control.action==observer::Action::Clear || control.action==observer::Action::Synchronize ||
                    (control.action==observer::Action::Configure && control.step==2);
            bool matched=control.pendingId && bringup::le32(p.payload)==control.pendingId && p.payload[4]==control.opcode;
            bool synchronizing=control.action==observer::Action::Synchronize;
            if (matched && synchronizing && control.opcode==4 && !p.payload[5] &&
                (statusPackets==syncQueryStatus || status[0] || !status[3])) {
                delivering=false; control.abort("SYNC UNVERIFIED / TAP LINK"); continue;
            }
            if (matched && control.action==observer::Action::Clear && control.opcode==4 && !p.payload[5] &&
                (statusPackets==clearQueryStatus || status[0])) {
                control.abort("CLEAR STATUS UNCERTAIN / PAUSED"); continue;
            }
            bool discoveryAck=matched && control.action==observer::Action::DiscoverPause;
            if(discoveryAck && control.opcode==4 && !p.payload[5] &&
                (statusPackets==discoveryQueryStatus || status[0])) {
                control.abort("DISCOVER PAUSE UNVERIFIED / TAP RUN"); discoveryResume=false; continue;
            }
            uint32_t cleared=control.clearsAcknowledged;
            if (control.reply(p)) Serial.printf("CONTROL: %s\n",control.message);
            if(discoveryAck && !control.busy()) {
                discoveryPausedVerified=!control.uncertain;
                if(control.uncertain)discoveryResume=false;
            }
            if (matched && synchronizing && !control.busy()) {
                sessionReady=!control.uncertain;
                delivering=sessionReady && status[3];
            }
            if (control.clearsAcknowledged!=cleared) {
                store->clear(); decoder.clear(); discoveryDecoder->clear(); sequence.synced=false; sequence.gaps=0;
                if(clearDiscoveryRequested)discover->clear();
                clearDiscoveryRequested=false; discoveryResume=false; discoveryPauseOwned=false; discoveryPausedVerified=false;
                discoveryDeferred=false;
                groupedOnce=false; listInitialized=false;
                clearTabletDiagnostics();
                lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
            }
        } else if(p.type==7 && delivering && sessionReady){
            recorder_wire::Timing timing;
            bool valid=recorder_wire::read(p.payload,p.length,timing);
            size_t prefix=recorder_wire::Header+16*timing.count;
            size_t n=valid?transport5::unpack(p.payload+prefix,p.length-prefix,transactionWire,bringup::MaxPayload):0;
            if(!n || !observer::parse(transactionWire,n,store->incoming) || store->incoming.raw[8]!=timing.count){++receiver.bad;decoderGap();continue;}
            currentTiming=timing;haveCurrentTiming=true;
            receiveTransaction(transactionWire,n);haveCurrentTiming=false;
        } else if (p.type==4 && delivering && sessionReady) {
            receiveTransaction(p.payload,p.length);
        } else if (p.type==5 && delivering && sessionReady) {
            // Validate the entire bundle before delivering any record. A corrupt
            // trailing record must not turn a partial bundle into apparently valid data.
            size_t at=0; bool valid=p.length!=0;
            while (valid && at<p.length) {
                if (p.length-at<2) { valid=false; break; }
                size_t record=bringup::le16(p.payload+at); at+=2;
                if (record>p.length-at) { valid=false; break; }
                size_t n=transport5::unpack(p.payload+at,record,transactionWire,bringup::MaxPayload);
                if (!n || !observer::parse(transactionWire,n,store->incoming)) { valid=false; break; }
                at+=record;
            }
            if (!valid) { ++receiver.bad; ++store->malformed; decoderGap(); continue; }
            at=0;
            while (at<p.length) {
                size_t record=bringup::le16(p.payload+at); at+=2;
                size_t n=transport5::unpack(p.payload+at,record,transactionWire,bringup::MaxPayload); at+=record;
                receiveTransaction(transactionWire,n);
            }
        }
    }
    control.timeout_ms=uartBaud==115200?7000u:2500u;
    activeUiTick();
    extraTick();
    recorderTick();
    control.tick(millis());
    discoveryTick();
    if (control.uncertain && !control.busy() && !sessionReady) delivering=false;
    if (control.busy() && !connected()) { control.abort("LINK LOST / STATE UNCERTAIN"); return; }
    if (!rateRequest && (!activeBusy() || recorderControl() || (profileUploadBusy && control.action==observer::Action::Pause)) && connected() && control.busy() && !control.pendingId && uart.availableForWrite()>=80) {
        uint8_t wire[96]; size_t n=control.prepare(millis(),wire,sizeof(wire));
        if (n && control.action==observer::Action::Clear && control.opcode==4) clearQueryStatus=statusPackets;
        if (n && control.action==observer::Action::DiscoverPause && control.opcode==4) discoveryQueryStatus=statusPackets;
        if (n && control.action==observer::Action::Synchronize && control.opcode==4) syncQueryStatus=statusPackets;
        if (n) { uart.write(uint8_t(0)); if (uart.write(wire,n)!=n) control.abort("SHORT UART WRITE / CHECK DIAG"); }
    }
    if (uartBaud!=115200 && !rateRequest && uint32_t(millis()-keepaliveAt_ms)>=1000 && uart.availableForWrite()>=32) {
        uint8_t command[1]={12}, wire[32]; uint32_t id=++control.nextId;
        size_t n=bringup::command(id,command,1,wire,sizeof(wire)); uart.write(wire,n); keepaliveAt_ms=millis();
    }
    // Rendezvous and verified pause first. One automatic attempt per tablet boot;
    // recovery never silently resumes capture or starts a negotiation loop.
    if (!defaultBaudAttempted && !activeBusy() && sessionReady && connected() && !control.busy() && !rateRequest && !status[0] && !status[2]) {
        if (uartBaud==2000000 || requestBaud(2000000)) defaultBaudAttempted=true;
    }
    startTick();
}
void flat(lv_obj_t *o,uint32_t color) {
    lv_obj_remove_style_all(o); ui_update::bgColor(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
}
lv_obj_t *text(lv_obj_t *parent,int x,int y,int w,const char *s,const lv_font_t *font=&lv_font_montserrat_14) {
    auto *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    ui_update::font(o,font,0); ui_update::textColor(o,lv_color_hex(Ink),0); ui_update::text(o,s); return o;
}
void closeDetail(lv_event_t *) { if (discoveryDialog) cancelDiscoveryPrompt(); if (modal) { lv_obj_delete(modal); modal=nullptr; keyboard=nullptr; activeRefreshDetail=false; activeLabel=activeButtonCaption=nullptr; focusedInput=replaceInput=nullptr; inputJustFocused=false; } }
lv_obj_t *button(lv_obj_t *,int,int,int,const char *,lv_event_cb_t,uintptr_t);
void openProfile(uint8_t address);
int profileDetails(lv_obj_t *body,int y,uint8_t address);
int activeReadDetails(lv_obj_t *body,int y,const analysis::Value &value);
#include "instrument_ui.h"
#include "profile_ui.h"
#include "active_ui.h"
void rowTouch(lv_event_t *e) {
    auto code=lv_event_get_code(e);
    if (code==LV_EVENT_PRESS_LOST) { rowHeld=false; pressedValid=false; return; }
    // LVGL 9.2.2 sends RELEASED before CLICKED. Keep the snapshot until CLICKED.
    if (code==LV_EVENT_RELEASED) { rowHeld=false; return; }
    if (code!=LV_EVENT_PRESSED) return;
    rowHeld=true; pressedValid=false;
    unsigned index=unsigned(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (index>=visibleCount || modal) return;
    pressedIndex=index;
    if (visibleRaw[index]) { store->detail=*visibleRaw[index]; pressedRaw=true; }
    else if (visibleDecoded[index]) { pressedValue=*visibleDecoded[index]; pressedRaw=false; }
    else return;
    pressedValid=true;
}
void openDetail(lv_event_t *e) {
    unsigned index=unsigned(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (!pressedValid || index!=pressedIndex || modal || swipe.horizontal || sliding) return;
    pressedValid=false; rowHeld=false;
    if (!pressedRaw) { decodedDetail(pressedValue); return; }
    rawDetail(store->detail);
}
void capture(lv_event_t *e) {
    if (activeBusy() || discoveryDialog) return;
    if (swipe.horizontal || sliding) return;
    decoderGap();
    if (!connected() || !sessionReady || status[2] || rateRequest) return;
    bool clear=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))==1;
    if(control.begin(clear?observer::Action::Clear:status[0]?observer::Action::Pause:observer::Action::Enable,status[0])) {
        if(clear)clearDiscoveryRequested=view==View::Discover;
        discoveryResume=false; discoveryPauseOwned=false; discoveryPausedVerified=false;
        discoveryDeferred=false;
    }
    updateVisualClock();
}
void chooseView(lv_event_t *e) {
    view=static_cast<View>(lv_dropdown_get_selected(lv_event_get_target_obj(e)));
    if(view==View::Discover)discoveryDeferred=false;
    if(view==View::Discover && (!discover->enabled || !discover->hasDecisions())) {
        discover->enabled=true;
        store->uniqueCount=store->historyCount=store->historyHead=0; ++store->revision;
        discoveryDecoder->clear(); discoveryDecoder->setChangeFactor(selection.changeFactor);
        groupedOnce=false;listInitialized=false;
    }
    if(view!=View::Discover) { discoveryResume=false; discoveryPauseOwned=false; discoveryPausedVerified=false; } lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
}
void linkDetails(lv_event_t *) {
    discoveryResume=false;
    if (connected() && !sessionReady && !control.busy() && !status[2]) {
        needsSync=false; delivering=false; decoderGap();
        control.begin(observer::Action::Synchronize);
    }
    view=View::Diag; lv_dropdown_set_selected(viewChoice,2);
}
lv_obj_t *button(lv_obj_t *parent,int x,int y,int w,const char *s,lv_event_cb_t cb,uintptr_t id) {
    auto *o=lv_button_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,40);
    lv_obj_set_style_radius(o,3,0); lv_obj_set_style_shadow_width(o,0,0);
    ui_update::bgColor(o,lv_color_hex(0x2c363b),0); lv_obj_set_style_border_width(o,1,0);
    ui_update::borderColor(o,lv_color_hex(0x47565e),0); ui_update::font(o,&lv_font_montserrat_14,0);
    auto *l=lv_label_create(o); ui_update::text(l,s); lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,reinterpret_cast<void *>(id));
    return o;
}
void focusInput(lv_event_t *e) {
    auto *input=lv_event_get_target_obj(e); auto code=lv_event_get_code(e);
    if (code==LV_EVENT_INSERT) {
        if (replaceInput==input && !inputReplacing) {
            replaceInput=nullptr; inputReplacing=true;
            lv_textarea_clear_selection(input); lv_textarea_set_text(input,""); inputReplacing=false;
        }
        return;
    }
    if (code==LV_EVENT_FOCUSED) {
        if (focusedInput && focusedInput!=input) lv_textarea_clear_selection(focusedInput);
        focusedInput=input; replaceInput=input; inputJustFocused=true;
        lv_textarea_set_placeholder_text(input,"");
        lv_keyboard_set_textarea(keyboard,input);
    } else if (code==LV_EVENT_CLICKED && focusedInput==input) {
        if (!inputJustFocused) { replaceInput=nullptr; lv_textarea_clear_selection(input); }
        else {
            auto *label=lv_textarea_get_label(input);
            lv_label_set_text_selection_start(label,0);
            lv_label_set_text_selection_end(label,uint32_t(strlen(lv_textarea_get_text(input))));
        }
        inputJustFocused=false;
    } else if (code==LV_EVENT_DEFOCUSED) {
        lv_textarea_clear_selection(input); lv_textarea_set_placeholder_text(input,"ALL");
        if (focusedInput==input) focusedInput=replaceInput=nullptr;
    }
}
void clearInput(lv_event_t *e) {
    auto *input=static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    lv_textarea_clear_selection(input); lv_textarea_set_text(input,"");
    lv_textarea_set_placeholder_text(input,"ALL"); replaceInput=nullptr;
}
void sourceFilter(const analysis::Selection &candidate) {
    // Build the union required by independent consumers, not merely the visible
    // page. START/session reset deliberately requests unrestricted discovery.
    // Filter setup is acknowledged before capture resumes; never assume a sent
    // request changed peer state until its response/status confirms it.
    uint8_t *p=control.filterCommand; memset(p,0,52);
    p[0]=8; p[1]=8|(candidate.anyAddress?1:0)|(candidate.anyRegister?2:0)|(candidate.includeUnkeyed?4:0);
    p[2]=(candidate.read?1:0)|(candidate.write?2:0); p[3]=candidate.outcome;
    for (unsigned a=0;a<128;++a) if (candidate.addresses[a]) p[4+a/8]|=uint8_t(1u<<(a%8));
    for (unsigned r=0;r<256;++r) if (candidate.registers[r]) p[20+r/8]|=uint8_t(1u<<(r%8));
    if (candidate.anyRegister) for (const auto &property:decoding::properties) {
        bool selected=false;
        for (unsigned n=0;n<property.count;++n) selected|=candidate.reg(property.low+n);
        if (property.count>1 && selected) {
            for (unsigned n=0;n<property.count;++n) { unsigned reg=property.low+n; p[20+reg/8]|=uint8_t(1u<<(reg%8)); }
        }
    }
    // Reports are an independent enabled consumer. Union their reads/results
    // with the monitor request; never mutate the monitor's display selection.
    bool reports=false;
    for (unsigned a=8;a<0x78;++a) if (registry.bindings[a].profile>=0) {
        const auto &profile=decoding::profiles[registry.bindings[a].profile];
        bool pages=false;
        for (const auto &page:reporting::pages) pages|=!strcmp(page.profileId,profile.id);
        if (!pages) continue;
        reports=true; p[4+a/8]|=uint8_t(1u<<(a%8));
        for (const auto &property:decoding::properties) if (property.count && property.address==a)
            for (unsigned n=0;n<property.count;++n) { unsigned r=property.low+n; p[20+r/8]|=uint8_t(1u<<(r%8)); }
    }
    bool graph=graphSourceFilter(p);
    if (reports || graph) { p[2]|=1; p[3]=0; }
    if (!(p[1]&1)) memset(p+4,0,16);
    if (!(p[1]&2)) memset(p+20,0,32);
    if(recorderAcquiring()){p[1]=8|4;p[2]=3;p[3]=0;memset(p+4,0,48);}
    if(recorderWatching()){
        p[1]=8|1|2;p[2]=3;p[3]=0;memset(p+4,0,48);
        for(unsigned a=8;a<120;++a)for(unsigned r=0;r<256;++r)if(recorderPost->includes(uint8_t(a),uint8_t(r))){
            p[4+a/8]|=uint8_t(1u<<(a%8));p[20+r/8]|=uint8_t(1u<<(r%8));
        }
    }
    // START is an independent discovery consumer, never limited by Monitor.
    if(screenIndex==3 || !busConfirmed || sessionMutation){p[1]=8|4;p[2]=3;p[3]=0;memset(p+4,0,48);}
}
void applyFilter(lv_event_t *) {
    if (control.busy() || rateRequest || activeBusy()) { ui_update::text(filterError,"Instrument operation pending"); return; }
    analysis::Selection candidate;
    if (!analysis::parseList(lv_textarea_get_text(addressInput),candidate.addresses,128,candidate.anyAddress)) {
        ui_update::text(filterError,"Invalid address list (00-7F)"); return;
    }
    if (!analysis::parseList(lv_textarea_get_text(registerInput),candidate.registers,256,candidate.anyRegister)) {
        ui_update::text(filterError,"Invalid register list (00-FF)"); return;
    }
    candidate.read=lv_obj_has_state(readCheck,LV_STATE_CHECKED); candidate.write=lv_obj_has_state(writeCheck,LV_STATE_CHECKED);
    if (!candidate.read && !candidate.write) { ui_update::text(filterError,"Select Read and/or Write"); return; }
    candidate.display=static_cast<analysis::Display>(lv_dropdown_get_selected(displayChoice));
    candidate.uniqueOrder=static_cast<analysis::UniqueOrder>(lv_dropdown_get_selected(orderChoice));
    candidate.changeFactor=sensitivityFactors[lv_dropdown_get_selected(factorChoice)];
    candidate.outcome=uint8_t(lv_dropdown_get_selected(outcomeChoice));
    candidate.includeUnkeyed=lv_obj_has_state(unkeyedCheck,LV_STATE_CHECKED);
    selection=candidate;
    decoder.setChangeFactor(candidate.changeFactor); discoveryDecoder->setChangeFactor(candidate.changeFactor); groupedOnce=false;
    sourceFilter(candidate); decoderGap();
    if (sessionReady && connected()) control.begin(observer::Action::Configure);
    snprintf(addressText,sizeof(addressText),"%s",lv_textarea_get_text(addressInput));
    snprintf(registerText,sizeof(registerText),"%s",lv_textarea_get_text(registerInput));
    closeDetail(nullptr); lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);
}
void resetFilter(lv_event_t *) {
    lv_textarea_set_text(addressInput,""); lv_textarea_set_text(registerInput,"");
    lv_obj_add_state(readCheck,LV_STATE_CHECKED); lv_obj_add_state(writeCheck,LV_STATE_CHECKED);
    lv_dropdown_set_selected(displayChoice,unsigned(analysis::Selection().display)); ui_update::text(filterError,"");
    lv_dropdown_set_selected(orderChoice,unsigned(decoding::PropertyCount?analysis::UniqueOrder::LatestChange:analysis::UniqueOrder::LatestTransaction));
    lv_dropdown_set_selected(factorChoice,0);
    lv_dropdown_set_selected(outcomeChoice,0); lv_obj_add_state(unkeyedCheck,LV_STATE_CHECKED);
}
void openFilter(lv_event_t *) {
    if (modal) return;
    modal=lv_obj_create(lv_screen_active()); flat(modal,0x1e2528);
    lv_obj_set_pos(modal,0,0); lv_obj_set_size(modal,W,H); lv_obj_remove_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    text(modal,8,12,235,"FILTER",&lv_font_montserrat_18);
    auto *close=button(modal,266,4,48,LV_SYMBOL_CLOSE,closeDetail,0);
    (void)close;
    text(modal,8,42,300,"Addresses / HEX");
    addressInput=lv_textarea_create(modal); lv_obj_set_pos(addressInput,8,62); lv_obj_set_size(addressInput,264,36);
    lv_textarea_set_one_line(addressInput,true); lv_textarea_set_max_length(addressInput,sizeof(addressText)-1);
    lv_textarea_set_text(addressInput,addressText);
    lv_textarea_set_placeholder_text(addressInput,"ALL");
    text(modal,8,102,300,"Registers (hex)");
    registerInput=lv_textarea_create(modal); lv_obj_set_pos(registerInput,8,122); lv_obj_set_size(registerInput,264,36);
    lv_textarea_set_one_line(registerInput,true); lv_textarea_set_max_length(registerInput,sizeof(registerText)-1);
    lv_textarea_set_text(registerInput,registerText);
    lv_textarea_set_placeholder_text(registerInput,"ALL");
    for (auto *input:{addressInput,registerInput}) {
        ui_update::font(input,&lv_font_montserrat_16,0);
        ui_update::bgColor(input,lv_color_hex(0x2c363b),0);
        ui_update::textColor(input,lv_color_hex(Ink),0);
        ui_update::borderColor(input,lv_color_hex(0x47565e),0);
        lv_obj_set_style_radius(input,3,0);
        lv_obj_set_style_border_width(input,2,0);
        ui_update::borderColor(input,lv_color_hex(Cyan),LV_STATE_FOCUSED);
        ui_update::bgColor(input,lv_color_hex(0x235742),LV_PART_SELECTED);
        lv_textarea_set_text_selection(input,true);
        for (auto code:{LV_EVENT_FOCUSED,LV_EVENT_DEFOCUSED,LV_EVENT_CLICKED,LV_EVENT_INSERT}) lv_obj_add_event_cb(input,focusInput,code,nullptr);
        auto *clear=button(modal,278,input==addressInput?62:122,34,LV_SYMBOL_CLOSE,clearInput,0);
        lv_obj_set_height(clear,36); lv_obj_set_user_data(clear,input);
        lv_obj_remove_event_cb(clear,clearInput);
        lv_obj_add_event_cb(clear,clearInput,LV_EVENT_CLICKED,input);
    }
    readCheck=lv_checkbox_create(modal); lv_obj_set_pos(readCheck,12,164); lv_checkbox_set_text(readCheck,"Read");
    writeCheck=lv_checkbox_create(modal); lv_obj_set_pos(writeCheck,104,164); lv_checkbox_set_text(writeCheck,"Write");
    unkeyedCheck=lv_checkbox_create(modal); lv_obj_set_pos(unkeyedCheck,200,164); lv_checkbox_set_text(unkeyedCheck,"Unkeyed faults");
    if (selection.includeUnkeyed) lv_obj_add_state(unkeyedCheck,LV_STATE_CHECKED);
    if (selection.read) lv_obj_add_state(readCheck,LV_STATE_CHECKED);
    if (selection.write) lv_obj_add_state(writeCheck,LV_STATE_CHECKED);
    for (auto *check:{readCheck,writeCheck,unkeyedCheck}) {
        ui_update::font(check,check==unkeyedCheck?&lv_font_montserrat_10:&lv_font_montserrat_14,0); ui_update::textColor(check,lv_color_hex(Ink),0);
        ui_update::bgColor(check,lv_color_hex(0x2c363b),LV_PART_INDICATOR);
        ui_update::bgColor(check,lv_color_hex(0x245561),LV_PART_INDICATOR|LV_STATE_CHECKED);
        ui_update::borderColor(check,lv_color_hex(Cyan),LV_PART_INDICATOR);
        ui_update::textColor(check,lv_color_hex(Ink),LV_PART_INDICATOR|LV_STATE_CHECKED);
    }
    outcomeChoice=lv_dropdown_create(modal); lv_obj_set_pos(outcomeChoice,8,190); lv_obj_set_size(outcomeChoice,148,36);
    lv_dropdown_set_options(outcomeChoice,"All results\nSuccess only\nFailure only"); lv_dropdown_set_selected(outcomeChoice,selection.outcome);
    ui_update::bgColor(outcomeChoice,lv_color_hex(0x2c363b),0); ui_update::textColor(outcomeChoice,lv_color_hex(Ink),0);
    ui_update::font(outcomeChoice,&lv_font_montserrat_14,0); ui_update::borderColor(outcomeChoice,lv_color_hex(0x47565e),0);
    lv_obj_set_style_radius(outcomeChoice,3,0);
    displayChoice=lv_dropdown_create(modal); lv_obj_set_pos(displayChoice,164,190); lv_obj_set_size(displayChoice,148,36);
    lv_dropdown_set_options(displayChoice,"Raw\nParsed\nRaw + parsed"); lv_dropdown_set_selected(displayChoice,unsigned(selection.display));
    ui_update::bgColor(displayChoice,lv_color_hex(0x2c363b),0);
    ui_update::textColor(displayChoice,lv_color_hex(Ink),0);
    ui_update::font(displayChoice,&lv_font_montserrat_14,0);
    ui_update::borderColor(displayChoice,lv_color_hex(0x47565e),0); lv_obj_set_style_radius(displayChoice,3,0);
    text(modal,8,242,84,"Unique order",&lv_font_montserrat_12);
    orderChoice=lv_dropdown_create(modal); lv_obj_set_pos(orderChoice,96,232); lv_obj_set_size(orderChoice,216,36);
    lv_dropdown_set_options(orderChoice,"Latest transaction\nLatest change"); lv_dropdown_set_selected(orderChoice,unsigned(selection.uniqueOrder));
    ui_update::bgColor(orderChoice,lv_color_hex(0x2c363b),0); ui_update::textColor(orderChoice,lv_color_hex(Ink),0);
    ui_update::font(orderChoice,&lv_font_montserrat_14,0); ui_update::borderColor(orderChoice,lv_color_hex(0x47565e),0);
    lv_obj_set_style_radius(orderChoice,3,0);
    text(modal,8,280,142,"Change factor",&lv_font_montserrat_12);
    factorChoice=lv_dropdown_create(modal); lv_obj_set_pos(factorChoice,164,272); lv_obj_set_size(factorChoice,148,32);
    lv_dropdown_set_options(factorChoice,"0 / exact\n0.1\n0.25\n0.5\n1\n2\n5\n10\n20\n50\n100");
    unsigned factorIndex=0;
    for (unsigned i=0;i<sizeof(sensitivityFactors)/sizeof(sensitivityFactors[0]);++i) if (selection.changeFactor==sensitivityFactors[i]) factorIndex=i;
    lv_dropdown_set_selected(factorChoice,factorIndex);
    ui_update::bgColor(factorChoice,lv_color_hex(0x2c363b),0); ui_update::textColor(factorChoice,lv_color_hex(Ink),0);
    ui_update::font(factorChoice,&lv_font_montserrat_14,0);
    filterError=text(modal,8,306,304,"",&lv_font_montserrat_10); ui_update::textColor(filterError,lv_color_hex(Red),0);
    auto *apply=button(modal,8,322,148,"APPLY",applyFilter,0);
    auto *reset=button(modal,164,322,148,LV_SYMBOL_REFRESH " RESET",resetFilter,0);
    ui_update::bgColor(apply,lv_color_hex(0x226b49),0); ui_update::borderColor(apply,lv_color_hex(Green),0);
    ui_update::bgColor(reset,lv_color_hex(0x85373f),0); ui_update::borderColor(reset,lv_color_hex(Red),0);
    keyboard=lv_keyboard_create(modal); lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard,0,368); lv_obj_set_size(keyboard,320,112);
    static const char *const hexMap[]={"0","1","2","3","4","5","6","7","\n","8","9","A","B","C","D","E","F","\n","x","-",","," ",LV_SYMBOL_LEFT,LV_SYMBOL_RIGHT,LV_SYMBOL_BACKSPACE,""};
    static lv_buttonmatrix_ctrl_t hexCtrl[23];
    for (auto &key:hexCtrl) key=static_cast<lv_buttonmatrix_ctrl_t>(1|LV_BUTTONMATRIX_CTRL_NO_REPEAT|LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_USER_1,hexMap,hexCtrl);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_USER_1);
    ui_update::bgColor(keyboard,lv_color_hex(0x1e2528),0);
    ui_update::bgColor(keyboard,lv_color_hex(0x2c363b),LV_PART_ITEMS);
    ui_update::textColor(keyboard,lv_color_hex(Ink),LV_PART_ITEMS);
    ui_update::font(keyboard,&lv_font_montserrat_16,LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard,3,LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard,addressInput);
}
void createUi() {
    auto *root=lv_screen_active(); flat(root,Bg); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    monitorScreen=lv_obj_create(root); flat(monitorScreen,Bg); lv_obj_set_size(monitorScreen,W,H); lv_obj_set_pos(monitorScreen,0,0); lv_obj_remove_flag(monitorScreen,LV_OBJ_FLAG_SCROLLABLE);
    auto *screen=monitorScreen;
    text(screen,8,7,225,"I2C MONITOR",&lv_font_montserrat_18);
    linkIcon=text(screen,242,8,78,LV_SYMBOL_USB " LINK");
    lv_obj_add_flag(linkIcon,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(linkIcon,linkDetails,LV_EVENT_CLICKED,nullptr);
    speed=text(screen,8,34,140,"--.- kHz",&lv_font_montserrat_24); state=text(screen,148,40,124,"NO LINK");
    filterButton=button(screen,272,27,40,"",openFilter,0); lv_obj_set_style_pad_all(filterButton,0,0);
    // The bundled symbol font has no funnel; an alpha bitmap avoids skew-line clipping.
    static uint8_t alpha[400];
    static const uint32_t funnel[]={0xfffff,0x40002,0x20004,0x10008,0x8010,0x4020,0x2040,
        0x1080,0x1080,0x1080,0x1080,0x1080,0x1080,0x1080,0x1080,0x1080,0x1080,0x880,0x280,0x180};
    for (unsigned y=0;y<20;++y) for (unsigned x=0;x<20;++x) alpha[y*20+x]=(funnel[y]&(1u<<x))?255:0;
    static lv_image_dsc_t image={}; image.header.magic=LV_IMAGE_HEADER_MAGIC;
    image.header.cf=LV_COLOR_FORMAT_A8; image.header.w=image.header.h=image.header.stride=20;
    image.data_size=sizeof(alpha); image.data=alpha;
    auto *icon=lv_image_create(filterButton); lv_image_set_src(icon,&image);
    lv_obj_set_style_image_recolor(icon,lv_color_hex(Ink),0); lv_obj_set_style_image_recolor_opa(icon,LV_OPA_COVER,0); lv_obj_center(icon);
    lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);
    controls[0]=button(screen,8,68,96,LV_SYMBOL_PLAY " RUN",capture,0);
    ui_update::bgColor(controls[0],lv_color_hex(0x238557),0);
    runLabel=lv_obj_get_child(controls[0],0);
    controls[1]=button(screen,112,68,88,LV_SYMBOL_TRASH " CLEAR",capture,1);
    ui_update::bgColor(controls[1],lv_color_hex(0x9c3544),0);
    viewChoice=lv_dropdown_create(screen); lv_obj_set_pos(viewChoice,208,68); lv_obj_set_size(viewChoice,104,40);
    lv_dropdown_set_options(viewChoice,"Sniff\nUnique\nDiag\nDiscover"); lv_dropdown_set_selected(viewChoice,unsigned(view));
    ui_update::bgColor(viewChoice,lv_color_hex(0x2c363b),0); ui_update::textColor(viewChoice,lv_color_hex(Ink),0);
    ui_update::font(viewChoice,&lv_font_montserrat_14,0); lv_obj_set_style_radius(viewChoice,3,0);
    ui_update::borderColor(viewChoice,lv_color_hex(0x47565e),0); lv_obj_set_style_pad_hor(viewChoice,8,0);
    lv_obj_add_event_cb(viewChoice,chooseView,LV_EVENT_VALUE_CHANGED,nullptr);
    headings[0]=text(screen,8,113,148,"RAW"); headings[1]=text(screen,164,113,148,"RAW");
    list=lv_obj_create(screen); flat(list,Bg); lv_obj_set_pos(list,0,132); lv_obj_set_size(list,320,320);
    lv_obj_set_scroll_dir(list,LV_DIR_VER); lv_obj_set_scrollbar_mode(list,LV_SCROLLBAR_MODE_AUTO);
    for (unsigned i=0;i<128;++i) {
        rows[i]=lv_obj_create(list); flat(rows[i],Bg); lv_obj_set_pos(rows[i],8+(i%2)*156,(i/2)*20); lv_obj_set_size(rows[i],148,20);
        lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(rows[i],LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(rows[i],openDetail,LV_EVENT_CLICKED,reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_add_event_cb(rows[i],rowTouch,LV_EVENT_PRESSED,reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_add_event_cb(rows[i],rowTouch,LV_EVENT_RELEASED,reinterpret_cast<void *>(uintptr_t(i)));
        lv_obj_add_event_cb(rows[i],rowTouch,LV_EVENT_PRESS_LOST,reinterpret_cast<void *>(uintptr_t(i)));
        ranks[i]=text(rows[i],1,3,24,"",&lv_font_montserrat_10); ui_update::textColor(ranks[i],lv_color_hex(Muted),0);
        labels[i]=text(rows[i],26,1,122,"",&mono::font); lv_label_set_long_mode(labels[i],LV_LABEL_LONG_CLIP);
        values[i]=text(rows[i],26,19,122,"",&lv_font_montserrat_14); lv_label_set_long_mode(values[i],LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(values[i],LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
    }
    diag=lv_obj_create(screen); flat(diag,Bg); lv_obj_set_pos(diag,0,112); lv_obj_set_size(diag,320,340);
    lv_obj_set_scroll_dir(diag,LV_DIR_VER); lv_obj_add_flag(diag,LV_OBJ_FLAG_HIDDEN);
    footer=text(screen,8,462,304,"STANDBY");
    createReports();
    createExtras();
    createRecorder();
}
struct DiagPanel { lv_obj_t *root=nullptr, *title=nullptr, *values[32]={}; };
DiagPanel diagPanels[7];
lv_obj_t *uartSummary, *uartSummaryTitle;
bool requestBaud(uint32_t target) {
    if (activeBusy()) return false;
    if (!connected() || !sessionReady || control.busy() || rateRequest) return false;
    rateTarget=target;
    if (rateTarget==uartBaud) return true;
    uint8_t command[5]={10}, wire[32]; bringup::put32(command+1,rateTarget);
    rateRequest=++control.nextId; if (!rateRequest) rateRequest=++control.nextId;
    size_t n=bringup::command(rateRequest,command,5,wire,sizeof(wire));
    if (uart.availableForWrite()<int(n)) { rateRequest=0; return false; }
    uart.write(wire,n); rateAt_ms=millis(); rateConfirm=false; awaitingBaudBeacon=false; decoderGap();
    rateStage="WAIT OLD-RATE ACK"; rateFailure="NONE";
    control.message="UART RATE NEGOTIATING";
    return true;
}
void changeBaud(lv_event_t *) {
    static const uint32_t rates[]={115200,1000000,2000000};
    if (requestBaud(rates[lv_dropdown_get_selected(baudChoice)])) defaultBaudAttempted=true;
}
void diagPanel(unsigned index,int &y,const char *title,const char *const *names,unsigned count) {
    auto &p=diagPanels[index]; int h=52+int(count)*24;
    p.root=inset(diag,8,y,304,h); p.title=text(p.root,12,12,280,title,&lv_font_montserrat_14);
    ui_update::textColor(p.title,lv_color_hex(Cyan),0);
    for (unsigned i=0;i<count;++i) {
        auto *name=text(p.root,12,40+int(i)*24,190,names[i],&lv_font_montserrat_12);
        ui_update::textColor(name,lv_color_hex(Muted),0);
        p.values[i]=text(p.root,212,38+int(i)*24,80,"",&mono::font);
        lv_obj_set_style_text_align(p.values[i],LV_TEXT_ALIGN_RIGHT,0);
    }
    insetEdge(p.root); y+=h+10;
}
void diagNumber(unsigned group,unsigned row,uint32_t value,bool problem=false) {
    auto *label=diagPanels[group].values[row];
    ui_update::textFmt(label,"%lu",(unsigned long)value);
    ui_update::textColor(label,lv_color_hex(problem && value?Amber:Ink),0);
}
void refreshDiag() {
    const char *captureNames[]={"Capture active","Capture fault","Raw export","UART subscription","Epoch","Transactions","Frames","Invalid frames","Ring overruns","FIFO stalls","Capture queue drops"};
    const char *timingNames[]={"PIO Hz","Resolution ns","Max blind ns","Clock Hz","Samples","Sample uptime ms","HIGH min ns","HIGH median ns","HIGH max ns","LOW min ns","LOW median ns","LOW max us","Period min ns","Period median ns","Period max us","Short HIGH (<250ns)","Short LOW (<250ns)","Long LOW (>5x)","Invalid timing frames","Heartbeats","Capture gaps"};
    const char *tabletNames[]={"Transactions","Failed transactions","Malformed transactions","Unassociated","Unique / 128","New keys not stored","History / 64","Touch bus errors","Touch bounds errors","Internal free bytes","PSRAM free bytes"};
    const char *syncNames[]={"Synchronized","Definitions","Decoded keys"};
    const char *transportNames[]={"RP actual baud","RP queue peak bytes","RP transmitted bytes","RP baud fallbacks","RP pending batch bytes","RP filter generation","RP switch omissions","RP rejected transactions","RP discarded bytes","RP queue bytes","RP capacity bytes","RP rejected packets","RP malformed input","RP filtered transactions","Tablet valid packets","Tablet bad packets","Tablet sequence gaps","Tablet sequence resets","Tablet RX errors","Tablet startup gaps","Tablet startup RX errors"};
    if (!diagPanels[0].root) {
        int y=8;
        auto *summary=inset(diag,8,y,304,220); insetEdge(summary);
        uartSummaryTitle=text(summary,12,12,280,"UART / WAITING",&lv_font_montserrat_14);
        uartSummary=text(summary,12,40,280,"",&lv_font_montserrat_12); y+=230;
        diagPanel(5,y,"UART / TRANSPORT",transportNames,21);
        auto *root=diagPanels[5].root;
        lv_obj_set_height(root,620);
        baudChoice=lv_dropdown_create(root); lv_obj_set_pos(baudChoice,12,566); lv_obj_set_size(baudChoice,160,40);
        lv_dropdown_set_options(baudChoice,"115200\n1000000\n2000000");
        lv_dropdown_set_selected(baudChoice,uartBaud==2000000?2:uartBaud==1000000?1:0);
        ui_update::font(baudChoice,&lv_font_montserrat_14,0); ui_update::textColor(baudChoice,lv_color_hex(Ink),0);
        ui_update::bgColor(baudChoice,lv_color_hex(0x2c363b),0);
        auto *setBaud=button(root,184,566,108,"SET BAUD",changeBaud,0);ui_update::bgColor(setBaud,lv_color_hex(0x238557),0);
        y+=620-(52+21*24);
        const char *rateNames[]={"Queue capacity bytes","Current queue bytes","Diag epoch / RP","Rate phase / RP","Rate failure / RP","Rate attempts / RP","Requested baud / RP","Attempt actual / RP","Actual baud / tablet","Transition RX errors","Transition bad packets","BREAK events","RX buffer full","FIFO overflow","Framing errors","Parity errors","Clear count / tablet"};
        diagPanel(6,y,"UART / RECOVERY",rateNames,17);
        auto &r=diagPanels[6]; lv_obj_set_height(r.root,40+17*24+88);
        r.values[17]=text(r.root,12,40+17*24,280,"",&lv_font_montserrat_12);
        y+=36;
        diagPanel(0,y,"CAPTURE / RP2350",captureNames,11);
        diagPanel(1,y,"TIMING / RP2350",timingNames,21);
        diagPanel(2,y,"TABLET / DECODER",tabletNames,11);
        diagPanel(3,y,"SYNC / DECODER",syncNames,3);
        auto &p=diagPanels[4]; p.root=inset(diag,8,y,304,152);
        p.title=text(p.root,12,12,280,"CONTROL",&lv_font_montserrat_14);
        ui_update::textColor(p.title,lv_color_hex(Cyan),0);
        p.values[0]=text(p.root,12,40,280,"",&lv_font_montserrat_14);
        p.values[1]=text(p.root,12,86,280,"UART source filter; USB independent",&lv_font_montserrat_12);
        ui_update::textColor(p.values[1],lv_color_hex(Muted),0); insetEdge(p.root);
    }
    const uint32_t baudDifference=val(136)>tabletActualBaud?val(136)-tabletActualBaud:tabletActualBaud-val(136);
    const bool baudMismatch=val(136) && uint64_t(baudDifference)*100>val(136);
    const bool transportProblem=baudMismatch || val(32) || val(36) || val(148) || val(160) || val(164) || val(168) ||
        receiver.bad || sequence.gaps || rxOverflow || startupRxErrors || transitionErrors || transitionBad;
    const char *summaryState=!connected()?"DISCONNECTED / STALE":rateRequest?"NEGOTIATING":!val(136)?"WAITING FOR BAUD":transportProblem?"ERRORS OBSERVED":
        val(44)>8192?"BACKLOG":"HEALTHY";
    const uint32_t summaryColor=!connected() || rateRequest?Amber:transportProblem?Red:val(44)>8192?Amber:Green;
    ui_update::textFmt(uartSummaryTitle,"UART / %s",summaryState);
    ui_update::textColor(uartSummaryTitle,lv_color_hex(summaryColor),0);
    ui_update::textFmt(uartSummary,"Baud RP %lu / tablet %lu\nQueue %lu of %lu B / peak %lu B\nRP rejected TX %lu / fallback %lu\nTablet bad %lu / gaps %lu / RX %lu\nEpoch %lu / tablet clear %lu / age %lu ms\nPeak includes control traffic. Counters since clear/boot; not proof of complete I2C capture.",
        (unsigned long)val(136),(unsigned long)tabletActualBaud,(unsigned long)val(44),(unsigned long)val(176),(unsigned long)val(140),(unsigned long)val(164),(unsigned long)val(148),
        (unsigned long)receiver.bad,(unsigned long)sequence.gaps,(unsigned long)(rxOverflow+startupRxErrors),
        (unsigned long)val(172),(unsigned long)resetCount,(unsigned long)(seen?uint32_t(millis()-statusAt):0));
    ui_update::text(diagPanels[0].title,connected()?"CAPTURE / LIVE":seen?"CAPTURE / STALE":"CAPTURE / NO STATUS");
    ui_update::textColor(diagPanels[0].title,lv_color_hex(connected()?Cyan:Amber),0);
    for (unsigned i=0;i<4;++i) diagNumber(0,i,status[i],i==1);
    for (unsigned i=0;i<7;++i) diagNumber(0,4+i,val(4+4*i),i>=3);
    for (unsigned i=0;i<21;++i) diagNumber(1,i,val(48+4*i),i==18 || i==20);
    const uint32_t tabletValues[]={store->received,store->failures,store->malformed,store->unassociated,store->uniqueCount,store->capacityMiss,store->historyCount,touchBus,touchBounds,heap_caps_get_free_size(MALLOC_CAP_INTERNAL),ESP.getFreePsram()};
    for (unsigned i=0;i<11;++i) diagNumber(2,i,tabletValues[i],i==1 || i==2 || i==5 || i==7 || i==8);
    const uint32_t syncValues[]={sequence.synced,registry.activeCount(),decoder.count};
    for (unsigned i=0;i<3;++i) diagNumber(3,i,syncValues[i]);
    ui_update::textFmt(diagPanels[4].values[0],"%s\n%s",sessionReady?"SESSION VERIFIED":"SESSION NOT READY",control.message);
    ui_update::textColor(diagPanels[4].values[0],lv_color_hex(control.uncertain?Amber:Ink),0);
    for (unsigned i=0;i<9;++i) diagNumber(5,i,val(136+4*i),i==3 || i>=6);
    const uint32_t linkValues[]={val(44),val(176),val(32),val(36),val(40),receiver.valid,receiver.bad,sequence.gaps,sequence.resets,rxOverflow,sequence.startupGaps,startupRxErrors};
    for (unsigned i=0;i<12;++i) diagNumber(5,9+i,linkValues[i],i==2 || i==3 || i>=6);
    const uint32_t rateValues[]={val(176),val(44),val(172),val(180),val(184),val(188),val(192),val(196),tabletActualBaud,
        transitionErrors,transitionBad,errorKinds[UART_BREAK_ERROR],errorKinds[UART_BUFFER_FULL_ERROR],errorKinds[UART_FIFO_OVF_ERROR],
        errorKinds[UART_FRAME_ERROR],errorKinds[UART_PARITY_ERROR],resetCount};
    for (unsigned i=0;i<17;++i) diagNumber(6,i,rateValues[i],i==4 || (i>=9 && i<=15));
    ui_update::textFmt(diagPanels[6].values[17],"%s\n%s",rateStage,rateFailure);
}
void removeFields(unsigned i) {
    auto &group=groups[i];
    if (!group.root) return;
    lv_obj_add_flag(group.root,LV_OBJ_FLAG_HIDDEN);
    // Keep the reusable root, but do not accumulate hidden field children in the
    // fixed 512 KiB pool as history rows change between scalar/bitfield layouts.
    for (auto &field:group.fields) if (field.background) {
        lv_obj_delete(field.background); field=FieldUi{};
    }
    group.count=0; group.signature=0;
}
const char *fieldName(const decoding::Property &p) {
    if (!strcmp(p.id,"r3a_thermal_loop")) return "Thermal loop";
    if (!strcmp(p.id,"r3a_ic_temp")) return "IC temp";
    if (!strcmp(p.id,"r04_bat_num")) return "Series cells";
    return p.name;
}
const analysis::Value *fieldObservation(const analysis::Value &v,unsigned property) {
    auto &decoder=monitorDecoder();
    if(uniqueView())return decoder.used[property]?&decoder.values[property]:nullptr;
    for(unsigned i=0;i<decoder.historyCount;++i){const auto *field=decoder.row(false,i);
        if(field->property==property && field->transactionId==v.transactionId && field->observed_ms==v.observed_ms)return field;}
    return nullptr;
}
int refreshFields(unsigned i,const analysis::Value &v) {
    auto &decoder=monitorDecoder();
    const auto &p=decoding::properties[v.property]; auto &group=groups[i];
    uint32_t signature=(uint32_t(p.address)<<16)|(uint32_t(p.low)<<8)|uint8_t(v.direction);
    if (!group.root || group.signature!=signature) {
        group.signature=signature;
        if (!group.root) {
            ++fieldRootsCreated; group.root=lv_obj_create(rows[i]); flat(group.root,Bg);
            lv_obj_set_style_bg_opa(group.root,LV_OPA_TRANSP,0);
            lv_obj_remove_flag(group.root,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(group.root,LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_set_pos(group.root,0,42); lv_obj_set_width(group.root,148);
        }
        unsigned indices[8], count=0;
        for (unsigned k=0;k<decoding::PropertyCount && count<8;++k) {
            const auto &field=decoding::properties[k];
            if (field.address==p.address && field.low==p.low && field.count==1 && (v.direction=='R' || field.writable)) indices[count++]=k;
        }
        for (unsigned a=0;a<count;++a) for (unsigned b=a+1;b<count;++b)
            if (decoding::properties[indices[b]].shift>decoding::properties[indices[a]].shift) {
                unsigned temp=indices[a]; indices[a]=indices[b]; indices[b]=temp;
            }
        group.count=count;
        for (unsigned k=0;k<count;++k) {
            auto &field=group.fields[k]; field.property=indices[k];
            if (field.background) continue;
            field.background=lv_obj_create(group.root); flat(field.background,Bg);
            lv_obj_remove_flag(field.background,LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(field.background,LV_OBJ_FLAG_EVENT_BUBBLE);
            field.name=text(field.background,12,3,132,"");
            field.value=text(field.background,12,3,132,"");
            lv_label_set_long_mode(field.name,LV_LABEL_LONG_DOT);
            lv_label_set_long_mode(field.value,LV_LABEL_LONG_DOT);
            lv_obj_set_height(field.name,16); lv_obj_set_height(field.value,16);
            lv_obj_set_style_text_align(field.value,LV_TEXT_ALIGN_RIGHT,0);
        }
        for (unsigned k=count;k<8;++k) if (group.fields[k].background) {
            lv_obj_delete(group.fields[k].background); group.fields[k]=FieldUi{};
        }
    }
    lv_obj_remove_flag(group.root,LV_OBJ_FLAG_HIDDEN);
    int y=0;
    for (unsigned k=0;k<group.count;++k) {
        auto &field=group.fields[k]; const auto &definition=decoding::properties[field.property];
        auto candidate=v; candidate.property=field.property;
        if(const auto *observed=fieldObservation(v,field.property))candidate=*observed;
        char name[192], value[192];
        snprintf(name,sizeof(name),"%s",fieldName(definition));
        for (char *c=name;*c;++c) if (*c=='_') *c=' ';
        analysis::formatValue(candidate,value,sizeof(value));
        bool stale=status[0] && uint32_t(visualNow_ms()-v.observed_ms)>3000;
        if (stale) {
            char copy[192]; snprintf(copy,sizeof(copy),"%s",value); snprintf(value,sizeof(value),"~ %.188s",copy);
        }
        constexpr int height=22;
        lv_obj_set_pos(field.background,0,y); lv_obj_set_size(field.background,148,height);
        bool boolean=definition.bit0 && candidate.complete;
        uint32_t nameColor=Muted;
        if (boolean) {
            unsigned code=(candidate.word&definition.mask)>>definition.shift;
            const char *meaning=code?definition.bit1:definition.bit0;
            nameColor=!candidate.success || stale?Amber:!strcmp(meaning,"YES")?Green:Red;
            ui_update::textFmt(field.name,"%s%s",!candidate.success?"! ":stale?"~ ":"",name);
            lv_obj_set_width(field.name,132); lv_obj_add_flag(field.value,LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_point_t size; lv_text_get_size(&size,value,&lv_font_montserrat_14,0,0,1000,LV_TEXT_FLAG_NONE);
            int valueWidth=size.x<86?size.x:86;
            ui_update::text(field.name,name); lv_obj_set_width(field.name,132-valueWidth-6);
            lv_obj_set_pos(field.value,144-valueWidth,3); lv_obj_set_width(field.value,valueWidth);
            ui_update::text(field.value,value); lv_obj_remove_flag(field.value,LV_OBJ_FLAG_HIDDEN);
        }
        bool lit=view==View::Unique && uint32_t(visualNow_ms()-candidate.fieldChanged_ms)<2200;
        if (view==View::Unique) scheduleVisualExpiry(candidate.fieldChanged_ms,2200);
        uint32_t accent=v.success?(v.fresh?0x235742:0x655024):0x732d34;
        ui_update::bgColor(field.background,lv_color_hex(highlight::activity(0x1a2023,accent,candidate.fieldChanged_ms,candidate.observed_ms,candidate.unchanged,candidate.success,uniqueView(),visualNow_ms())),0);
        ui_update::textColor(field.name,lv_color_hex(nameColor),0);
        ui_update::textColor(field.value,lv_color_hex(!v.success?Red:stale?Muted:lit?(v.fresh?Green:Amber):Cyan),0);
        y+=height;
    }
    lv_obj_set_height(group.root,y); return 42+y;
}
uint32_t sourceChanged(const analysis::Value &v) {
    if(!uniqueView())return v.fieldChanged_ms;
    auto &decoder=monitorDecoder();
    uint32_t latest=v.fieldChanged_ms;
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoder.used[i] && analysis::sameSource(v,decoder.values[i],true) &&
        int32_t(decoder.values[i].fieldChanged_ms-latest)>0) latest=decoder.values[i].fieldChanged_ms;
    return latest;
}
bool sourceFresh(const analysis::Value &v) {
    if(!uniqueView())return v.fresh;
    auto &decoder=monitorDecoder();
    uint32_t latest=v.fieldChanged_ms; bool fresh=v.fresh;
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoder.used[i] && analysis::sameSource(v,decoder.values[i],true)) {
        const auto &field=decoder.values[i];
        if (int32_t(field.fieldChanged_ms-latest)>0) { latest=field.fieldChanged_ms; fresh=field.fresh; }
        else if (field.fieldChanged_ms==latest && !field.fresh) fresh=false;
    }
    return fresh;
}
bool sourceUnchanged(const analysis::Value &v) {
    auto &decoder=monitorDecoder();
    if(uniqueView()) {
        for(unsigned i=0;i<decoding::PropertyCount;++i)
            if(decoder.used[i] && analysis::sameSource(v,decoder.values[i],true) && !decoder.values[i].unchanged)return false;
    } else {
        for(unsigned i=0;i<decoder.historyCount;++i) {
            const auto *field=decoder.row(false,i);
            if(field && field->transactionId==v.transactionId && field->observed_ms==v.observed_ms &&
               analysis::sameSource(v,*field,true) && !field->unchanged)return false;
        }
    }
    return v.unchanged;
}
void refreshHighlights() {
    auto &decoder=monitorDecoder();
    if ((view!=View::Unique && view!=View::Sniff) || !listInitialized || touchHeld || rowHeld || !visualClock.running) return;
    uint32_t now_ms=visualNow_ms();
    if (uint32_t(now_ms-highlightAt_ms)<highlight::Refresh_ms) return;
    highlightAt_ms=now_ms;
    const bool both=selection.display==analysis::Display::Both;
    // Style-only updates: no row sorting, text changes, allocation or layout.
    for (unsigned i=0;i<visibleCount;++i) {
        const auto *o=visibleRaw[i]; const auto *v=visibleDecoded[i];
        bool success=o?o->success:v->success, fresh=o?o->fresh:sourceFresh(*v);
        uint32_t changed_ms=o?o->changed_ms:sourceChanged(*v);
        uint32_t base=(both?i:i/2)%2?0x1d2327:Bg;
        uint32_t accent=success?(fresh?0x235742:0x655024):0x732d34;
        bool unchanged=o?o->unchanged:sourceUnchanged(*v);uint32_t observed_ms=o?o->observed_ms:v->observed_ms;
        ui_update::bgColor(rows[i],lv_color_hex(highlight::activity(base,accent,changed_ms,observed_ms,unchanged,success,uniqueView(),now_ms)),0);
        auto &group=groups[i];
        for (unsigned k=0;k<group.count;++k) {
            unsigned property=group.fields[k].property;
            if (decoder.used[property]) {
                const analysis::Value *field=fieldObservation(*v,property);
                if(field)ui_update::bgColor(group.fields[k].background,
                    lv_color_hex(highlight::activity(0x1a2023,accent,field->fieldChanged_ms,field->observed_ms,field->unchanged,field->success,uniqueView(),now_ms)),0);
            }
        }
    }
}
const char *displayName(const decoding::Property &p) {
    return !strcmp(p.id,"battery_current_code_ma")?"Battery current":
        !strcmp(p.id,"system_current_code_ma")?"System current":
        !strcmp(p.id,"r3a_ic_temp")?"Die temperature":
        !strcmp(p.id,"r04_bat_num")?"Series cells":p.name;
}
bool computeRecent(unsigned row) {
    auto &decoder=monitorDecoder();
    if (visibleRaw[row]) return rawRecent[visibleRaw[row]-store->unique];
    const auto &v=*visibleDecoded[row];
    for (unsigned i=0;i<decoding::PropertyCount;++i) if (decoder.used[i] && decodedRecent[i] && analysis::sameSource(v,decoder.values[i],true)) return true;
    return false;
}
bool recentlyGrouped(unsigned row) { return visibleRecent[row]; }
int compareNames(const char *a,const char *b) {
    while (*a && *b) {
        unsigned x=unsigned(*a), y=unsigned(*b);
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return x<y?-1:1;
        ++a; ++b;
    }
    return *a?1:*b?-1:0;
}
bool alphabeticalBefore(unsigned a,unsigned b) {
    bool recentA=recentlyGrouped(a), recentB=recentlyGrouped(b);
    if (recentA!=recentB) return recentA;
    const auto *x=visibleDecoded[a], *y=visibleDecoded[b];
    int names=compareNames(x?displayName(decoding::properties[x->property]):"~ Raw",y?displayName(decoding::properties[y->property]):"~ Raw");
    if (names) return names<0;
    unsigned addrA=x?decoding::properties[x->property].address:visibleRaw[a]->address;
    unsigned addrB=y?decoding::properties[y->property].address:visibleRaw[b]->address;
    if (addrA!=addrB) return addrA<addrB;
    unsigned regA=x?decoding::properties[x->property].low:visibleRaw[a]->reg;
    unsigned regB=y?decoding::properties[y->property].low:visibleRaw[b]->reg;
    return regA<regB;
}
void refreshUi() {
    // Reconcile model changes into retained LVGL widgets. Cached setters and
    // generation counters avoid unconditional redraws and preserve touch targets.
    auto &decoder=monitorDecoder();
    updateVisualClock();
    reportPolicy();
    profileStorage.save(registry,connected() && sessionReady && !status[0] && !control.busy() && !rateRequest && !activeBusy());
    if (screenIndex<2 && !activeBusy() && !modal && !touchHeld && !rowHeld && !sliding && visualClock.running && (reportActive || (view!=View::Diag && selection.display!=analysis::Display::Raw))) {
        for (unsigned a=8;a<0x78;++a) if (registry.bindings[a].seen) registry.observe(a,true);
        int address=registry.nextPrompt(); if (address>=0) openProfile(uint8_t(address));
    }
    // Capture continues in pump(); an opaque modal needs no underlying updates.
    if (modal) return;
    if (screenIndex>=2) { if(screenIndex==5)refreshRecorder();else refreshExtras();if(screenIndex==3)refreshStart();return; }
    if (reportActive) { refreshReports(); return; }
    bool online=connected();
    bool uartWarning=control.uncertain && !strcmp(control.message,"UART RX ERROR / CHECK DIAG");
    ui_update::textColor(linkIcon,lv_color_hex(uartWarning?Red:online && sessionReady?Green:Amber),0);
    ui_update::text(linkIcon,online && sessionReady && !uartWarning?LV_SYMBOL_USB " LINK":LV_SYMBOL_WARNING " LINK");
    uint32_t clockTenths_kHz=uint32_t((uint64_t(val(60))+50)/100);
    if (online && val(60)) ui_update::textFmt(speed,"%lu.%lu kHz",(unsigned long)(clockTenths_kHz/10),(unsigned long)(clockTenths_kHz%10));
    else ui_update::text(speed,"--.- kHz");
    const char *s=!online?"NO LINK":status[1]?"FAULT":status[2]?"RAW EXPORT":activePending?"ACTIVE READ":rateRequest?"RATE CHANGE":!sessionReady?(control.busy()?"SYNCING":"TAP LINK"):control.busy()?"PENDING":
        status[0]?(delivering?"CAPTURING":"REMOTE ACTIVE"):(delivering?"PAUSED":"STANDBY");
    ui_update::text(state,s); ui_update::textColor(state,lv_color_hex(!online?Amber:status[1]?Red:status[0]?Green:Muted),0);
    ui_update::bgColor(filterButton,lv_color_hex(selection.active() || (discover->enabled && discover->hasDecisions())?0x245561:0x2c363b),0);
    ui_update::borderColor(filterButton,lv_color_hex(selection.active() || (discover->enabled && discover->hasDecisions())?Cyan:0x47565e),0);
    ui_update::text(runLabel,online && sessionReady && status[0]?LV_SYMBOL_PAUSE " PAUSE":LV_SYMBOL_PLAY " RUN");
    ui_update::bgColor(controls[0],lv_color_hex(online && sessionReady && status[0]?0x9c3544:0x238557),0);
    for (unsigned i=0;i<2;++i) {
        if (online && sessionReady && !activeBusy() && !control.busy() && !rateRequest && !status[2]) lv_obj_remove_state(controls[i],LV_STATE_DISABLED); else lv_obj_add_state(controls[i],LV_STATE_DISABLED);
    }
    if (view==View::Diag) { lv_obj_add_flag(list,LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(diag,LV_OBJ_FLAG_HIDDEN); refreshDiag(); rendered=View::Diag; }
    else if (!touchHeld && !rowHeld && (!listInitialized || rendered!=view || !sameSelection(selection,renderedSelection) ||
        (visualClock.running && (revision!=store->revision || renderedActive!=bool(status[0]) ||
        uint32_t(visualNow_ms()-listAt_ms)>=listWait_ms)))) {
        const bool grouped=view==View::Unique && selection.uniqueOrder==analysis::UniqueOrder::LatestChange;
        if (grouped && (!groupedOnce || uint32_t(visualNow_ms()-groupAt_ms)>=1000)) {
            groupedOnce=true; groupAt_ms=visualNow_ms();
            for (unsigned i=0;i<decoding::PropertyCount;++i) {
                decodedPublished[i]=decoder.used[i];
                decodedRecent[i]=decoder.used[i] && uint32_t(groupAt_ms-decoder.values[i].fieldChanged_ms)<2200;
            }
            rawPublishedCount=store->uniqueCount;
            for (unsigned i=0;i<store->uniqueCount;++i) rawRecent[i]=uint32_t(groupAt_ms-store->unique[i].changed_ms)<2200;
        }
        ++listPasses;
        listAt_ms=visualNow_ms(); listWait_ms=0x7fffffffu;
        if (grouped) listWait_ms=1000-uint32_t(visualNow_ms()-groupAt_ms);
        lv_obj_remove_flag(list,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(diag,LV_OBJ_FLAG_HIDDEN);
        visibleCount=0;
        bool raw=selection.display==analysis::Display::Raw;
        bool both=selection.display==analysis::Display::Both;
        ui_update::text(headings[0],raw?"RAW":both?"RAW / SOURCE":"PARSED");
        ui_update::text(headings[1],raw?"RAW":both?"VALUE":"PARSED");
        unsigned limit=raw?(uniqueView()?store->uniqueCount:store->historyCount):
            (uniqueView()?decoder.count:decoder.historyCount);
        for (unsigned source=0;source<limit && visibleCount<128;++source) {
            const auto *o=raw?store->row(uniqueView(),source):nullptr;
            const auto *v=raw?nullptr:decoder.row(uniqueView(),source);
            if (grouped && (o?unsigned(o-store->unique)>=rawPublishedCount:!decodedPublished[v->property])) continue;
            if (o?!selection.matches(*o):!analysis::matches(selection,*v)) continue;
            bool duplicate=false;
            if (v) for (unsigned j=0;j<visibleCount;++j) {
                if (!visibleDecoded[j] || !analysis::sameSource(*v,*visibleDecoded[j],uniqueView())) continue;
                const auto &previous=*visibleDecoded[j];
                bool newer=uniqueView() && (int32_t(v->observed_ms-previous.observed_ms)>0 ||
                    (v->observed_ms==previous.observed_ms && int32_t(v->transactionId-previous.transactionId)>0));
                bool sameTransaction=v->observed_ms==previous.observed_ms && v->transactionId==previous.transactionId;
                if (newer || ((!uniqueView() || sameTransaction) && analysis::priority(*v)>analysis::priority(previous))) visibleDecoded[j]=v;
                duplicate=true; break;
            }
            if (duplicate) continue;
            visibleRaw[visibleCount]=o; visibleDecoded[visibleCount]=v; ++visibleCount;
        }
        if (!raw) {
            // Retain unknown or unsupported transfers as raw fallback rows.
            unsigned rawLimit=uniqueView()?store->uniqueCount:store->historyCount;
            for (unsigned source=0;source<rawLimit && visibleCount<128;++source) {
                const auto *o=store->row(uniqueView(),source);
                if (grouped && unsigned(o-store->unique)>=rawPublishedCount) continue;
                if (!selection.matches(*o)) continue;
                bool covered=false;
                if (o->associated && o->dataBytes==1) for (unsigned k=0;k<decoding::PropertyCount;++k) {
                    const auto &p=decoding::properties[k];
                    if (decoder.used[k] && p.count && p.address==o->address && o->reg>=p.low && o->reg<unsigned(p.low)+p.count &&
                        (o->direction=='R' || (o->direction=='W' && p.writable))) covered=true;
                }
                if (!covered) { visibleRaw[visibleCount]=o; visibleDecoded[visibleCount]=nullptr; ++visibleCount; }
            }
        }
        if (grouped) for (unsigned i=0;i<visibleCount;++i) visibleRecent[i]=computeRecent(i);
        for (unsigned i=1;i<visibleCount;++i) {
                unsigned j=i;
                while (j>0) {
                    uint32_t a=visibleRaw[j]?visibleRaw[j]->observed_ms:visibleDecoded[j]->observed_ms;
                    uint32_t b=visibleRaw[j-1]?visibleRaw[j-1]->observed_ms:visibleDecoded[j-1]->observed_ms;
                    uint32_t idA=visibleRaw[j]?visibleRaw[j]->id:visibleDecoded[j]->transactionId;
                    uint32_t idB=visibleRaw[j-1]?visibleRaw[j-1]->id:visibleDecoded[j-1]->transactionId;
                    if (!(grouped?alphabeticalBefore(j,j-1):analysis::moreRecent(a,idA,b,idB,true))) break;
                    auto *tempRaw=visibleRaw[j]; visibleRaw[j]=visibleRaw[j-1]; visibleRaw[j-1]=tempRaw;
                    auto *tempDecoded=visibleDecoded[j]; visibleDecoded[j]=visibleDecoded[j-1]; visibleDecoded[j-1]=tempDecoded;
                    bool tempRecent=visibleRecent[j]; visibleRecent[j]=visibleRecent[j-1]; visibleRecent[j-1]=tempRecent;
                    --j;
                }
            }
        analysis::ColumnFlow flow;
        for (unsigned i=0;i<128;++i) {
            if (i>=visibleCount) { removeFields(i); lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN); continue; }
            const auto *o=visibleRaw[i]; const auto *v=visibleDecoded[i];
            lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
            int rowHeight=raw || both?20:40;
            bool expanded=!raw && !both && v && analysis::bitfields(*v);
            if (!expanded) removeFields(i);
            ui_update::textFmt(ranks[i],"%u",i+1);
            lv_obj_set_pos(labels[i],26,1); lv_obj_set_width(labels[i],122);
            ui_update::font(labels[i],raw || both || expanded?&mono::font:&lv_font_montserrat_14,0);
            ui_update::font(values[i],expanded?&mono::font:&lv_font_montserrat_14,0);
            char line[192], result[192];
            bool success=o?o->success:v->success, fresh=o?o->fresh:sourceFresh(*v);
            uint32_t changed_ms=o?o->changed_ms:sourceChanged(*v);
            bool stale=v && status[0] && uint32_t(visualNow_ms()-v->observed_ms)>3000;
            if (v && status[0]) scheduleVisualExpiry(v->observed_ms,3001);
            if (view==View::Unique) scheduleVisualExpiry(changed_ms,2200);
            if (o) {
                char reg[3]; if (o->associated) snprintf(reg,sizeof(reg),"%02X",o->reg); else memcpy(reg,"--",3);
                snprintf(line,sizeof(line),"%02X %s %c",o->address,reg,o->direction);
                size_t n=strlen(line);
                for (unsigned j=0;j<o->dataBytes && j<2;++j) n+=snprintf(line+n,sizeof(line)-n," %02X",o->preview[j]);
                if (o->dataBytes>2) snprintf(line+n,sizeof(line)-n,"+");
                if (raw) lv_obj_add_flag(values[i],LV_OBJ_FLAG_HIDDEN);
                else {
                    lv_obj_set_pos(values[i],both?164:26,both?1:20); lv_obj_set_width(values[i],both?140:122);
                    ui_update::text(values[i],"-- RAW ONLY"); ui_update::textColor(values[i],lv_color_hex(Muted),0);
                    lv_obj_remove_flag(values[i],LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                const auto &p=decoding::properties[v->property]; analysis::formatOverview(*v,result,sizeof(result));
                if (both) {
                    if (p.count==3 && v->complete) snprintf(line,sizeof(line),"%02X %02X-%02X %06X",p.address,p.low,p.high,v->word);
                    else if (p.count==3) snprintf(line,sizeof(line),"%02X %02X-%02X --",p.address,p.low,p.high);
                    else if (p.count==2 && v->complete) snprintf(line,sizeof(line),"%02X %02X+%02X %02X%02X",p.address,p.low,p.high,v->word&255,v->word>>8);
                    else if (p.count==2) snprintf(line,sizeof(line),"%02X %02X+%02X --",p.address,p.low,p.high);
                    else snprintf(line,sizeof(line),"%02X %02X %c %02X",p.address,p.low,v->direction,v->word);
                    lv_obj_set_pos(values[i],164,1); lv_obj_set_width(values[i],140);
                } else if (expanded) {
                    snprintf(line,sizeof(line),"%02X %02X %c",p.address,p.low,v->direction);
                    for (unsigned bit=0;bit<8;++bit) result[bit]=(v->word&(1u<<(7-bit)))?'1':'0';
                    result[8]=0;
                    if (!v->success) { char copy[16]; snprintf(copy,sizeof(copy),"%s",result); snprintf(result,sizeof(result),"FAIL %s",copy); }
                    lv_obj_set_pos(values[i],26,20); lv_obj_set_width(values[i],122);
                    rowHeight=refreshFields(i,*v);
                } else {
                    const char *shortName=displayName(p);
                    if (analysis::flagsOnly(*v)) snprintf(line,sizeof(line),"%02X %02X / Flags",p.address,p.low);
                    else snprintf(line,sizeof(line),"%s",shortName);
                    lv_obj_set_pos(values[i],26,20); lv_obj_set_width(values[i],122);
                }
                ui_update::textFmt(values[i],"%s%s",stale?"~ ":"",result);
                ui_update::textColor(values[i],lv_color_hex(stale?Muted:success?Cyan:Red),0);
                lv_obj_remove_flag(values[i],LV_OBJ_FLAG_HIDDEN);
            }
            ui_update::text(labels[i],line);
            int x,y;
            if (both) { x=8; y=int(i)*20; }
            else flow.place(rowHeight+(raw?0:4),x,y);
            lv_obj_set_pos(rows[i],x,y); lv_obj_set_size(rows[i],both?304:148,rowHeight);
            bool lit=view==View::Unique && uint32_t(visualNow_ms()-changed_ms)<2200;
            uint32_t base=(both?i:i/2)%2?0x1d2327:Bg;
            uint32_t accent=success?(fresh?0x235742:0x655024):0x732d34;
            uint32_t bg=highlight::activity(base,accent,changed_ms,o?o->observed_ms:v->observed_ms,o?o->unchanged:sourceUnchanged(*v),success,uniqueView(),visualNow_ms());
            ui_update::bgColor(rows[i],lv_color_hex(bg),0);
            ui_update::textColor(labels[i],lv_color_hex(!success?Red:lit?(fresh?Green:Amber):Ink),0);
        }
        revision=store->revision; rendered=view; renderedSelection=selection;
        renderedActive=status[0]; listInitialized=true;
    }
    refreshHighlights();
    if (!online) ui_update::text(footer,"Waiting for sniffer / tap LINK for Diag");
    else if (!sessionReady) ui_update::text(footer,control.busy()?"Synchronizing / capture will stay paused":"Tap LINK to reconnect / open Diag");
    else if ((control.uncertain && !uartWarning) || control.busy()) ui_update::text(footer,control.message);
    else if (rateRequest) ui_update::text(footer,"UART speed negotiation / capture unchanged");
    else if (val(44)>8192 && val(136)) ui_update::textFmt(footer,"UART backlog %lu bytes / ~%lu ms",(unsigned long)val(44),
        (unsigned long)(uint64_t(val(44))*10000/val(136)));
    else if (val(164)) ui_update::textFmt(footer,"UART lost %lu TX / %lu RX gaps",(unsigned long)val(164),(unsigned long)sequence.gaps);
    else if (registry.saveFailed) ui_update::text(footer,"Profile save failed / choices not persisted");
    else if (profileStorage.pending(registry)) ui_update::text(footer,"Profile chosen / pause to save assignment");
    else if(discoveryGatesMonitor())ui_update::textFmt(footer,"%u keep / %u ignore / %u pending%s",discover->kept,discover->ignored,discover->count,discover->overflow?" / overflow":"");
    else ui_update::textFmt(footer,"%u shown / %lu fail / %lu gaps",visibleCount,(unsigned long)store->failures,(unsigned long)sequence.gaps);
    ui_update::textColor(footer,lv_color_hex(online && sessionReady && control.uncertain && !uartWarning?Red:val(164)?Amber:Muted),0);
}
#include "discovery_ui.h"
#include "report_ui.h"
#include "experiment_ui.h"
#include "read_burst_ui.h"
#include "recorder_ui.h"
#include "profile_upload_ui.h"
#include "start_ui.h"
} // namespace
extern "C" void *tablet_lvgl_pool_alloc(size_t size) { return size<=LV_MEM_SIZE?lvglPool:nullptr; }
void setup() {
    // Allocate long-lived buffers before constructing widgets. Uploaded profile
    // strings remain owned by their package storage until reboot; loading them
    // before the UI prevents dangling pointers when fallback profiles are replaced.
    Serial.begin(115200); pinMode(1,OUTPUT); digitalWrite(1,LOW);
    decoder.valueObserver=[](const analysis::Value &v) { reportCache.accept(v,true,millis()); reportDirty=true; };
    decoder.resetObserver=[](int address) { if (address<0) reportCache.clear(); else reportCache.clearAddress(uint8_t(address)); reportDirty=true; };
    if (!psramFound()) fatal("PSRAM unavailable");
#ifndef TABLET_PREVIEW
    profileDisk.begin();profileDisk.load();
    if(!profileDisk.ready)Serial.printf("PROFILE_STORAGE %s / built-in profiles retained\n",profileDisk.error);
#endif
    profileStorage.load(registry);
    void *recorderStorage=heap_caps_malloc(sizeof(recording18::Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!recorderStorage)fatal("Recorder allocation");recorder=new(recorderStorage) recording18::Session();
    void *postStorage=heap_caps_malloc(sizeof(recording18::PostCapture),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!postStorage)fatal("Playback follow-up allocation");recorderPost=new(postStorage) recording18::PostCapture();
    void *discoveryStorage=heap_caps_malloc(sizeof(discovery::Session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    void *decoderStorage=heap_caps_malloc(sizeof(analysis::Decoder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!discoveryStorage || !decoderStorage)fatal("Discover allocation");
    discover=new(discoveryStorage) discovery::Session();
    discoveryDecoder=new(decoderStorage) analysis::Decoder();
    void *storage=heap_caps_malloc(sizeof(observer::Store),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!storage) fatal("Observation store allocation"); store=new(storage) observer::Store();
    fb=static_cast<uint16_t *>(heap_caps_calloc(W*H,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    draw=static_cast<uint8_t *>(heap_caps_malloc(W*20*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    detailText=static_cast<char *>(heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    transactionWire=static_cast<uint8_t *>(heap_caps_malloc(bringup::MaxPayload,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#ifndef TABLET_PREVIEW
    lvglPool=heap_caps_malloc(LV_MEM_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!lvglPool) fatal("LVGL PSRAM pool allocation");
#endif
    if (!fb || !draw || !detailText || !transactionWire) fatal("Display allocation");
    if (!panel.begin()) fatal("AXS15231B initialization"); panel.draw16bitRGBBitmap(0,0,fb,W,H); digitalWrite(1,HIGH);
    touchReady=Wire.begin(4,8,400000); Wire.setTimeOut(20);
    uartReady=uart.setRxBufferSize(16384)>=16384 && uart.setTxBufferSize(1024)>=1024;
    uart.begin(115200,SERIAL_8N1,17,18); uartReady=uartReady && bool(uart);
    uart.onReceiveError([](hardwareSerial_error_t error) {
        if (error==UART_NO_ERROR) return;
        pendingUartErrors.fetch_add(1);
        if (unsigned(error)<6) pendingErrorKinds[unsigned(error)].fetch_add(1);
    });
    lv_init(); lv_tick_set_cb(tick_ms);
    auto *d=lv_display_create(W,H); if (!d) fatal("LVGL display allocation");
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565); lv_display_set_buffers(d,draw,nullptr,W*20*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    auto *input=lv_indev_create(); if (!input) fatal("LVGL input allocation"); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(input,touch);
    createUi(); restoreScreen(); sourceFilter(selection); refreshUi(); Serial.printf("Tablet v22 ready; store %u bytes; %u profiles / %u definitions\n",unsigned(sizeof(observer::Store)),decoding::ProfileCount,decoding::DefinitionCount);
}
void loop() {
    // Keep wall time (link leases/timeouts) separate from presentation time
    // (fades frozen by Pause). Do not put blocking serial reads in this loop.
    profileUploadPump();pump(); static uint32_t refreshed=0;
    if (uint32_t(millis()-refreshed)>=125) { refreshed=millis(); refreshUi(); }
    lv_timer_handler(); pump(); delay(3);
}
