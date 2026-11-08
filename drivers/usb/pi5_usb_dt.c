#include "usb.h"
#include "dt_property.h"
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32|be32(p+4); }
static bool string_copy(char *to,size_t capacity,const unsigned char *p,uint32_t n)
{
    if (!n || n>capacity || p[n-1]) { return false; }
    for (uint32_t i=0;i<n-1;++i) { if (p[i]<32 || p[i]>126) { return false; } to[i]=(char)p[i]; }
    to[n-1]='\0'; return true;
}
static bool contains_string(const unsigned char *p,uint32_t n,const char *key)
{
    uint32_t at=0;
    while (at<n) {
        uint32_t start=at; while (at<n && p[at]) { ++at; } if (at==n) { return false; }
        uint32_t i=0; while (start+i<at && key[i] && p[start+i]==(unsigned char)key[i]) { ++i; }
        if (start+i==at && !key[i]) { return true; } ++at;
    }
    return false;
}
bool pi5_usb_describe(const void *dtb,size_t available,unsigned index,struct pi5_usb_description *out)
{
    if (!out || index>1) { return false; } *out=(struct pi5_usb_description){0};
    const unsigned char *p; uint32_t n;
    if (!dt_property(dtb,available,"/","compatible",&p,&n) ||
        !contains_string(p,n,"raspberrypi,5-model-b")) { return false; }
    if (!dt_property(dtb,available,"/aliases",index ? "usb1" : "usb0",&p,&n) ||
        !string_copy(out->path,sizeof(out->path),p,n) || out->path[0]!='/') { return false; }
    if (!dt_property(dtb,available,out->path,"compatible",&p,&n) ||
        !contains_string(p,n,"snps,dwc3")) { return false; }
    char parent[256]; unsigned last=0;
    for (unsigned i=0;out->path[i];++i) { parent[i]=out->path[i]; if (parent[i]=='/') { last=i; } }
    parent[last]='\0';
    if (!last || !dt_property(dtb,available,parent,"#address-cells",&p,&n) || n!=4 || be32(p)!=2 ||
        !dt_property(dtb,available,parent,"#size-cells",&p,&n) || n!=4 || be32(p)!=2) { return false; }
    if (!dt_property(dtb,available,out->path,"dr_mode",&p,&n) || n!=5 ||
        !contains_string(p,n,"host")) { return false; }
    if (!dt_property(dtb,available,out->path,"reg",&p,&n) || n!=16) { return false; }
    uint64_t base=be64(p),size=be64(p+8);
    if (base!=0xc040200000ULL+(uint64_t)index*0x100000 || size!=0x100000) { return false; }
    out->bus_address=base; out->span=(uint32_t)size;
    if (dt_property(dtb,available,out->path,"status",&p,&n)) {
        if (!string_copy(out->status,sizeof(out->status),p,n)) { return false; }
    } else {
        out->status[0]='o'; out->status[1]='k'; out->status[2]='a'; out->status[3]='y';
    }
    return true;
}
