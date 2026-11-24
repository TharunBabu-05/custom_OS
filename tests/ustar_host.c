#include "ustar.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static unsigned char archive[USTAR_LIMIT];
static const unsigned char sample[]={0,0x55,0xff};

static void append(size_t *offset,const char *path,bool directory,const void *data,uint32_t bytes)
{
    unsigned char *header=archive+*offset;
    assert(*offset+USTAR_BLOCK+bytes<=sizeof(archive));
    assert(ustar_make_header(header,path,bytes,directory));
    if (bytes) { memcpy(header+USTAR_BLOCK,data,bytes); }
    *offset+=USTAR_BLOCK+(bytes+USTAR_BLOCK-1)/USTAR_BLOCK*USTAR_BLOCK;
    assert(*offset<=sizeof(archive));
}

static void checksum(unsigned char *header)
{
    memset(header+148,' ',8);
    unsigned sum=0;
    for (unsigned i=0;i<USTAR_BLOCK;++i) { sum+=header[i]; }
    assert(snprintf((char *)header+148,7,"%06o",sum)==6);
}

static size_t sample_archive(void)
{
    memset(archive,0,sizeof(archive));
    size_t at=0;
    append(&at,"folder",true,NULL,0);
    append(&at,"folder/data.bin",false,sample,sizeof(sample));
    append(&at,"empty",true,NULL,0);
    return at+2*USTAR_BLOCK;
}

static void check_sample(size_t length)
{
    struct ustar_index index;
    assert(ustar_scan(archive,length,&index) && index.count==3);
    bool seen_file=false,seen_folder=false,seen_empty=false;
    for (unsigned i=0;i<index.count;++i) {
        struct ustar_member member;
        ustar_member_at(archive,index.offsets[i],&member);
        if (!strcmp(member.path,"folder/data.bin")) {
            assert(!member.directory && member.bytes==sizeof(sample));
            assert(!memcmp(archive+index.offsets[i]+USTAR_BLOCK,sample,sizeof(sample)));
            seen_file=true;
        } else if (!strcmp(member.path,"folder")) {
            assert(member.directory && !member.bytes);seen_folder=true;
        } else {
            assert(!strcmp(member.path,"empty") && member.directory && !member.bytes);seen_empty=true;
        }
    }
    assert(seen_file && seen_folder && seen_empty);
}

static void invalid_headers(void)
{
    struct ustar_index index;
    size_t length=sample_archive();
    assert(!ustar_scan(archive,length-1,&index));
    assert(!ustar_scan(archive,length-USTAR_BLOCK,&index));
    archive[length-1]=1;
    assert(!ustar_scan(archive,length,&index));
    length=sample_archive();
    archive[148]='9';
    assert(!ustar_scan(archive,length,&index));
    const char *const names[]={"/folder","../folder","folder/../empty","a//b","a\\b","a:drive"};
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        length=sample_archive();
        memset(archive,0,100);memcpy(archive,names[i],strlen(names[i]));
        checksum(archive);
        assert(!ustar_scan(archive,length,&index));
        assert(!ustar_make_header(archive,names[i],0,false));
    }
    length=sample_archive();
    archive[156]='2';checksum(archive);
    assert(!ustar_scan(archive,length,&index));
    length=sample_archive();
    memset(archive+USTAR_BLOCK+124,'7',11);checksum(archive+USTAR_BLOCK);
    assert(!ustar_scan(archive,length,&index));
}

static void path_conflicts_and_limits(void)
{
    struct ustar_index index;
    size_t length=sample_archive();
    unsigned char *last=archive+3*USTAR_BLOCK;
    memset(last,0,100);memcpy(last,"./folder/",9);checksum(last);
    assert(!ustar_scan(archive,length,&index));
    length=sample_archive();
    archive[156]='0';checksum(archive);
    assert(!ustar_scan(archive,length,&index));

    memset(archive,0,sizeof(archive));
    const char path[]="abcdefghijklmnopqrstuvwx/abcdefghijklmnopqrstuvwx/abcdefghijklmnopqrstuvwx/"
                      "abcdefghijklmnopqrstuvwx/abcdefghijklmnopqrstuvwx";
    assert(strlen(path)>100);
    size_t at=0;
    append(&at,path,false,sample,sizeof(sample));
    assert(ustar_scan(archive,at+2*USTAR_BLOCK,&index) && index.count==1);
    struct ustar_member member;
    ustar_member_at(archive,index.offsets[0],&member);
    assert(!strcmp(member.path,path));

    memset(archive,0,sizeof(archive));
    assert(ustar_scan(archive,2*USTAR_BLOCK,&index) && !index.count);
    at=0;
    for (unsigned i=0;i<USTAR_MEMBERS;++i) {
        char name[12];
        assert(snprintf(name,sizeof(name),"node%u",i)>0);
        append(&at,name,false,NULL,0);
    }
    assert(at+2*USTAR_BLOCK==sizeof(archive));
    assert(ustar_scan(archive,sizeof(archive),&index) && index.count==USTAR_MEMBERS);
}

int main(int argc,char **argv)
{
    size_t bytes=sample_archive();
    check_sample(bytes);
    invalid_headers();
    path_conflicts_and_limits();
    if (argc==3) {
        FILE *file=fopen(argv[1],"rb");
        assert(file);
        bytes=fread(archive,1,sizeof(archive),file);
        assert(fgetc(file)==EOF && !ferror(file) && !fclose(file));
        check_sample(bytes);
        bytes=sample_archive();
        file=fopen(argv[2],"wb");
        assert(file && fwrite(archive,1,bytes,file)==bytes && !fclose(file));
    } else { assert(argc==1); }
    puts("PASS: ustar round trip, nested names, prefix fields, archive limits, and malformed headers.");
    return 0;
}
