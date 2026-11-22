#include "rp1_msix.h"
#include <assert.h>
#include <stdio.h>

static uint32_t registers[0x9310/4];
static unsigned writes,config_reads;
static bool fail_restore,fail_select;
enum { INDEX=0x9000/4, ENDPOINT=0x8000/4 };

static bool read_register(void *context,uint32_t offset,uint32_t *value)
{
    assert(context==registers && !(offset&3) && offset<sizeof(registers));
    if (offset>=0x8000 && offset<0x8100) {
        assert(registers[INDEX]==0x00100000);
        ++config_reads;
    }
    *value=registers[offset/4];
    return true;
}

static bool write_selector(void *context,uint32_t offset,uint32_t value)
{
    assert(context==registers && offset==0x9000);
    ++writes;
    if (value==0x00100000 && fail_select) { return false; }
    if (!value && fail_restore) { return false; }
    registers[INDEX]=value;
    return true;
}

int main(void)
{
    const struct xhci_observer bridge={read_register,registers,sizeof(registers)};
    registers[0x4068/4]=0xb0;
    registers[0x18/4]=0x010100;
    registers[ENDPOINT]=0x00011de4;
    registers[ENDPOINT+1]=0x00100006;
    registers[ENDPOINT+0x10/4]=0x00400000;
    registers[ENDPOINT+0x14/4]=0;
    registers[ENDPOINT+0x34/4]=0x40;
    registers[ENDPOINT+0x40/4]=0x5010;
    registers[ENDPOINT+0x50/4]=0x403c0011;
    registers[ENDPOINT+0x58/4]=0x2000;
    struct rp1_pci_header root;
    struct rp1_msix out={0};
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_OK && writes==2 && !registers[INDEX]);
    assert(out.bus==1 && out.capability==0x50 && out.entries==61 && out.control==0x403c);
    assert(out.peripheral_bus==0 && out.table_bus==0x00400000 && out.pba_bus==0x00402000);
    assert(root.link==0xb0 && root.buses==0x010100);
    registers[0x4068/4]=0x30;
    registers[0x0c/4]=0x00010000;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_OK && writes==4 && !registers[INDEX]);

    registers[ENDPOINT+0x40/4]=0x4010;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_CAPABILITY && !registers[INDEX]);
    registers[ENDPOINT+0x40/4]=0x5010;
    registers[ENDPOINT+0x58/4]=0x100;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_CAPABILITY && !registers[INDEX]);
    registers[ENDPOINT+0x58/4]=0x2001;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_CAPABILITY && !registers[INDEX]);
    registers[ENDPOINT+0x58/4]=0x4000;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_CAPABILITY && !registers[INDEX]);
    registers[ENDPOINT+0x58/4]=0x2000;
    registers[ENDPOINT]=0xffffffff;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_ENDPOINT && !registers[INDEX]);
    registers[ENDPOINT]=0x00011de4;
    unsigned reads=config_reads;
    fail_select=true;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_SELECTOR && config_reads==reads && !registers[INDEX]);
    fail_select=false;
    fail_restore=true;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_RESTORE && registers[INDEX]==0x100000);
    registers[INDEX]=0;
    fail_restore=false;
    registers[0x4068/4]=0xa0;
    unsigned prior=writes;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_LINK && writes==prior && out.entries==61);
    assert(root.link==0xa0 && root.header==0x00010000 && root.buses==0x010100);
    registers[0x4068/4]=0x30;
    registers[0x0c/4]=0;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_LINK && writes==prior);
    registers[0x0c/4]=0x00010000;
    registers[0x18/4]=0;
    assert(rp1_msix_read(&bridge,write_selector,&root,&out)==RP1_PCI_BUS && writes==prior && root.buses==0);

    registers[0x4070/4]=0x00f00000;
    registers[0x4080/4]=0x1c;
    registers[0x4084/4]=0x1c;
    uint64_t cpu=42;
    assert(rp1_pci_cpu_address(&bridge,0x30000,0x100,&cpu) && cpu==0x1c00030000ULL);
    assert(rp1_pci_cpu_address(&bridge,out.table_bus,out.entries*16,&cpu) && cpu==0x1c00400000ULL);
    assert(!rp1_pci_cpu_address(&bridge,0xfffff0,32,&cpu) && cpu==0x1c00400000ULL);
    registers[0x4074/4]=0x00400000;
    registers[0x4088/4]=0x1f;
    registers[0x408c/4]=0x1f;
    assert(!rp1_pci_cpu_address(&bridge,out.table_bus,out.entries*16,&cpu));

    assert(rp1_mip_aperture_read(&bridge)==RP1_MIP_UNMAPPED);
    registers[0x402c/4]=0xfffff01c;
    registers[0x4030/4]=0xff;
    registers[0x40ac/4]=0x00130001;
    registers[0x40b0/4]=0x10;
    assert(rp1_mip_aperture_read(&bridge)==RP1_MIP_MAPPED);
    registers[0x40d4/4]=0xffff0001;
    registers[0x40d8/4]=0xff;
    registers[0x410c/4]=1;
    assert(rp1_mip_aperture_read(&bridge)==RP1_MIP_UNAVAILABLE);
    registers[0x410c/4]=0;
    registers[0x40ac/4]&=~1u;
    assert(rp1_mip_aperture_read(&bridge)==RP1_MIP_UNMAPPED);
    puts("PASS: RP1 bridge diagnostics, PCI selector restoration, MSI-X capability and doorbell mapping.");
    return 0;
}
