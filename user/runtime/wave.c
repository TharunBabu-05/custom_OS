#include "file_input.h"
#include "wave.h"

static bool id(const uint8_t *bytes,const char *name)
{
    for (unsigned i=0;i<4;++i) {
        if (bytes[i]!=(uint8_t)name[i]) { return false; }
    }
    return true;
}

bool wave_read_exact(int64_t input,uint8_t *out,size_t bytes)
{
    while (bytes) {
        int64_t n=file_input_read(input,out,bytes);
        if (n<=0) { return false; }
        bytes-=(size_t)n;
        out+=n;
    }
    return true;
}

static bool format(const uint8_t *p,struct wave_info *info)
{
    info->channels=wave_u16(p+2);
    info->rate=wave_u32(p+4);
    info->align=wave_u16(p+12);
    info->bits=wave_u16(p+14);
    return wave_u16(p)==1 && info->channels>=1 && info->channels<=2 &&
        (info->bits==8 || info->bits==16) && info->rate && info->rate<=192000 &&
        info->align==info->channels*(info->bits/8) && wave_u32(p+8)==info->rate*info->align;
}

bool wave_scan(int64_t input,struct wave_info *info,wave_visitor visit,void *context)
{
    uint8_t buffer[WAVE_BUFFER_BYTES];
    if (!wave_read_exact(input,buffer,12) || !id(buffer,"RIFF") || !id(buffer+8,"WAVE")) { return false; }
    uint32_t size=wave_u32(buffer+4);
    if (size<4 || size>WAVE_MAX_RIFF_BYTES) { return false; }
    uint32_t remaining=size-4;
    bool have_format=false,have_data=false;
    while (remaining) {
        if (remaining<8 || !wave_read_exact(input,buffer,8)) { return false; }
        remaining-=8;
        uint32_t chunk=wave_u32(buffer+4),pad=chunk&1;
        if (chunk>remaining || pad>remaining-chunk) { return false; }
        uint32_t offset=8+size-remaining;
        remaining-=chunk+pad;
        bool data=id(buffer,"data");
        if (id(buffer,"fmt ")) {
            if (have_format || (chunk!=16 && chunk!=18) || !wave_read_exact(input,buffer,chunk) ||
                (chunk==18 && wave_u16(buffer+16)) || !format(buffer,info)) { return false; }
            have_format=true;
            continue;
        }
        if (data) {
            if (!have_format || have_data || chunk%info->align) { return false; }
            have_data=true;
            info->frames=chunk/info->align;
            info->data_offset=offset;
        }
        while (chunk) {
            size_t count=chunk<sizeof(buffer) ? chunk : sizeof(buffer);
            if (!wave_read_exact(input,buffer,count)) { return false; }
            if (data && visit) { visit(buffer,count,info,context); }
            chunk-=(uint32_t)count;
        }
        if (pad && !wave_read_exact(input,buffer,1)) { return false; }
    }
    return have_data && file_input_read(input,buffer,1)==0;
}

bool wave_write_header(const struct wave_info *info)
{
    uint64_t bytes=(uint64_t)info->frames*info->align;
    uint64_t riff_size=WAVE_HEADER_BYTES-8+bytes+(bytes&1);
    if (riff_size>WAVE_MAX_RIFF_BYTES) { return false; }
    uint8_t header[WAVE_HEADER_BYTES]={
        'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
        16,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'d','a','t','a',0,0,0,0
    };
    wave_put32(header+4,(uint32_t)riff_size);
    wave_put16(header+22,info->channels);
    wave_put32(header+24,info->rate);
    wave_put32(header+28,info->rate*info->align);
    wave_put16(header+32,info->align);
    wave_put16(header+34,info->bits);
    wave_put32(header+40,(uint32_t)bytes);
    return uos_write(header,sizeof(header))==sizeof(header);
}
