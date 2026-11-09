#include <stdio.h>
#include <string.h>
#include "wifi_target.h"
#include "host_assert.h"
static unsigned char bss[512];
static struct brcm_scan_state scan;
static struct brcm_caps_state caps;
static const struct brcm_scan_ap *target;
static const unsigned char rsn[]={48,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,192,0,0,0,0,15,172,6};
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p,unsigned v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static unsigned fixture(void)
{
    memset(bss,0,sizeof(bss));put32(bss,109);bss[8]=2;bss[13]=1;put16(bss+16,16);
    bss[18]=6;memcpy(bss+19,"Jack's",6);put16(bss+72,0x1006);put16(bss+78,(uint16_t)-44);bss[88]=6;
    put16(bss+116,126);memcpy(bss+126,rsn,sizeof(rsn));
    unsigned n=126+sizeof(rsn);put32(bss+4,n);put32(bss+120,n-126);return n;
}
static void seed(void)
{
    scan=(struct brcm_scan_state){.ready=true,.error=BLOCK_OK,.interface_down=true,.mask_restored=true,.count=1};
    caps=(struct brcm_caps_state){.ready=true,.error=BLOCK_OK,.external_sae=true,.mfp_known=true,.mfp_supported=true};
    unsigned n=fixture();assert(brcm_scan_bss(bss,n,&scan.ap[0])==BLOCK_OK);target=NULL;
}
static enum wifi_target_result check(void) { return wifi_target_check(&scan,&caps,"Jack's",6,&target); }
static unsigned parse(unsigned n)
{ put32(bss+4,n);put32(bss+120,n-126);return brcm_scan_bss(bss,n,&scan.ap[0]); }
int main(void)
{
    seed();assert(check()==WIFI_TARGET_OK && target==&scan.ap[0]);
    assert(target->group_cipher==0x000fac04 && target->management_cipher==0x000fac06 && target->pairwise_ccmp && target->akm_sae);
    assert(wifi_target_check(&scan,&caps,"Jack's!",7,&target)==WIFI_TARGET_NOT_FOUND && !target);
    assert(wifi_target_check(&scan,&caps,"jack's",6,&target)==WIFI_TARGET_NOT_FOUND);
    assert(wifi_target_check(&scan,&caps,NULL,0,&target)==WIFI_TARGET_ARGUMENT && !target);
    assert(wifi_target_check(&scan,&caps,bss,33,&target)==WIFI_TARGET_ARGUMENT);
    assert(wifi_target_check(NULL,&caps,bss,1,&target)==WIFI_TARGET_ARGUMENT);
    assert(wifi_target_check(&scan,&caps,bss,1,NULL)==WIFI_TARGET_ARGUMENT);
    seed();scan.ap[0].security=SCAN_PRIVACY;assert(check()==WIFI_TARGET_PRIVACY);
    seed();scan.ap[0].akm_sae=false;scan.ap[0].security|=SCAN_PSK;assert(check()==WIFI_TARGET_AKM);
    seed();scan.ap[0].pairwise_ccmp=false;assert(check()==WIFI_TARGET_CIPHER);
    seed();scan.ap[0].group_cipher=0x000fac02;assert(check()==WIFI_TARGET_CIPHER);
    seed();scan.ap[0].pmf_capable=false;assert(check()==WIFI_TARGET_PMF);
    seed();scan.ap[0].pmf_required=false;assert(check()==WIFI_TARGET_OK); /* STA still requires PMF. */
    seed();scan.ap[0].management_cipher=0x000fac0b;assert(check()==WIFI_TARGET_MANAGEMENT);
    seed();caps.external_sae=false;assert(check()==WIFI_TARGET_FIRMWARE);
    seed();caps.mfp_known=false;assert(check()==WIFI_TARGET_FIRMWARE);
    seed();caps.mfp_supported=false;assert(check()==WIFI_TARGET_FIRMWARE);
    seed();caps.ready=false;assert(check()==WIFI_TARGET_FIRMWARE);
    seed();caps.error=BLOCK_IO;assert(check()==WIFI_TARGET_FIRMWARE);
    for (unsigned bad=0;bad<8;++bad) {
        seed();switch (bad) {
        case 0:scan.ready=false;break;case 1:scan.interface_down=false;break;
        case 2:scan.mask_restored=false;break;case 3:scan.interface_may_be_up=true;break;
        case 4:scan.truncated=true;break;case 5:scan.malformed=1;break;
        case 6:scan.count=BRCM_SCAN_AP_MAX+1;break;default:scan.error=BLOCK_IO;break;
        }assert(check()==WIFI_TARGET_SCAN && !target);
    }
    seed();scan.ap[1]=scan.ap[0];scan.ap[1].bssid[5]=2;scan.count=2;assert(check()==WIFI_TARGET_AMBIGUOUS && !target);
    /* Exact SAE versus FT-SAE, and OUI-aware cipher selection. */
    seed();bss[126+19]=9;assert(parse(154)==BLOCK_OK && (scan.ap[0].security&SCAN_SAE) && !scan.ap[0].akm_sae);assert(check()==WIFI_TARGET_AKM);
    seed();bss[126+10]=1;assert(parse(154)==BLOCK_OK && !scan.ap[0].pairwise_ccmp);assert(check()==WIFI_TARGET_CIPHER);
    seed();bss[126+6]=1;assert(parse(154)==BLOCK_OK && scan.ap[0].group_cipher==0x000f0104);assert(check()==WIFI_TARGET_CIPHER);
    /* Optional management suite: default, explicit unknown OUI, PMKID bounds. */
    seed();bss[127]=20;assert(parse(148)==BLOCK_OK && scan.ap[0].management_cipher==0x000fac06);assert(check()==WIFI_TARGET_OK);
    seed();bss[126+24]=1;assert(parse(154)==BLOCK_OK);assert(check()==WIFI_TARGET_MANAGEMENT);
    seed();bss[126+22]=255;assert(parse(154)==BLOCK_RANGE);
    seed();bss[126+20]=64;assert(parse(154)==BLOCK_RANGE); /* MFPR without MFPC. */
    /* Retain all RSNXE octets; reject duplicate, inconsistent and zero lengths. */
    seed();bss[154]=244;bss[155]=1;bss[156]=32;assert(parse(157)==BLOCK_OK && scan.ap[0].rsnxe_length==1 && scan.ap[0].rsnxe[0]==32);assert(check()==WIFI_TARGET_OK);
    bss[157]=244;bss[158]=1;bss[159]=32;assert(parse(160)==BLOCK_RANGE);
    seed();bss[154]=244;bss[155]=0;assert(parse(156)==BLOCK_RANGE);
    seed();bss[154]=244;bss[155]=1;bss[156]=33;assert(parse(157)==BLOCK_RANGE);
    seed();bss[154]=244;bss[155]=2;bss[156]=33;bss[157]=128;assert(parse(158)==BLOCK_OK && scan.ap[0].rsnxe[1]==128);
    seed();bss[154]=244;bss[155]=16;bss[156]=15;assert(parse(172)==BLOCK_OK && scan.ap[0].rsnxe_length==16);
    seed();bss[154]=244;bss[155]=17;bss[156]=15;assert(parse(173)==BLOCK_RANGE);
    seed();unsigned n=fixture();memcpy(bss+n,rsn,sizeof(rsn));assert(parse(n+sizeof(rsn))==BLOCK_RANGE);
    /* A truncation at every byte of the complete BSS is never accepted. */
    n=fixture();for (unsigned i=0;i<n;++i) { struct brcm_scan_ap ap;assert(brcm_scan_bss(bss,i,&ap)!=BLOCK_OK); }
    /* Arbitrary SSIDs remain bounded byte strings, including embedded NUL. */
    seed();scan.ap[0].ssid[1]=0;const unsigned char binary[]={'J',0,'c','k','\'', 's'};
    assert(wifi_target_check(&scan,&caps,binary,sizeof(binary),&target)==WIFI_TARGET_OK);
    puts("PASS: exact WPA3 target/cipher/PMF checks, no PSK or FT-SAE fallback, ambiguous/unclean scan refusal, RSNXE bounds, PMKIDs and truncated advertisements");return 0;
}
