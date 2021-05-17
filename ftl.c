#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <math.h>

#include "initialize.h"
#include "interface.h"
#include "ssd.h"
#include "buffer.h"
#include "ftl.h"
#include "flash.h"

extern int secno_num_per_page, secno_num_sub_page;

Status invalidate_old_lpn(struct ssd_info* ssd, unsigned int lpn)
{
	unsigned int ppn = INVALID_PPN, fing = 0;
	struct local loc;

	ppn = ssd->dram->map->L2P_entry[lpn].pn;

	if(ppn != INVALID_PPN)
	{
		find_location_ppn(ssd, ppn, &loc);

		decrease_reverse_mapping(ssd, ppn, lpn);

		if(ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry == NULL)
		{
			fing = ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].fing;
			ssd->dram->map->F2P_entry[fing].pn = INVALID_PPN;
			
			ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].invalid_page_num++;
		}

		if(ssd->dram->map->in_nvram[lpn] == 1)
		{
			ssd->nvram_seg[loc.block].invalid_entry++;
			ssd->invalid_oob_entry++;
		}
		else if(ssd->dram->map->in_nvram[lpn] == 2)
		{
			ssd->nvram_seg[loc.block].invalid_entry_page++;
		}
	}

	return SUCCESS;
}


/*****************************************************************************
*The function is based on the parameters channel, chip, die, plane, block, page, 
*find the physical page number
******************************************************************************/
unsigned int find_ppn(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn = 0;
	unsigned int i = 0;
	int page_plane = 0, page_die = 0, page_chip = 0;
	int page_channel[100];                 

#ifdef DEBUG
	printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n", channel, chip, die, plane, block, page);
#endif

	/***************************************************************
	*Calculate the number of pages in plane, die, chip, and channel
	****************************************************************/
	page_plane = ssd->parameter->page_block*ssd->parameter->block_plane;
	page_die = page_plane*ssd->parameter->plane_die;
	page_chip = page_die*ssd->parameter->die_chip;
	while (i<ssd->parameter->channel_number)
	{
		page_channel[i] = ssd->parameter->chip_channel[i] * page_chip;
		i++;
	}

	/****************************************************************************
	*Calculate the physical page number ppn, ppn is the sum of the number of pages 
	*in channel, chip, die, plane, block, page
	*****************************************************************************/
	i = 0;
	while (i<channel)
	{
		ppn = ppn + page_channel[i];
		i++;
	}
	ppn = ppn + page_chip*chip + page_die*die + page_plane*plane + block*ssd->parameter->page_block + page;

	return ppn;
}


/************************************************************************************
*function is based on the physical page number ppn find the physical page where the 
*channel, chip, die, plane, block,In the structure location and as a return value
*************************************************************************************/
Status find_location_ppn(struct ssd_info* ssd, unsigned int ppn, struct local *location)
{
	int page_block, page_plane, page_die, page_chip, page_channel;

	page_block = ssd->parameter->page_block;
	page_plane = page_block * ssd->parameter->block_plane;
	page_die   = page_plane * ssd->parameter->plane_die;
	page_chip = page_die * ssd->parameter->die_chip;
	page_channel = page_chip * ssd->parameter->chip_channel[0];  // work under the condition where all channels are armed with the same number of chips

#ifdef DEBUG
	printf("enter find_location\n");
#endif

	location->channel = ppn / page_channel;
	location->chip = (ppn % page_channel) / page_chip;
	location->die = ((ppn % page_channel) % page_chip) / page_die;
	location->plane = (((ppn % page_channel) % page_chip) % page_die) / page_plane;
	location->block = ((((ppn % page_channel) % page_chip) % page_die) % page_plane) / page_block;
	location->page = ((((ppn % page_channel) % page_chip) % page_die) % page_plane) % page_block;
	
	return SUCCESS;
}

