#pragma once

#include "initialize.h"

Status find_active_superblock(struct ssd_info* ssd, struct request* req);
Status SuperBlock_GC(struct ssd_info* ssd, struct request* req);
int find_victim_superblock(struct ssd_info *ssd, int *victim);
int Get_SB_Invalid(struct ssd_info *ssd, unsigned int sb_no);
int get_free_sb_count(struct ssd_info* ssd);
Status Is_Garbage_SBlk(struct ssd_info *ssd, int sb_no);
int migration_horizon(struct ssd_info* ssd, struct request* req, unsigned int victim);
unsigned int find_ppn(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page);
Status find_location_ppn(struct ssd_info* ssd, unsigned int ppn, struct local *location);

unsigned int get_new_page(struct ssd_info *ssd);
Status invalidate_old_lpn(struct ssd_info* ssd,unsigned int lpn);
Status update_new_page_mapping(struct ssd_info *ssd, unsigned int lpn, unsigned int ppn);

Status increase_reverse_mapping(struct ssd_info *ssd, unsigned int ppn, unsigned int lpn);
Status decrease_reverse_mapping(struct ssd_info *ssd, unsigned int ppn, unsigned int lpn);

Status update_nvram_oob(struct ssd_info *ssd, unsigned int block, int type);
Status nvram_oob_gc(struct ssd_info *ssd);
Status flash_oob_gc(struct ssd_info *ssd);
Status move_entry_to_flash(struct ssd_info *ssd, int ex_block);
Status use_remap(struct ssd_info *ssd, unsigned int block);