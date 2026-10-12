#pragma once
// LittleFS durable package storage. Two slots/generations retain a valid prior
// package while replacement is written/verified; boot recovery validates bytes
// rather than trusting filenames. Configuration reset does not erase chip files.
// Installation becomes visible on reboot, preserving runtime pointer lifetimes.
#include "profile_binary.h"
#ifndef TABLET_PREVIEW
#include <LittleFS.h>
#include <esp_partition.h>
#else
#include "profile_disk_preview.h"
#endif
namespace portable {
class Disk {
public:
    bool ready=false; const char *error=nullptr;
    void begin(){
        ready=LittleFS.begin(false);if(ready)return;
#ifndef TABLET_PREVIEW
        // Never format an unrecognized occupied filesystem. Only a fully erased
        // partition may be initialized automatically on the first V20 boot.
        auto *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
        if(!part){error="no_storage_partition";return;}
        uint8_t buffer[512];
        for(size_t at=0;at<part->size;at+=sizeof(buffer)){
            unsigned n=part->size-at<sizeof(buffer)?part->size-at:sizeof(buffer);
            if(esp_partition_read(part,at,buffer,n)!=ESP_OK){error="storage_read";return;}
            for(unsigned i=0;i<n;++i)if(buffer[i]!=0xff){error="storage_not_blank";return;}
        }
        ready=LittleFS.begin(true);if(!ready)error="storage_init";
#else
        error="storage_unavailable";
#endif
    }
    Package *read(const char *path){
        auto file=LittleFS.open(path,"r");if(!file)return nullptr;
        unsigned n=file.size();if(n<36||n>MaxBytes+12)return nullptr;
        uint8_t head[12];if(file.read(head,12)!=12||memcmp(head,"PRFS",4)||crc32(head,8)!=u32(head+8)||!u32(head+4))return nullptr;
        auto *bytes=static_cast<uint8_t *>(allocate(n-12));if(!bytes)return nullptr;
        if(file.read(bytes,n-12)!=n-12){free(bytes);return nullptr;}
        auto *p=new(std::nothrow)Package();if(!p){free(bytes);return nullptr;}
        if(!p->parse(bytes,n-12)){delete p;return nullptr;}p->generation=u32(head+4);return p;
    }
    void load(){
        if(!ready)return;
        auto root=LittleFS.open("/");auto file=root.openNextFile();
        while(file){char id[49]={};const char *path=file.path();unsigned n=strlen(path);
            if(n>3&&path[0]=='/'&&path[n-2]=='.'&&(path[n-1]=='a'||path[n-1]=='b')&&n-3<sizeof(id)){
                memcpy(id,path+1,n-3);if(validId(id)&&find(id)<int(decoding::MaxProfiles)){
                    bool already=false;for(auto *p:installed)if(p&&!strcmp(p->profile.id,id))already=true;
                    if(!already){char a[56],b[56];snprintf(a,sizeof(a),"/%s.a",id);snprintf(b,sizeof(b),"/%s.b",id);
                        auto *pa=read(a),*pb=read(b);if(pa&&strcmp(pa->profile.id,id)){delete pa;pa=nullptr;}if(pb&&strcmp(pb->profile.id,id)){delete pb;pb=nullptr;}
                        auto *best=pa&&(!pb||pa->generation>=pb->generation)?pa:pb;
                        if(best && !install(best)){Serial.printf("PROFILE_LOAD rejected %s\n",id);best=nullptr;}
                        if(pa&&pa!=best)delete pa;if(pb&&pb!=best)delete pb;
                    }
                }
            }
            file=root.openNextFile();
        }
    }
    bool commit(Package &p){
        if(!ready){error="storage_unavailable";return false;}
        char a[56],b[56];snprintf(a,sizeof(a),"/%s.a",p.profile.id);snprintf(b,sizeof(b),"/%s.b",p.profile.id);
        auto *pa=read(a),*pb=read(b);uint32_t ga=pa?pa->generation:0,gb=pb?pb->generation:0;delete pa;delete pb;
        uint32_t generation=(ga>gb?ga:gb)+1;if(!generation){error="generation_exhausted";return false;}
        const char *target=ga<=gb?a:b;
        // Write only the inactive slot. A complete CRC-valid record is the commit
        // marker; interrupted writes cannot replace the other valid slot.
        auto file=LittleFS.open(target,"w");if(!file){error="storage_open";return false;}
        uint8_t head[12]={'P','R','F','S'};for(unsigned i=0;i<4;++i)head[4+i]=uint8_t(generation>>(8*i));uint32_t check=crc32(head,8);for(unsigned i=0;i<4;++i)head[8+i]=uint8_t(check>>(8*i));
        bool ok=file.write(head,12)==12&&file.write(p.bytes,p.size)==p.size;file.flush();file.close();
        auto *verified=ok?read(target):nullptr;
        ok=verified&&verified->generation==generation&&verified->crc==p.crc&&verified->size==p.size&&!memcmp(verified->bytes,p.bytes,p.size);
        delete verified;if(!ok){LittleFS.remove(target);error="storage_verify";return false;}return true;
    }
};
}