//each time write operation is carried out, judge whether superblock-garbage is carried out
Status SuperBlock_GC(struct ssd_info *ssd, struct request *req)
{
	int sb_no = -1;
	unsigned int md_cnt,invalid_cnt;
	//find victim garbage superblock
	invalid_cnt = find_victim_superblock(ssd, &sb_no);
	// invalid_cnt = Get_SB_Invalid(ssd, sb_no);

	if(sb_no == -1)
	{
		printf("ERROR: no victim block\n");
		getchar();
	}

	//migrate and erase
	md_cnt = migration_horizon(ssd, req, sb_no);

	if (md_cnt + invalid_cnt != ssd->parameter->page_block*ssd->open_sb->blk_cnt)
	{
		printf("Look Here 7\n");
		getchar();
	}

	ssd->gc_program_cnt += md_cnt;
	ssd->gc_count++;
	ssd->total_gc_count++;
	 
	ssd->free_sb_cnt++;
	return SUCCESS;
}

//migrate data 
int migration_horizon(struct ssd_info* ssd, struct request* req, unsigned int victim)
{
	int i, j;
	unsigned int chan, chip, die, plane, block, page;
	unsigned int lpn, old_ppn = INVALID_PPN, new_ppn = INVALID_PPN, fing = 0;
	__int64 time;
	unsigned int sum_md = 0, page_sb = 0;
	struct local loc;
	int oob_write = 0;
	int entry_nb = 0;
	__int64 nvram_read_time = 0, flash_oob_read_time = 0, blocking_time = 0, blocking_time_nvram = 0, blocking_time_flash_oob = 0;
	struct LPN_ENTRY *lpn_entry = NULL, *tmp_entry = NULL;

	block = ssd->sb_pool[victim].pos[0].block;

	page_sb = ssd->parameter->chip_num * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->page_block;
	entry_nb = ssd->nvram_seg[victim].alloc_seg * OOB_ENTRY_PER_SEG - ssd->nvram_seg[victim].free_entry;
	if(Get_SB_Invalid(ssd, victim) < page_sb && entry_nb > 0)
	{
		nvram_read_time = (__int64)entry_nb * OOB_ENTRY_BYTES / 64 * NVRAM_READ_DELAY;
		flash_oob_read_time = (__int64)((ssd->nvram_seg[victim].alloc_page + CHIP_PARA - 1) / CHIP_PARA) * ssd->parameter->time_characteristics.tR + (__int64)(ssd->nvram_seg[victim].total_entry_page) * OOB_ENTRY_BYTES / 64 * 50;
		
		blocking_time_nvram = update_nvram_ts(ssd, block, nvram_read_time);
		blocking_time_flash_oob = update_flash_oob_ts(ssd, flash_oob_read_time);
	}

	if(nvram_read_time > 0)
	{
		for(i = 0; i < ssd->parameter->channel_number; i++)
		{
			for(j = 0; j < ssd->channel_head[i].chip; j++)
			{
				ssd->channel_head[i].chip_head[j].next_state_predict_time += blocking_time_flash_oob > blocking_time_nvram ? flash_oob_read_time : nvram_read_time;
			}
		}

		ssd->gcr_nvram_print++;
		ssd->gcr_nvram_delay_print += nvram_read_time;
		ssd->avg_gcr_nvram_delay = ssd->gcr_nvram_delay_print / ssd->gcr_nvram_print;
	}
	
	update_nvram_oob(ssd, victim, 0);
	ssd->nvram_seg[victim].alloc_page = 0;
	ssd->nvram_seg[victim].total_entry_page = 0;
	ssd->nvram_seg[victim].invalid_entry_page = 0;

	for (page = 0; page < ssd->parameter->page_block; page++)
	{
		for (chan = 0; chan < ssd->parameter->channel_number; chan++)
		{
			for (chip = 0; chip < ssd->parameter->chip_channel[chan]; chip++)
			{
				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; plane < ssd->parameter->plane_die; plane++)
					{
						lpn_entry = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn_entry;
						tmp_entry = lpn_entry;
						if(lpn_entry != NULL)
						{
							sum_md++;
							oob_write = 0;

							fing = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].fing;

							old_ppn = find_ppn(ssd, chan, chip, die, plane, block, page);
							new_ppn = get_new_page(ssd);

							if(fing < 1 || fing > UNIQUE_PAGE_NB)
							{
								printf("ERROR: fing ERROR in GC\n");
								getchar();
							}

							if(new_ppn == INVALID_PPN)
							{
								printf("ERROR: get new page fail in GC\n");
								getchar();
							}

							find_location_ppn(ssd, new_ppn, &loc);

							ssd_page_read(ssd, chan, chip);
							ssd_page_write(ssd, loc.channel, loc.chip);
							
							ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].fing = fing;
							ssd->dram->map->F2P_entry[fing].pn = new_ppn;

							while(tmp_entry != NULL)
							{
								lpn = tmp_entry->lpn;

								if(ssd->dram->map->L2P_entry[lpn].pn != old_ppn)
								{
									printf("ERROR: ssd->dram->map->L2P_entry[lpn].pn != old_ppn\n");
									getchar();
								}

								// decrease_reverse_mapping(ssd, old_ppn, lpn);
								// update_new_page_mapping(ssd, lpn, new_ppn);

								ssd->dram->map->L2P_entry[lpn].pn = new_ppn;

								if(oob_write == 0)
								{
									ssd->dram->map->in_nvram[lpn] = 0;
									oob_write = 1;
								}
								else
								{
									update_nvram_oob(ssd, loc.block, 1);
									ssd->dram->map->in_nvram[lpn] = 1;

									update_nvram_ts(ssd, loc.block, NVRAM_WRITE_DELAY / 4);
									
									if(ssd->total_alloc_seg >= MAX_OOB_SEG)
									{
										move_entry_to_flash(ssd, block);
									}
								}

								tmp_entry = tmp_entry->next;
							}
						
							ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry = lpn_entry;
							ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn_entry = NULL;
							lpn_entry = NULL;
						}					
					}
				}
			}
		}
	}

	for (i = 0; i < ssd->sb_pool[victim].blk_cnt; i++)
	{
		chan = ssd->sb_pool[victim].pos[i].channel;
		chip = ssd->sb_pool[victim].pos[i].chip;
		die = ssd->sb_pool[victim].pos[i].die;
		plane = ssd->sb_pool[victim].pos[i].plane;
		block = ssd->sb_pool[victim].pos[i].block;
		ssd->channel_head[chan].current_state = CHANNEL_GC;
		ssd->channel_head[chan].next_state = CHANNEL_IDLE;
		erase_operation(ssd, chan, chip, die, plane, block);
	}

	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		for (j = 0; j < ssd->parameter->chip_channel[i]; j++)
		{
			ssd->channel_head[i].chip_head[j].next_state_predict_time += ssd->parameter->time_characteristics.tBERS;
		}
	}

	ssd->sb_pool[victim].ec++;
	ssd->sb_pool[victim].next_wr_page = 0;
	ssd->sb_pool[victim].pg_off = -1;
	ssd->sb_pool[victim].gcing = 0;

	return sum_md;
}

