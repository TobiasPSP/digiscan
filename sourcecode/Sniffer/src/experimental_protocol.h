#pragma once
#include <stdint.h>
#include <stddef.h>
namespace active_read {
inline bool experimentalRequest(const uint8_t *p,size_t n,uint16_t &bytes){
    bytes=0;
    if(!p || n<7 || p[1]!=0xa5 || p[6]<8 || p[6]>119)return false;
    if(!(p[2]|p[3]|p[4]|p[5]))return false;
    if(p[0]==17)return n==7;
    if(p[0]==18)return n==8;
    if(p[0]==21){if(n!=10)return false;bytes=uint16_t(p[8]|(uint16_t(p[9])<<8));return bytes>=1 && bytes<=256;}
    if(p[0]!=16 || n<9)return false;
    bytes=uint16_t(p[7]|(uint16_t(p[8])<<8));
    return bytes>=1 && bytes<=258 && n==size_t(9+bytes);
}
}
