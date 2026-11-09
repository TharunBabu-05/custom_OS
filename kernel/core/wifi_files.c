#include "wifi_files.h"
#include "vfs.h"
#include "console.h"
static struct fat32_file file;
static struct brcm_fw_state checked={.error=BLOCK_NOT_READY};
enum block_result wifi_file_open(void *context,unsigned asset,uint32_t *bytes)
{
    (void)context;
    if (asset>=BRCM_FW_ASSETS || !bytes) { return BLOCK_ARGUMENT; }
    char path[32]="/boot/";unsigned at=6;
    const char *name=brcm_fw_assets[asset].name;while (*name) { path[at++]=*name++; }path[at]=0;
    enum fs_result r=vfs_boot_open(path,&file);
    if (r!=FS_OK) { console_write("Wi-Fi file: ");console_write(path);console_write(" ");console_write(ramfs_error(r));console_write("\n");return BLOCK_IO; }
    *bytes=file.entry.size;return BLOCK_OK;
}
enum block_result wifi_file_read(void *context,uint32_t offset,void *data,size_t length)
{
    (void)context;size_t count=0;enum fs_result r=fat32_file_read(&file,offset,data,length,&count);
    return r==FS_OK && count==length ? BLOCK_OK : BLOCK_IO;
}
void wifi_files_report(const struct brcm_fw_state *s)
{
    console_write("Wi-Fi firmware files: ");console_write(s->files_valid ? "SHA256 pins verified\n" : "not verified\n");
    if (s->files_valid) {
        for (unsigned a=0;a<BRCM_FW_ASSETS;++a) {
            console_write(brcm_fw_assets[a].name);console_write(" bytes=");console_u64(brcm_fw_assets[a].bytes);
            console_write(" SHA256=");console_write(brcm_fw_assets[a].sha256);console_write("\n");
        }
    } else {
        console_write("asset=");console_u64(s->asset);console_write(" result=");console_write(block_error(s->error));console_write("\n");
    }
    console_write("Vendor firmware is separate from the OurOS kernel. WIFILIC.TXT retains its notices.\n");
}
void wifi_files_command(void)
{
    if (!vfs_boot_mounted()) { console_write("Run storageinit before wififiles.\n");return; }
    struct brcm_fw_io io={.open=wifi_file_open,.file_read=wifi_file_read};
    console_write("Verifying pinned Wi-Fi files from /boot; no radio access.\n");
    (void)brcm_fw_check_files(&io,&checked);wifi_files_report(&checked);
}
