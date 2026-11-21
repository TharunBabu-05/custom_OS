/* Reuse the independently constructed media/cache model and legacy helpers. */
#define main legacy_main
#include "persist_host.c"
#undef main
static unsigned char pattern[DATA_V4_FILE_BYTES+1],output[DATA_V4_FILE_BYTES];
static unsigned char old_payload[DATA_V4_PAYLOAD_BYTES],new_payload[DATA_V4_PAYLOAD_BYTES];
static bool all_zero(const unsigned char *p,size_t n) { while (n--) { if (*p++) { return false; } }return true; }
static void workspace_clean(void)
{
    assert(!v.busy && !v.restoring && v.staged_slot==v.files);
    assert(all_zero(v.staged_directory,sizeof(v.staged_directory)) && all_zero(v.staged_file,sizeof(v.staged_file)));
}
static void capacity(void)
{
    genesis_v4();assert(persist_mount(&v,&device)==FS_OK && v.version==4 && v.file_bytes==16384 && v.files==16 && !writes);
    assert(persist_mkdir(&v,"/a")==FS_OK && persist_mkdir(&v,"/b")==FS_OK);
    assert(persist_write(&v,"/a/large",pattern,16384,false)==FS_OK);size_t n;
    assert(persist_read(&v,"/a/large",0,output,sizeof(output),&n)==FS_OK && n==16384 && !memcmp(output,pattern,n));
    assert(persist_read(&v,"/a/large",8190,output,8,&n)==FS_OK && n==8 && !memcmp(output,pattern+8190,n));
    assert(persist_read(&v,"/a/large",16383,output,8,&n)==FS_OK && n==1 && output[0]==pattern[16383]);
    assert(persist_read(&v,"/a/large",16384,output,8,&n)==FS_OK && !n);
    unsigned before=events;assert(persist_write(&v,"/a/large",pattern,16385,false)==FS_FILE_TOO_LARGE && before==events);
    assert(persist_rename(&v,"/a/large","/b/large")==FS_OK && persist_write(&v,"/a/large","old",3,false)==FS_OK);
    for (unsigned i=0;i<12;++i) { char path[32];snprintf(path,sizeof(path),"/slot%u",i);assert(persist_write(&v,path,"x",1,false)==FS_OK); }
    assert(persist_write(&v,"/full","x",1,false)==FS_NO_NODES);
    /* At a full table, consume source in place and clear the old target's slot. */
    assert(persist_publish(&v,"/b/large","/a/large")==FS_OK && persist_write(&v,"/reused","x",1,false)==FS_OK);
    assert(persist_read(&v,"/a/large",0,output,sizeof(output),&n)==FS_OK && n==16384 && !memcmp(output,pattern,n));workspace_clean();
    assert(persist_mount(&reopened,&device)==FS_OK && !memcmp(v.live,reopened.live,v.payload_bytes));
    assert(persist_write(&v,"/a/large","z",1,false)==FS_OK);workspace_clean();
    assert(persist_mount(&reopened,&device)==FS_OK);read_equals(&reopened,"/a/large","z");
    assert(persist_remove(&v,"/a/large",false)==FS_OK && persist_remove(&v,"/a",true)==FS_OK);
    assert(persist_mount(&reopened,&device)==FS_OK && !memcmp(v.live,reopened.live,v.payload_bytes));
    puts("PASS: v4 16 KiB binary/EOF/oversize bounds, nested paths, full-table publication, freed-slot reuse and zeroed tails/workspace.");
}
static void commit_cuts(unsigned operation)
{
    genesis_v4();assert(persist_mount(&v,&device)==FS_OK && persist_write(&v,"/target","old",3,false)==FS_OK);
    if (operation) { assert(persist_write(&v,"/stage",pattern,16384,false)==FS_OK); }
    memcpy(original,durable,sizeof(original));memcpy(old_payload,v.live,v.payload_bytes);events=0;
    assert((operation ? persist_publish(&v,"/stage","/target") : persist_write(&v,"/target",pattern,16384,false))==FS_OK);
    unsigned count=events;assert(count==1034);memcpy(new_payload,v.live,v.payload_bytes);
    for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
        memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);
        events=0;fail_event=point;torn=mode!=0;
        assert((operation ? persist_publish(&v,"/stage","/target") : persist_write(&v,"/target",pattern,16384,false))==FS_IO);
        assert(v.failed && !memcmp(v.live,old_payload,v.payload_bytes));workspace_clean();update_diagnostics(point);
        unsigned before=events;assert(persist_write(&v,"/retry","x",1,false)==FS_IO && events==before);
        memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
        assert(!memcmp(reopened.live,old_payload,reopened.payload_bytes) || !memcmp(reopened.live,new_payload,reopened.payload_bytes));
        assert(!memcmp(durable,original,(START+1)*512) && !memcmp(durable+(START+DATA_V4_MIN_SECTORS)*512,original+(START+DATA_V4_MIN_SECTORS)*512,(SECTORS-START-DATA_V4_MIN_SECTORS)*512));
    } }
    printf("PASS: all %u v4 %s interruption/torn-write outcomes preserve the old RAM view and recover one complete durable snapshot.\n",count*2,operation ? "publication" : "large write");fflush(stdout);
}
static void emit_count(const char *key,bool directory,size_t bytes,void *context)
{ (void)key;(void)directory;(void)bytes;unsigned *count=context;++*count; }
static bool guarded_input(void *buffer,size_t count,void *context)
{
    if (v.restoring) {
        bool directory;size_t n=99;unsigned entries=0;unsigned before=events;
        assert(persist_stat(&v,"/",&directory)==FS_BUSY && persist_read(&v,"/target",0,output,1,&n)==FS_BUSY && !n);
        assert(persist_list(&v,"/",emit_count,&entries)==FS_BUSY && !entries);
        assert(persist_sync(&v)==FS_BUSY && persist_mkdir(&v,"/no")==FS_BUSY && persist_write(&v,"/no","x",1,false)==FS_BUSY && events==before);
    }
    return import_bytes(buffer,count,context);
}
static void restore_checks(void)
{
    genesis_v4();assert(persist_mount(&v,&device)==FS_OK && persist_mkdir(&v,"/docs")==FS_OK && persist_write(&v,"/docs/large",pattern,16384,false)==FS_OK);
    struct archive_io io={0};assert(persist_export(&v,export_bytes,&io)==FS_OK && io.offset==263680);size_t length=io.offset;
    genesis_v4();assert(persist_mount(&v,&device)==FS_OK);memcpy(original,durable,sizeof(original));
    for (unsigned point=1;point<=515;++point) {
        io=(struct archive_io){.length=length,.fail_call=point};unsigned before=events;
        assert(persist_restore(&v,length,guarded_input,&io)==FS_IO && !v.failed && events==before && all_zero(v.live,v.payload_bytes));workspace_clean();
    }
    archive[1536]^=1;io=(struct archive_io){.length=length};unsigned before=events;
    assert(persist_restore(&v,length,guarded_input,&io)==FS_CORRUPT && before==events && !v.failed && all_zero(v.live,v.payload_bytes));archive[1536]^=1;
    events=0;io=(struct archive_io){.length=length};assert(persist_restore(&v,length,guarded_input,&io)==FS_OK && !memcmp(v.live,archive+512,v.payload_bytes));unsigned count=events;
    for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
        memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);
        events=0;fail_event=point;torn=mode!=0;io=(struct archive_io){.length=length};
        assert(persist_restore(&v,length,import_bytes,&io)==FS_IO && v.failed && all_zero(v.live,v.payload_bytes));workspace_clean();
        memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
        assert(all_zero(reopened.live,reopened.payload_bytes) || !memcmp(reopened.live,archive+512,reopened.payload_bytes));
    } }
    puts("PASS: v4 515 truncated-input positions, corrupt archive and reentrant reads refused before any disk write; 2068 restore interruption outcomes recover empty or exact imported tree.");fflush(stdout);
}
static unsigned bank0_headers,change_identity;
static enum block_result selection_read(void *context,uint64_t lba,unsigned char *buffer)
{
    if (lba==START+1 && ++bank0_headers==2 && change_identity) {
        unsigned char *h=media+(START+1)*512;
        if (change_identity==1) { p32(h+16,9); }
        else { h[512+DATA_V4_DIRECTORY_BYTES]^=1;digest(h+512,DATA_V4_PAYLOAD_BYTES,h+32); }
        digest(h,64,h+64);
    }
    return read_sector(context,lba,buffer);
}
static void selection(void)
{
    genesis_v4();assert(persist_mount(&v,&device)==FS_OK && persist_write(&v,"/x","a",1,false)==FS_OK && persist_write(&v,"/x","b",1,false)==FS_OK);
    memcpy(original,durable,sizeof(original));struct block_device d=device;d.read=selection_read;
    reset_counters();bank0_headers=change_identity=0;assert(persist_mount(&reopened,&d)==FS_OK && reopened.generation==3 && bank0_headers==2);read_equals(&reopened,"/x","b");
    unsigned count=events;assert(count==1548);
    for (unsigned point=1033;point<=count;++point) {
        reset_counters();bank0_headers=0;fail_event=point;assert(persist_mount(&reopened,&d)==FS_IO && !reopened.ready && !writes);
    }
    for (unsigned change=1;change<=2;++change) {
        memcpy(media,original,sizeof(media));reset_counters();bank0_headers=0;change_identity=change;
        assert(persist_mount(&reopened,&d)==FS_CORRUPT && !reopened.ready && !writes);
    }
    memcpy(media,original,sizeof(media));reset_counters();media[(START+2)*512]^=1;
    assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==2 && reopened.valid_banks==1);read_equals(&reopened,"/x","a");
    for (unsigned bad=0;bad<3;++bad) {
        genesis_v4();unsigned char *s=media+START*512;
        if (!bad) { p32(s+8,5); }else if (bad==1) { p32(s+20,259); }else { p32(s+28,8192); }digest(s,64,s+64);
        assert(persist_mount(&reopened,&device)!=FS_OK && !reopened.ready && !writes);
    }
    puts("PASS: v4 selected-bank revalidation, all 516 reload/flush failures, changed generation/digest, corrupt-newest fallback and strict version/geometry refusal.");
}
static void recovery(void)
{
    void (*const create[])(void)={genesis,genesis_v2,genesis_v3,genesis_v4};
    for (unsigned i=0;i<sizeof(create)/sizeof(create[0]);++i) {
        create[i]();
        assert(persist_mount(&v,&device)==FS_OK && v.version==i+1);
        reset_counters();
        struct persist_report report;
        assert(persist_check(&v,&report)==FS_OK && report.mounted_bank==0 && report.mounted_generation==1);
        assert(report.banks[0].state==PERSIST_BANK_VALID && report.banks[0].matches_mounted);
        assert(report.banks[1].state==PERSIST_BANK_EMPTY);
        struct archive_io io={0};
        assert(persist_export_previous(&v,export_bytes,&io)==FS_NOT_FOUND && !io.calls && !writes && !flushes);

        assert(persist_write(&v,"/note",pattern,v.file_bytes,false)==FS_OK);
        memcpy(old_payload,v.live,v.payload_bytes);
        assert(persist_write(&v,"/note","latest",6,false)==FS_OK);
        memcpy(new_payload,v.live,v.payload_bytes);
        memcpy(original,media,sizeof(media));
        reset_counters();
        assert(persist_check(&v,&report)==FS_OK && report.mounted_generation==3);
        assert(report.banks[0].state==PERSIST_BANK_VALID && report.banks[0].matches_mounted);
        assert(report.banks[1].state==PERSIST_BANK_VALID && report.banks[1].generation==2);

        assert(persist_export_previous(&v,export_bytes,&io)==FS_OK);
        assert(io.offset==512+v.payload_bytes && archive[12]==0 && archive[16]==2);
        assert(!memcmp(archive+512,old_payload,v.payload_bytes));
        size_t archive_bytes=io.offset;

        uint64_t previous=(uint64_t)START+1+v.bank_sectors;
        reset_counters();fail_event=1;
        assert(persist_check(&v,&report)==FS_OK && report.banks[0].state==PERSIST_BANK_UNREADABLE);
        assert(report.banks[0].io==BLOCK_IO && report.banks[0].lba==START+1 && report.banks[1].state==PERSIST_BANK_VALID);
        reset_counters();fail_event=1;io=(struct archive_io){0};
        assert(persist_export_previous(&v,export_bytes,&io)==FS_IO && !io.calls);

        /* A change between validation and emission must invalidate the export. */
        reset_counters();corrupt_event=v.bank_sectors+1;io=(struct archive_io){0};
        assert(persist_export_previous(&v,export_bytes,&io)==FS_CORRUPT && io.offset==archive_bytes);
        reset_counters();io=(struct archive_io){.fail_call=2};
        assert(persist_export_previous(&v,export_bytes,&io)==FS_IO && io.offset==512);

        unsigned char *header=media+previous*512;
        header[0]^=1;
        assert(persist_check(&v,&report)==FS_OK && report.banks[1].state==PERSIST_BANK_BAD_HEADER);
        header[0]^=1;
        header[512+v.directory_bytes]^=1;
        assert(persist_check(&v,&report)==FS_OK && report.banks[1].state==PERSIST_BANK_BAD_PAYLOAD);
        io=(struct archive_io){0};
        assert(persist_export_previous(&v,export_bytes,&io)==FS_CORRUPT && !io.calls);
        header[512+v.directory_bytes]^=1;
        assert(!memcmp(media,original,sizeof(media)) && !memcmp(v.live,new_payload,v.payload_bytes));

        /* A valid bank can differ from the mounted bytes or be newer after
         * an ambiguous commit. Neither case counts as the mounted snapshot. */
        unsigned char *active=media+(START+1)*512;
        active[512+v.directory_bytes]^=1;
        digest(active+512,v.payload_bytes,active+32);digest(active,64,active+64);
        p32(header+16,4);digest(header,64,header+64);
        assert(persist_check(&v,&report)==FS_OK);
        assert(report.banks[0].state==PERSIST_BANK_VALID && !report.banks[0].matches_mounted);
        assert(report.banks[1].state==PERSIST_BANK_VALID && report.banks[1].generation==4);
        assert(persist_export_previous(&v,export_bytes,&io)==FS_NOT_FOUND && !io.calls);
        memcpy(media,original,sizeof(media));
        assert(!writes && !flushes && v.generation==3 && v.valid_banks==2);
        workspace_clean();

        /* After an ambiguous write/flush failure the check stays available. */
        reset_counters();fail_event=1;
        assert(persist_sync(&v)==FS_IO && v.failed);
        reset_counters();
        assert(persist_check(&v,&report)==FS_OK && report.banks[0].matches_mounted);
        assert(persist_export_previous(&v,export_bytes,&io)==FS_OK && io.offset==archive_bytes);
        assert(!writes && !flushes && v.failed);

        create[i]();
        assert(persist_mount(&reopened,&device)==FS_OK);
        io=(struct archive_io){.length=archive_bytes};
        assert(persist_restore(&reopened,io.length,import_bytes,&io)==FS_OK);
        assert(!memcmp(reopened.live,old_payload,reopened.payload_bytes));
    }
    puts("PASS: v1-v4 bank checks, read errors, corrupt banks, interrupted export, previous archive restore and untouched live/media state.");
    fflush(stdout);
}
int main(void)
{
    for (unsigned i=0;i<sizeof(pattern);++i) { pattern[i]=(unsigned char)(i*29+7); }
    recovery();capacity();commit_cuts(0);commit_cuts(1);restore_checks();selection();return 0;
}
