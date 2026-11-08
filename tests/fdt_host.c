/* Native Windows tests. Host libc/Win32 are used only by this test executable. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "board.h"
static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static void write32(unsigned char *p, uint32_t value)
{
    p[0]=(unsigned char)(value>>24); p[1]=(unsigned char)(value>>16);
    p[2]=(unsigned char)(value>>8); p[3]=(unsigned char)value;
}
int main(int argc, char **argv)
{
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb"); assert(file);
    assert(fseek(file,0,SEEK_END)==0);
    long file_size=ftell(file); assert(file_size>=40 && file_size<=0x200000);
    rewind(file);
    size_t size=(size_t)file_size;
    unsigned char *original=malloc(size); assert(original);
    assert(fread(original,1,size,file)==size); fclose(file);
    SYSTEM_INFO system; GetSystemInfo(&system);
    size_t page=system.dwPageSize;
    size_t rounded=(size+page-1)/page*page;
    unsigned char *allocation=VirtualAlloc(NULL,rounded+page,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(allocation);
    DWORD old_protection;
    assert(VirtualProtect(allocation+rounded,page,PAGE_NOACCESS,&old_protection));
    unsigned char *buffer=allocation+rounded-size;
    memcpy(buffer,original,size);
    struct board_info info={0};
    assert(fdt_read_board(buffer,size,&info));
    assert(info.uart_base==0x09000000 && info.uart_clock==24000000);
    assert(info.ram_base==0x40000000 && info.ram_size==128UL*1024*1024);
    for (size_t length=0; length<40; ++length) {
        assert(!fdt_read_board(buffer+size-length,length,&info));
    }
    assert(!fdt_read_board(buffer,read32(buffer+4)-1,&info));
    assert(!fdt_read_board(NULL,size,&info));
    assert(!fdt_read_board(buffer,size,NULL));
    const unsigned fields[]={0,4,8,12,24,32,36};
    for (size_t i=0; i<sizeof(fields)/sizeof(fields[0]); ++i) {
        memcpy(buffer,original,size);
        write32(buffer+fields[i],0xfffffffc);
        assert(!fdt_read_board(buffer,size,&info));
    }
    memcpy(buffer,original,size);
    write32(buffer+20,16); assert(!fdt_read_board(buffer,size,&info));
    memcpy(buffer,original,size);
    uint32_t structure=read32(buffer+8), structure_size=read32(buffer+36);
    write32(buffer+structure,2); assert(!fdt_read_board(buffer,size,&info));
    memcpy(buffer,original,size);
    memset(buffer+read32(buffer+12),'X',read32(buffer+32));
    assert(!fdt_read_board(buffer,size,&info));
    /* Deterministic mutations against a guard page catch accidental overreads.
     * Mutated DTBs may legitimately remain valid; acceptance itself isn't failure. */
    uint32_t random=0x12345678;
    for (unsigned i=0; i<5000; ++i) {
        memcpy(buffer,original,size);
        random=random*1664525u+1013904223u;
        uint32_t at=structure+random%structure_size;
        buffer[at]^=(unsigned char)(1u<<(i%8));
        (void)fdt_read_board(buffer,size,&info);
    }
    VirtualFree(allocation,0,MEM_RELEASE); free(original);
    puts("PASS: DTB discovery, malformed headers/tokens/strings, truncation, 5000 guarded mutations");
    return 0;
}
