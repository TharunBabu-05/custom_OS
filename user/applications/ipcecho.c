#include "user.h"

enum { IO_WAIT_MS=3000 };
struct client {
    uint64_t channel;
    int64_t deadline;
    struct uos_message message;
    bool replying;
};

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

static void close_client(struct client *client)
{
    (void)uos_port_close(client->channel);
    *client=(struct client){0};
}

static void exchange(struct client *client,unsigned ready,int64_t now)
{
    if (ready&UOS_EVENT_HANGUP) { close_client(client);return; }
    if (!ready) { return; }
    struct uos_message *message=&client->message;
    int64_t status=client->replying ? uos_channel_send(client->channel,message,0) :
        uos_channel_recv(client->channel,message,0);
    if (status==UOS_EAGAIN) { return; }
    if (status) { close_client(client);return; }
    if (!client->replying) {
        if (message->handle) { (void)uos_close(message->handle); }
        message->peer=message->handle=message->rights=0;
    }
    client->replying=!client->replying;
    client->deadline=now+IO_WAIT_MS;
}

static int serve(const char *name)
{
    int64_t port=uos_port_listen(name);
    if (port<0) { uos_puts("ipcecho: listen failed\n");return 1; }
    uint64_t listener=(uint64_t)port;
    struct client clients[UOS_CHANNEL_MAX]={0};
    for (;;) {
        struct uos_event events[UOS_EVENT_MAX];
        unsigned slots[UOS_CHANNEL_MAX];
        unsigned count=0,free_slot=UOS_CHANNEL_MAX,wait=UOS_WAIT_MAX_MS;
        int64_t now=uos_time_ms();
        for (unsigned i=0;i<UOS_CHANNEL_MAX;++i) {
            struct client *client=&clients[i];
            if (client->channel && now>=client->deadline) { close_client(client); }
            if (!client->channel) { free_slot=i;continue; }
            unsigned remaining=(unsigned)(client->deadline-now);
            if (remaining<wait) { wait=remaining; }
            slots[count]=i;
            events[count++]=(struct uos_event){.object=client->channel,.source=UOS_EVENT_CHANNEL,
                .events=(uint16_t)(client->replying ? UOS_EVENT_WRITE : UOS_EVENT_READ)};
        }
        unsigned client_count=count;
        if (free_slot<UOS_CHANNEL_MAX) {
            events[count++]=(struct uos_event){.object=listener,.source=UOS_EVENT_LISTENER,.events=UOS_EVENT_READ};
        }
        int64_t ready=uos_event_wait(events,count,wait);
        if (ready==UOS_EINTR) { (void)uos_signal_wait(0);continue; }
        if (ready<0) { break; }
        if (!ready) { continue; }
        now=uos_time_ms();
        for (unsigned i=0;i<client_count;++i) { exchange(&clients[slots[i]],events[i].revents,now); }
        if (client_count==count) { continue; }
        unsigned listener_ready=events[client_count].revents;
        if (listener_ready&UOS_EVENT_HANGUP) { break; }
        if (listener_ready&UOS_EVENT_READ) {
            int64_t channel=uos_port_accept(listener,0);
            if (channel==UOS_EAGAIN) { continue; }
            if (channel<0) { break; }
            clients[free_slot]=(struct client){.channel=(uint64_t)channel,.deadline=now+IO_WAIT_MS};
        }
    }
    (void)uos_port_close(listener);
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
