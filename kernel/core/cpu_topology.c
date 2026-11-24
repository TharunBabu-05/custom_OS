#include "arm_platform.h"
#include "dt_resources.h"

struct discovery {
    struct arm_topology cpus;
    unsigned cells;
    bool valid;
};

static bool literal(const struct dt_node *node,const char *key,const char *expected)
{
    const unsigned char *p;
    uint32_t size;
    if (dt_node_property(node,key,&p,&size)!=DT_FOUND) { return false; }
    for (uint32_t i=0;i<size;++i) {
        if (p[i]!=(unsigned char)expected[i]) { return false; }
        if (!p[i]) { return i+1==size; }
    }
    return false;
}

static void collect_cpu(const char *path,const struct dt_node *node,void *context)
{
    struct discovery *state=context;
    if (!state->valid || !dt_direct_child(path,"/cpus")) { return; }
    bool cpu=literal(node,"device_type","cpu");
    const unsigned char *reg;
    uint32_t bytes;
    if (!cpu && !(node->name[0]=='c' && node->name[1]=='p' &&
                  node->name[2]=='u' && node->name[3]=='@')) { return; }
    enum dt_state enabled=dt_local_state(node);
    if (!cpu || enabled==DT_BAD_STATUS || state->cpus.count==ARM_CPU_LIMIT ||
        dt_node_property(node,"reg",&reg,&bytes)!=DT_FOUND || bytes!=4*state->cells) {
        state->valid=false;
        return;
    }
    uint64_t affinity=dt_cell(reg);
    if (state->cells==2) { affinity=(affinity<<32)|dt_cell(reg+4); }
    if (affinity&~ARM_AFFINITY_MASK) { state->valid=false; return; }
    for (unsigned i=0;i<state->cpus.count;++i) {
        if (state->cpus.cpu[i].affinity==affinity) { state->valid=false; return; }
    }
    state->cpus.cpu[state->cpus.count++]=(struct arm_cpu){
        affinity,enabled==DT_ENABLED,literal(node,"enable-method","psci")
    };
}

bool arm_topology_read(const void *dtb,size_t bytes,struct arm_topology *out)
{
    const unsigned char *p;
    uint32_t count;
    if (dt_device_state(dtb,bytes,"/cpus")!=DT_ENABLED ||
        !dt_property(dtb,bytes,"/cpus","#size-cells",&p,&count) || count!=4 || dt_cell(p) ||
        !dt_property(dtb,bytes,"/cpus","#address-cells",&p,&count) || count!=4) { return false; }
    struct discovery state={.cells=dt_cell(p),.valid=true};
    if ((state.cells!=1 && state.cells!=2) ||
        !dt_walk(dtb,bytes,collect_cpu,&state) || !state.valid || !state.cpus.count) { return false; }
    *out=state.cpus;
    return true;
}
