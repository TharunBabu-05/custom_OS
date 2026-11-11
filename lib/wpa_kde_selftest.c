#include "wpa_kde.h"
#include "crypto.h"
bool wpa_kde_selftest(void)
{
    static const unsigned char rsn[]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,2,0,15,172,8,128,0};
    struct { unsigned char plain[80];struct wpa_group_keys keys;struct wpa_kde_info info; } w={0};
    for (unsigned i=0;i<26;++i) { w.plain[i]=rsn[i]; }
    static const unsigned char gtk[]={221,22,0,15,172,1,1,0},igtk[]={221,28,0,15,172,9,4,0};
    for (unsigned i=0;i<8;++i) { w.plain[26+i]=gtk[i];w.plain[50+i]=igtk[i]; }
    for (unsigned i=0;i<16;++i) { w.plain[34+i]=(unsigned char)i;w.plain[64+i]=(unsigned char)(i+16); }
    for (unsigned i=0;i<6;++i) { w.plain[58+i]=(unsigned char)(i+7); }
    const unsigned char rsc[8]={1,2,3,4,5,6,0,0};
    bool ok=wpa_sae_key_data(rsn,sizeof(rsn),w.plain,sizeof(w.plain),rsc,&w.info,&w.keys)==KDE_OK && w.info.rsn_match && w.info.gtk_seen && w.info.igtk_seen && w.keys.gtk_id==1 && w.keys.igtk_id==4;
    for (unsigned i=0;i<16;++i) { ok=ok && w.keys.gtk[i]==i && w.keys.igtk[i]==i+16; }
    for (unsigned i=0;i<6;++i) { ok=ok && w.keys.rsc[i]==i+1 && w.keys.ipn[i]==i+7; }
    w.plain[4]^=1;ok=ok && wpa_sae_key_data(rsn,sizeof(rsn),w.plain,sizeof(w.plain),rsc,&w.info,&w.keys)==KDE_RSN_MISMATCH;
    const unsigned char *p=(const void *)&w.keys;for (size_t i=0;i<sizeof(w.keys);++i) { ok=ok && !p[i]; }
    crypto_wipe(&w,sizeof(w));return ok;
}
