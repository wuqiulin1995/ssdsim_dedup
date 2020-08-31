#pragma once

#include "initialize.h"

Status erase_operation(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block);
__int64 ssd_page_read(struct ssd_info *ssd, unsigned int channel, unsigned int chip);
__int64 ssd_page_write(struct ssd_info *ssd, unsigned int channel, unsigned int chip);
Status update_flash_ts(struct ssd_info *ssd, __int64 blocking_to_ts);
__int64 update_nvram_ts(struct ssd_info *ssd, unsigned int block, __int64 need_time);