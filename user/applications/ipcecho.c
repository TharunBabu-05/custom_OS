#include "user.h"

enum { IO_WAIT_MS=3000 };

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

static int serve(const char *name)
{
    int64_t listener=uos_port_listen(name);
    if (listener<0) { uos_puts("ipcecho: listen failed\n");return 1; }
    for (;;) {
        int64_t link=uos_port_accept((uint64_t)listener,UOS_WAIT_MAX_MS);
        if (link==UOS_ETIMEDOUT || link==UOS_EINTR) { continue; }
        if (link<0) { break; }
        struct uos_message message;
        while (!uos_channel_recv((uint64_t)link,&message,IO_WAIT_MS)) {
            if (message.handle) { (void)uos_close(message.handle); }
            message.peer=message.handle=message.rights=0;
            if (uos_channel_send((uint64_t)link,&message,IO_WAIT_MS)) { break; }
        }
        (void)uos_port_close((uint64_t)link);
    }
    (void)uos_port_close((uint64_t)listener);
    return 1;
}

static int call(const char *name,const char *text,unsigned length)
{
    int64_t link=uos_port_connect(name,0);
    if (link<0) { uos_puts("ipcecho: connection failed\n");return 1; }
    struct uos_message message={.bytes=length},reply;
    for (unsigned i=0;i<length;++i) { message.data[i]=(unsigned char)text[i]; }
    bool matches=!uos_channel_send((uint64_t)link,&message,IO_WAIT_MS) &&
        !uos_channel_recv((uint64_t)link,&reply,IO_WAIT_MS) && reply.bytes==length && !reply.handle;
    for (unsigned i=0;matches && i<length;++i) { matches=message.data[i]==reply.data[i]; }
    (void)uos_port_close((uint64_t)link);
    if (!matches) { uos_puts("ipcecho: reply failed or differed\n");return 1; }
    uos_puts("IPC echo matched: ");uos_puts(text);uos_puts("\n");
    return 0;
}

int app_main(void)
{
    char action[8],name[UOS_PORT_NAME_MAX+1],text[UOS_IPC_BYTES+1];
    int64_t argc=uos_argc();
    if (argc>=3 && uos_arg(1,action,sizeof(action))>0 && uos_arg(2,name,sizeof(name))>0) {
        if (argc==3 && same(action,"serve")) { return serve(name); }
        if (argc==4 && same(action,"call")) {
            int64_t length=uos_arg(3,text,sizeof(text));
            if (length>0) { return call(name,text,(unsigned)length); }
        }
    }
    uos_puts("Usage: ipcecho serve NAME | ipcecho call NAME MESSAGE\n");
    return 2;
}
