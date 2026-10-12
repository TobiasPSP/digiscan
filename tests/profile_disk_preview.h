#pragma once
#include <memory>
#include <algorithm>
// Fault-injectable filesystem boundary; the real Disk commit/read code is used.
struct PreviewFS;
struct File {
    PreviewFS *fs=nullptr;std::string filename;std::shared_ptr<std::vector<uint8_t>> bytes;
    unsigned at=0,dirIndex=0;bool directory=false;
    explicit operator bool()const{return directory||bool(bytes);}
    size_t size()const{return bytes?bytes->size():0;}
    size_t read(uint8_t *p,size_t n){if(!bytes)return 0;n=std::min(n,bytes->size()-at);memcpy(p,bytes->data()+at,n);at+=n;return n;}
    size_t write(const uint8_t *p,size_t n);
    void flush(){}void close(){}
    const char *path()const{return filename.c_str();}
    File openNextFile();
};
struct PreviewFS {
    std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
    int writeBudget=-1;bool mounted=true;
    bool begin(bool){return mounted;}
    File open(const char *path,const char *mode="r"){
        File f;f.fs=this;f.filename=path;
        if(!strcmp(path,"/")){f.directory=true;return f;}
        if(!strcmp(mode,"w"))files[path]=std::make_shared<std::vector<uint8_t>>();
        auto found=files.find(path);if(found!=files.end())f.bytes=found->second;return f;
    }
    bool remove(const char *path){return files.erase(path)!=0;}
} LittleFS;
inline size_t File::write(const uint8_t *p,size_t n){
    if(!bytes)return 0;if(fs->writeBudget>=0){n=std::min(n,size_t(fs->writeBudget));fs->writeBudget-=int(n);}
    if(at+n>bytes->size())bytes->resize(at+n);memcpy(bytes->data()+at,p,n);at+=n;return n;
}
inline File File::openNextFile(){
    if(!directory||dirIndex>=fs->files.size())return File{};
    auto it=fs->files.begin();std::advance(it,dirIndex++);return fs->open(it->first.c_str());
}