//return the garbage superblock with the maximal invalid data 
int find_victim_superblock(struct ssd_info *ssd, int *victim)
{
	int sb_no = -1;
	int max_sb_cnt = 0;
    int sb_invalid;
	int i;

	for (i = 0; i < ssd->sb_cnt; i++)
	{
		if (Is_Garbage_SBlk(ssd,i)==FAILURE)
			continue;

		sb_invalid = Get_SB_Invalid(ssd, i);
		if (sb_invalid > max_sb_cnt)
		{
			max_sb_cnt = sb_invalid;
			sb_no = i;
		}
	}

	// if (max_sb_cnt < ssd->open_sb->blk_cnt * ssd->parameter->page_block * 0.1)
	// {
	// 	printf("Look Here 9\n");
	// }

	ssd->sb_pool[sb_no].gcing = 1;
	*victim = sb_no;
	return max_sb_cnt;
}

//judge whether block is garbage block 
Status Is_Garbage_SBlk(struct ssd_info *ssd, int sb_no)
{
	unsigned int channel, chip, die, plane, block;
	int i;

	if (ssd->sb_pool[sb_no].gcing == 1)
		return FAILURE;
	for (i = 0; i < ssd->sb_pool[sb_no].blk_cnt; i++)
	{
		channel = ssd->sb_pool[sb_no].pos[i].channel;
		chip = ssd->sb_pool[sb_no].pos[i].chip;
		die = ssd->sb_pool[sb_no].pos[i].die;
		plane = ssd->sb_pool[sb_no].pos[i].plane;
		block = ssd->sb_pool[sb_no].pos[i].block;
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page != ssd->parameter->page_block-1)
		{
			return FAILURE;
		}
	}
	return SUCCESS;
}

