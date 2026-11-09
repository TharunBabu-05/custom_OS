#ifndef OS_WIFI_FILES_H
#define OS_WIFI_FILES_H
#include "brcm_firmware.h"
enum block_result wifi_file_open(void *context,unsigned asset,uint32_t *bytes);
enum block_result wifi_file_read(void *context,uint32_t offset,void *data,size_t length);
void wifi_files_command(void);
void wifi_files_report(const struct brcm_fw_state *state);
#endif
