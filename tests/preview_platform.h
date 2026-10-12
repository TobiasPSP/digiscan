#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <vector>
#include <map>
#include <string>
struct Preferences {
    static std::map<std::string,std::string> saved;
    static bool fail;
    bool begin(const char *,bool) { return !fail; }
    void end() {}
    bool clear() { if (fail) return false; saved.clear(); return true; }
    bool isKey(const char *key) { return saved.count(key)!=0; }
    size_t putString(const char *key,const char *value) { if (fail) return 0; saved[key]=value; return strlen(value); }
    size_t getString(const char *key,char *value,size_t cap) {
        auto it=saved.find(key); if (it==saved.end() || it->second.size()+1>cap) return 0;
        memcpy(value,it->second.c_str(),it->second.size()+1); return it->second.size()+1;
    }
};
std::map<std::string,std::string> Preferences::saved;
bool Preferences::fail=false;
static uint32_t previewTime_ms=10000;
inline uint32_t millis() { return previewTime_ms; }
inline uint32_t micros() { return previewTime_ms*1000u; }
inline void delay(unsigned n) { previewTime_ms+=n; }
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
inline bool psramFound() { return true; }
constexpr int OUTPUT=1, LOW=0, HIGH=1, SERIAL_8N1=0, GFX_NOT_DEFINED=-1;
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4;
inline void *heap_caps_malloc(size_t n,int) { return malloc(n); }
inline void *heap_caps_calloc(size_t n,size_t s,int) { return calloc(n,s); }
inline unsigned heap_caps_get_free_size(int) { return 65536; }
struct PreviewSerial {
    std::deque<uint8_t> input; std::string output;
    void begin(int) {}
    int available(){return int(input.size());}
    int read(){int c=input.front();input.pop_front();return c;}
    template<class... T> void printf(const char *s,T... args) {char line[1024];snprintf(line,sizeof(line),s,args...);output+=line;::printf("%s",line);}
} Serial;
struct PreviewEsp { unsigned restarts=0;void restart(){++restarts;}unsigned getFreePsram() { return 6000000; } } ESP;
enum hardwareSerial_error_t { UART_NO_ERROR, UART_BREAK_ERROR, UART_BUFFER_FULL_ERROR, UART_FIFO_OVF_ERROR, UART_FRAME_ERROR, UART_PARITY_ERROR };
struct HardwareSerial {
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
    uint32_t currentBaud=115200;
    explicit HardwareSerial(int) {}
    size_t setRxBufferSize(size_t n) { return n; }
    size_t setTxBufferSize(size_t n) { return n; }
    void begin(int,int,int,int) {}
    void updateBaudRate(unsigned long value) { currentBaud=uint32_t(value); }
    uint32_t baudRate() { return currentBaud; }
    unsigned rxFlushes=0;
    void flush(bool txOnly) { if (!txOnly) { input.clear(); ++rxFlushes; } }
    explicit operator bool() { return true; }
    int available() { return int(input.size()); }
    int availableForWrite() { return 1024; }
    int read() { uint8_t b=input.front(); input.pop_front(); return b; }
    size_t read(uint8_t *buffer,size_t n) { size_t used=0; while (used<n && !input.empty()) buffer[used++]=uint8_t(read()); return used; }
    size_t write(uint8_t b) { output.push_back(b); return 1; }
    size_t write(const uint8_t *p,size_t n) { output.insert(output.end(),p,p+n); return n; }
    template<class T> void onReceiveError(T) {}
};
struct PreviewWire {
    bool begin(int,int,int) { return true; }
    void setTimeOut(int) {}
    void beginTransmission(int) {}
    void write(const uint8_t *,size_t) {}
    uint8_t endTransmission(bool) { return 0; }
    size_t requestFrom(uint8_t,size_t n,bool) { return n; }
    int available() { return 0; }
    int read() { return 0; }
} Wire;
struct Arduino_ESP32QSPI { Arduino_ESP32QSPI(int,int,int,int,int,int) {} };
static uint32_t previewTransfers=0;
struct Arduino_AXS15231B {
    Arduino_AXS15231B(Arduino_ESP32QSPI *,int,int,bool,int,int) {}
    bool begin() { return true; }
    void draw16bitRGBBitmap(int,int,uint16_t *,int,int) { ++previewTransfers; }
};