//return superblock invalid data count 
int Get_SB_Invalid(struct ssd_info *ssd, unsigned int sb_no)
{
	unsigned int i, chan, chip, die, plane, block;
	unsigned int sum_invalid = 0;

	for (i = 0; i < ssd->sb_pool[sb_no].blk_cnt; i++)
	{
		chan = ssd->sb_pool[sb_no].pos[i].channel;
		chip = ssd->sb_pool[sb_no].pos[i].chip;
		die = ssd->sb_pool[sb_no].pos[i].die;
		plane = ssd->sb_pool[sb_no].pos[i].plane;
		block = ssd->sb_pool[sb_no].pos[i].block;

		if (ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num > ssd->parameter->page_block)
		{
			printf("Look Here 4\n");
			getchar();
		}

		sum_invalid += ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num;
	}
	
	return sum_invalid;
}

/* 
    find superblock block with the minimal P/E cycles
*/
Status find_active_superblock(struct ssd_info *ssd, struct request *req)
{
	int i;
	int min_ec = 9999999;
	int min_sb = -1;

	for (i = 0; i < ssd->sb_cnt; i++)
	{
		if (ssd->sb_pool[i].next_wr_page == 0 && ssd->sb_pool[i].pg_off == -1) //free superblock
		{
			if (ssd->sb_pool[i].ec < min_ec)
			{
				min_sb = i;
				min_ec = ssd->sb_pool[i].ec;
			}
		}
	}

	if (min_sb == -1)
	{
		for (i = 0; i < ssd->sb_cnt; i++)
			printf("%d  %d\n", ssd->sb_pool[i].next_wr_page, ssd->sb_pool[i].pg_off); //free superblock
		printf("No Free Blocks\n");
	}

	//set open superblock 
	ssd->open_sb = ssd->sb_pool + min_sb;
	ssd->free_sb_cnt--;

	return SUCCESS;
}

int get_free_sb_count(struct ssd_info* ssd)
{
	int cnt = 0;
	int i;
	for (i = 0; i < ssd->sb_cnt; i++)
	{
		if (ssd->sb_pool[i].next_wr_page == 0 && ssd->sb_pool[i].pg_off == -1) //free superblock
			cnt++;
	}
	return cnt;
}

unsigned int get_new_page(struct ssd_info *ssd)
{
	unsigned int channel = 0, chip = 0, die = 0, plane = 0, block = 0, page = 0;
	unsigned int new_ppn = INVALID_PPN;

	if (ssd->open_sb == NULL)
		find_active_superblock(ssd, NULL);

	if (ssd->open_sb->next_wr_page == ssd->parameter->page_block) //no free superpage in the superblock
		find_active_superblock(ssd, NULL);

	ssd->open_sb->pg_off = (ssd->open_sb->pg_off + 1) % ssd->open_sb->blk_cnt;

	channel = ssd->open_sb->pos[ssd->open_sb->pg_off].channel;
	chip = ssd->open_sb->pos[ssd->open_sb->pg_off].chip;
	die = ssd->open_sb->pos[ssd->open_sb->pg_off].die;
	plane = ssd->open_sb->pos[ssd->open_sb->pg_off].plane;
	block = ssd->open_sb->pos[ssd->open_sb->pg_off].block;
	page = ssd->open_sb->next_wr_page;

	if (page == ssd->parameter->page_block)
	{
		printf("ERROR: page == ssd->parameter->page_block\n");
		getchar();
	}

	if (ssd->open_sb->pg_off == ssd->open_sb->blk_cnt - 1)
		ssd->open_sb->next_wr_page++;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;  //inlitialization is -1

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page != page)
	{
		printf("ERROR: last_write_page != page\n");
		getchar();
	}

	new_ppn = find_ppn(ssd, channel, chip, die, plane, block, page);

	return new_ppn;
}

