#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "wpa_group_rekey.h"
#include "aes.h"

static size_t unhex(const char *text,unsigned char *out,size_t capacity)
{
    size_t n=strlen(text)/2;
    assert(n<=capacity && strlen(text)==2*n);
    for (size_t i=0;i<n;++i) {
        unsigned byte;
        assert(sscanf(text+2*i,"%2x",&byte)==1);
        out[i]=(unsigned char)byte;
    }
    return n;
}
static void rejected(const struct wpa_group_context *context,const void *frame,size_t n,enum wpa_group_result expect)
{
    struct wpa_group_offer offer;
    memset(&offer,0xcc,sizeof(offer));
    enum wpa_group_result result=wpa_group_validate(context,frame,n,&offer);
    assert(expect==WPA_GROUP_OK ? result!=WPA_GROUP_OK : result==expect);
    const unsigned char *bytes=(const void *)&offer;
    for (size_t i=0;i<sizeof(offer);++i) { assert(!bytes[i]); }
}
int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *fixture=fopen(argv[1],"r");
    assert(fixture);
    char line[2400];
    unsigned count=0;
    while (fgets(line,sizeof(line),fixture)) {
        char *words[6];unsigned used=0;
        for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(used<6);words[used++]=p; }
        assert(used==6);
        struct wpa_group_context context={0};
        unsigned char frame[633],response[113],gtk[16],igtk[16];
        assert(unhex(words[0],context.kck,16)==16 && unhex(words[1],context.kek,16)==16);
        size_t n=unhex(words[2],frame,sizeof(frame));
        assert(n==177 && unhex(words[3],response,113)==113 &&
               unhex(words[4],gtk,16)==16 && unhex(words[5],igtk,16)==16);
        memcpy(context.local,frame,6);memcpy(context.peer,frame+6,6);
        context.version=frame[14];context.last_replay[7]=255;
        struct wpa_group_offer offer;
        assert(wpa_group_validate(&context,frame,n,&offer)==WPA_GROUP_OK);
        assert(!memcmp(offer.reply,response,113) && !memcmp(offer.replay,frame+23,8) &&
               !memcmp(offer.keys.gtk,gtk,16) && !memcmp(offer.keys.igtk,igtk,16));
        assert(offer.keys.gtk_id==count%4 && !offer.keys.gtk_tx &&
               offer.keys.igtk_id==4+count%2 && offer.keys.rsc[0]==count+1 && offer.keys.ipn[5]==count);
        memcpy(context.last_replay,offer.replay,8);
        rejected(&context,frame,n,WPA_GROUP_REPLAY);
        memset(context.last_replay,0xff,8);rejected(&context,frame,n,WPA_GROUP_REPLAY);
        memset(context.last_replay,0,8);
        assert(wpa_group_validate(&context,frame,n,&offer)==WPA_GROUP_OK);
        for (size_t cut=0;cut<n;++cut) { rejected(&context,frame,cut,WPA_GROUP_FORMAT); }
        for (size_t at=0;at<n;++at) {
            frame[at]^=1;rejected(&context,frame,n,WPA_GROUP_OK);frame[at]^=1;
        }
        context.kek[0]^=1;rejected(&context,frame,n,WPA_GROUP_UNWRAP);context.kek[0]^=1;
        frame[85]=1;memset(frame+95,0,16);
        unsigned char mic[16];
        assert(aes128_cmac(context.kck,frame+14,n-14,mic));memcpy(frame+95,mic,16);
        rejected(&context,frame,n,WPA_GROUP_KEY_DATA);
        ++count;
    }
    fclose(fixture);assert(count==16);
    puts("PASS: 16 independent signed and wrapped group rekey messages, exact offline replies, MIC/unwrap, replay, peer/length/profile and counter checks");
    return 0;
}
