#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <math.h>

#include "initialize.h"
#include "ssd.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"

extern int secno_num_per_page, secno_num_sub_page;
/******************************************************************************************������ftl��map����******************************************************************************************/


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
	unsigned int sb_no;
	unsigned int md_cnt,invalid_cnt;
	//find victim garbage superblock
	sb_no = find_victim_superblock(ssd);
	invalid_cnt = Get_SB_Invalid(ssd, sb_no);

	//migrate and erase
	md_cnt = migration_horizon(ssd, req, sb_no);

	if (md_cnt + invalid_cnt != ssd->parameter->page_block*ssd->open_sb->blk_cnt)
	{
		printf("Look Here 7\n");
		getchar();
	}

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
	unsigned int transer = 0;
	unsigned int lpn, new_ppn = INVALID_PPN, state;
	__int64 time;
	unsigned int sum_md;
	int ref_cnt;
	struct local loc;

	for (i = 0; i <= secno_num_per_page - 1; i++)
		state = SET_VALID(state, i);

	sum_md = 0;

	block = ssd->sb_pool[victim].pos[0].block;

	for (page = 0; page < ssd->parameter->page_block; page++)
	{
		for (chan = 0; chan < ssd->parameter->channel_number; chan++)
		{
			for (chip = 0; chip < ssd->parameter->chip_channel[chan]; chip++)
			{
				// ssd->channel_head[chan].chip_head[chip].next_state_predict_time += ssd->parameter->time_characteristics.tR;
				// transer = 0;
				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; plane < ssd->parameter->plane_die; plane++)
					{
						ref_cnt = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].ref_cnt;
						if (ref_cnt > 0)
						{
							// transer++;
							sum_md++;
							lpn = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;

							//set mapping table invalid
							// ssd->dram->map->L2P_entry[lpn].pn = INVALID_PPN;
							// insert2_command_buffer(ssd, ssd->dram->data_command_buffer, lpn, state, req);

							new_ppn = get_new_page(ssd);
							if(new_ppn == INVALID_PPN)
							{
								printf("ERROR: get new page fail in GC\n");
							}

							ssd_page_read(ssd, chan, chip);

							ssd_page_write(ssd, loc.channel, loc.chip);

							find_location_ppn(ssd, new_ppn, &loc);

							invalidate_old_lpn(ssd, lpn);

							update_new_page_mapping(ssd, lpn, new_ppn);
						}
					}
				}
				//transfer req to buffer and hand the request
				// if (transer > 0)
				// {
				// 	time = ssd->channel_head[chan].chip_head[chip].next_state_predict_time + transer * ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tRC;
				// 	ssd->channel_head[chan].next_state_predict_time = (ssd->channel_head[chan].next_state_predict_time > time) ? time : ssd->channel_head[chan].next_state_predict_time;
				// }
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
int find_victim_superblock(struct ssd_info *ssd)
{
	unsigned int sb_no = 0;
	unsigned int max_sb_cnt = 0;
    
	unsigned int sb_invalid;
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

	if (max_sb_cnt < ssd->open_sb->blk_cnt * ssd->parameter->page_block * 0.1)
	{
		printf("Look Here 9\n");
	}

	ssd->sb_pool[sb_no].gcing = 1;
	return sb_no;
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

	// /*
	// judge whether GC is triggered
	// note need to reduce the influence of mixture of GC data and user data
    // */
	// if(ssd->free_sb_cnt <= MIN_SB_RATE * ssd->sb_cnt)
	// {
	// 	SuperBlock_GC(ssd, req);
	// }

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
		printf("ERROR page == ssd->parameter->page_block\n");
	}

	if (ssd->open_sb->pg_off == ssd->open_sb->blk_cnt - 1)
		ssd->open_sb->next_wr_page++;

	new_ppn = find_ppn(ssd, channel, chip, die, plane, block, page);

	return new_ppn;
}

Status update_new_page_mapping(struct ssd_info *ssd, unsigned int lpn, unsigned int ppn)
{
	unsigned int channel, chip, die, plane, block, page;
	struct local loc;

	ssd->dram->map->L2P_entry[lpn].pn = ppn;
	
	find_location_ppn(ssd, ppn, &loc);
	channel = loc.channel;
	chip = loc.chip;
	die = loc.die;
	plane = loc.plane;
	block = loc.block;
	page = loc.page;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;  //inlitialization is -1
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page != page)
	{
		printf("ERROR last_write_page != loc.page\n");
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].ref_cnt = 1;

	return SUCCESS;
}