Status update_new_page_mapping(struct ssd_info *ssd, unsigned int lpn, unsigned int ppn)
{
	increase_reverse_mapping(ssd, ppn, lpn);
	ssd->dram->map->L2P_entry[lpn].pn = ppn;

	return SUCCESS;
}

Status increase_reverse_mapping(struct ssd_info *ssd, unsigned int ppn, unsigned int lpn)
{
	struct local loc;
	struct LPN_ENTRY *lpn_entry = NULL;

	lpn_entry = (struct LPN_ENTRY *)malloc(sizeof(struct LPN_ENTRY));
	alloc_assert(lpn_entry, "lpn_entry");
	
	lpn_entry->lpn = lpn;
	lpn_entry->next = NULL;

	find_location_ppn(ssd, ppn, &loc);

	lpn_entry->next = ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry;
	ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry = lpn_entry;

	return SUCCESS;
}

Status decrease_reverse_mapping(struct ssd_info *ssd, unsigned int ppn, unsigned int lpn)
{
	struct local loc;
	struct LPN_ENTRY *del_entry = NULL, *pre_entry = NULL;
	int ret = FAILURE;

	find_location_ppn(ssd, ppn, &loc);

	del_entry = ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry;

	if(del_entry == NULL)
	{
		printf("ERROR: no reverse mapping\n");
		getchar();
	}

	while(del_entry != NULL)
	{
		if(del_entry->lpn == lpn)
		{
			if(pre_entry == NULL)
			{
				if(del_entry->next == NULL)
				{
					ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry = NULL;
				}
				else
				{
					ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].lpn_entry = del_entry->next;
				}		
			}
			else
			{
				if(del_entry->next == NULL)
				{
					pre_entry->next = NULL;	
				}
				else
				{
					pre_entry->next = del_entry->next;
				}
			}

			free(del_entry);
			del_entry = NULL;
			
			ret = SUCCESS;
			break;
		}

		pre_entry = del_entry;
		del_entry = pre_entry->next;
	}

	if(ret == FAILURE)
	{
		printf("ERROR: no reverse mapping\n");
		getchar();
	}

	return ret;
}

Status update_nvram_oob(struct ssd_info *ssd, unsigned int block, int type)
{
	int entry_nb = 0;

	if(type == 1)
	{
		if(ssd->nvram_seg[block].free_entry == 0)
		{
			ssd->nvram_seg[block].alloc_seg++;
			ssd->nvram_seg[block].free_entry = OOB_ENTRY_PER_SEG;

			if(ssd->nvram_seg[block].alloc_seg > ssd->max_alloc_seg)
				ssd->max_alloc_seg = ssd->nvram_seg[block].alloc_seg;

			ssd->total_alloc_seg++;
		}

		ssd->nvram_seg[block].free_entry--;
		ssd->total_oob_entry++;

		if(ssd->total_alloc_seg > MAX_OOB_SEG)
			printf("ERROR: ssd->total_alloc_seg > MAX_OOB_SEG\n");
	}
	else if(type == 0)
	{
		entry_nb = ssd->nvram_seg[block].alloc_seg * OOB_ENTRY_PER_SEG - ssd->nvram_seg[block].free_entry;

		if(ssd->nvram_seg[block].alloc_seg < ssd->min_alloc_seg)
			ssd->min_alloc_seg = ssd->nvram_seg[block].alloc_seg;

		ssd->total_alloc_seg -= ssd->nvram_seg[block].alloc_seg;
		ssd->total_oob_entry -= entry_nb;
		ssd->invalid_oob_entry -= ssd->nvram_seg[block].invalid_entry;
		
		ssd->nvram_seg[block].alloc_seg = 0;
		ssd->nvram_seg[block].free_entry = 0;
		ssd->nvram_seg[block].invalid_entry = 0;
	}
	else
	{
		printf("ERROR: invalid update nvram oob type\n");
	}
	
	return SUCCESS;
}

