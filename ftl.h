#pragma once

#include "initialize.h"

unsigned int find_ppn(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page);
Status find_active_superblock(struct ssd_info* ssd, struct request* req);
Status SuperBlock_GC(struct ssd_info* ssd, struct request* req);
int find_victim_superblock(struct ssd_info *ssd);
int Get_SB_Invalid(struct ssd_info *ssd, unsigned int sb_no);
Status Is_Garbage_SBlk(struct ssd_info *ssd, int sb_no);
int migration_horizon(struct ssd_info* ssd, struct request* req, unsigned int victim);
Status find_location_ppn(struct ssd_info* ssd, unsigned int ppn, struct local *location);
int get_free_sb_count(struct ssd_info* ssd);

unsigned int get_new_page(struct ssd_info *ssd);
Status update_new_page_mapping(struct ssd_info *ssd, unsigned int lpn, unsigned int ppn);