Status nvram_oob_gc(struct ssd_info *ssd)
{
	int max_invalid_entry = 0, entry_nb = 0, valid_entry = 0, victim_seg = -1, i = 0;
	__int64 nvram_oob_rw_time = 0;

	for(i = 0; i < ssd->sb_cnt; i++)
	{
		if(ssd->nvram_seg[i].invalid_entry > max_invalid_entry)
		{
			max_invalid_entry = ssd->nvram_seg[i].invalid_entry;
			victim_seg = i;
		}
	}

	if(victim_seg == -1)
	{
		printf("ERROR: no victim segment to gc\n");
		getchar();
	}

	entry_nb = ssd->nvram_seg[victim_seg].alloc_seg * OOB_ENTRY_PER_SEG - ssd->nvram_seg[victim_seg].free_entry;
	valid_entry = entry_nb - ssd->nvram_seg[victim_seg].invalid_entry;

	update_nvram_oob(ssd, victim_seg, 0);

	for(i = 0; i < valid_entry; i++)
	{
		if(ssd->nvram_seg[victim_seg].free_entry == 0)
		{
			ssd->nvram_seg[victim_seg].alloc_seg++;
			ssd->nvram_seg[victim_seg].free_entry = OOB_ENTRY_PER_SEG;

			if(ssd->nvram_seg[victim_seg].alloc_seg > ssd->max_alloc_seg)
				ssd->max_alloc_seg = ssd->nvram_seg[victim_seg].alloc_seg;

			ssd->total_alloc_seg++;
		}

		ssd->nvram_seg[victim_seg].free_entry--;
		ssd->total_oob_entry++;
	}

	nvram_oob_rw_time = (__int64)entry_nb * OOB_ENTRY_BYTES / 64 * NVRAM_READ_DELAY + (__int64)valid_entry * OOB_ENTRY_BYTES / 64 * NVRAM_WRITE_DELAY;

	update_nvram_ts(ssd, victim_seg, nvram_oob_rw_time);

	ssd->nvram_gc_print++;
	ssd->nvram_gc_delay_print += nvram_oob_rw_time;
	ssd->avg_nvram_gc_delay = ssd->nvram_gc_delay_print / ssd->nvram_gc_print;

	return SUCCESS;
}

Status flash_oob_gc(struct ssd_info *ssd)
{
	int entry_nb = 0, used_page = 0, alloc_page = 0, i = 0;
	int need_oob_page = 0;
	__int64 FLASH_OOB_rw_time;

	ssd->flash_oob->alloc_page = 0;

	for(i = 0; i < ssd->sb_cnt; i++)
	{
		entry_nb += ssd->nvram_seg[i].total_entry_page;
		used_page += ssd->nvram_seg[i].alloc_page;
		need_oob_page = (ssd->nvram_seg[i].total_entry_page - ssd->nvram_seg[i].invalid_entry_page + OOB_ENTRY_PER_PAGE - 1) / OOB_ENTRY_PER_PAGE;
		alloc_page += need_oob_page;

		ssd->flash_oob->alloc_page += need_oob_page;

		ssd->nvram_seg[i].alloc_page = need_oob_page;
		ssd->nvram_seg[i].total_entry_page = ssd->nvram_seg[i].total_entry_page - ssd->nvram_seg[i].invalid_entry_page;
		ssd->nvram_seg[i].invalid_entry_page = 0;
	}

	FLASH_OOB_rw_time = (__int64)used_page * ssd->parameter->time_characteristics.tR / CHIP_PARA + (__int64)entry_nb * OOB_ENTRY_BYTES / 64 * 50 + (__int64)alloc_page * ssd->parameter->time_characteristics.tPROG / CHIP_PARA + MAX_OOB_SB * ssd->parameter->time_characteristics.tBERS;

	update_flash_oob_ts(ssd, FLASH_OOB_rw_time);

	return SUCCESS;
}

Status move_entry_to_flash(struct ssd_info *ssd, int ex_block)
{
	int max_alloc_seg = 0, victim_oob_nb = -1, entry_nb = 0, valid_entry_nb = 0, last_entry_nb = 0, need_oob_page = 0;
	unsigned int ppn = -1, lpn = -1;
	unsigned int i = 0, j = 0, chan, chip, die, plane, block, page;
	__int64 NVRAM_OOB_rw_time = 0, FLASH_OOB_rw_time = 0;
	struct LPN_ENTRY *lpn_entry = NULL, *tmp_entry = NULL;

	for(i = 0; i < ssd->sb_cnt; i++)
	{
		if(ssd->nvram_seg[i].alloc_seg > max_alloc_seg && i != ex_block)
		{
			max_alloc_seg = ssd->nvram_seg[i].alloc_seg;
			victim_oob_nb = i;
		}
	}

	entry_nb = ssd->nvram_seg[victim_oob_nb].alloc_seg * OOB_ENTRY_PER_SEG - ssd->nvram_seg[victim_oob_nb].free_entry;
	valid_entry_nb = entry_nb - ssd->nvram_seg[victim_oob_nb].invalid_entry;

	need_oob_page = valid_entry_nb / OOB_ENTRY_PER_PAGE;

	if(need_oob_page == 0)
	{
		need_oob_page = 1;
		last_entry_nb = 0;
		update_nvram_oob(ssd, victim_oob_nb, 0);
	}
	else
	{
		last_entry_nb = valid_entry_nb - need_oob_page * OOB_ENTRY_PER_PAGE;
		update_nvram_oob(ssd, victim_oob_nb, 0);
		for(i = 0; i < last_entry_nb; i++)
			update_nvram_oob(ssd, victim_oob_nb, 1);
	}
	ssd->nvram_seg[victim_oob_nb].alloc_page += need_oob_page;
	ssd->nvram_seg[victim_oob_nb].total_entry_page += valid_entry_nb - last_entry_nb;

	ssd->flash_oob->alloc_page += need_oob_page;

	NVRAM_OOB_rw_time = (__int64)entry_nb * OOB_ENTRY_BYTES * NVRAM_READ_DELAY / 64 + (__int64)last_entry_nb * OOB_ENTRY_BYTES * NVRAM_WRITE_DELAY / 64;
	FLASH_OOB_rw_time = (__int64)((need_oob_page + CHIP_PARA - 1) / CHIP_PARA) * ssd->parameter->time_characteristics.tPROG;

	update_nvram_ts(ssd, victim_oob_nb, NVRAM_OOB_rw_time);
	update_flash_oob_ts(ssd, FLASH_OOB_rw_time);

	j = valid_entry_nb - last_entry_nb;
	block = ssd->sb_pool[victim_oob_nb].pos[0].block;
	for (page = 0; page < ssd->parameter->page_block; page++)
	{
		for (chan = 0; j > 0 && chan < ssd->parameter->channel_number; chan++)
		{
			for (chip = 0; j > 0 && chip < ssd->parameter->chip_channel[chan]; chip++)
			{
				for (die = 0; j > 0 && die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; j > 0 && plane < ssd->parameter->plane_die; plane++)
					{
						lpn_entry = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn_entry;
						while(j > 0 && lpn_entry != NULL)
						{
							lpn = lpn_entry->lpn;
							if(ssd->dram->map->in_nvram[lpn] == 1)
							{
								ssd->dram->map->in_nvram[lpn] = 2;
								j--;
							}
							lpn_entry = lpn_entry->next;
						}
					}
				}
			}
		}
	}

	if(ssd->flash_oob->alloc_page >= MAX_FLASH_OOB_PAGE)
	{
		flash_oob_gc(ssd);
	}

	return SUCCESS;
}

Status use_remap(struct ssd_info *ssd, unsigned int block)
{
	int count = 0;
	float invalid_ratio = 0.0;

	do{
		if(ssd->nvram_seg[block].free_entry > 0)
			return SUCCESS;

		if(ssd->total_alloc_seg < MAX_OOB_SEG)
			return SUCCESS;

		if(ssd->total_oob_entry > 0)
			invalid_ratio = (float)(ssd->invalid_oob_entry) / ssd->total_oob_entry;

		if(ssd->total_alloc_seg >= MAX_OOB_SEG && invalid_ratio > INVALID_ENTRY_THRE)
		{
			nvram_oob_gc(ssd);
			count++;
		}

		if(ssd->total_alloc_seg >= MAX_OOB_SEG && invalid_ratio <= INVALID_ENTRY_THRE)
		{
			move_entry_to_flash(ssd, -1);
		}

	}while(count <= ssd->parameter->block_plane);

	ssd->use_remap_fail++;
	return FAILURE;
}