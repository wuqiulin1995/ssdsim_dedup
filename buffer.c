#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "initialize.h"
#include "interface.h"
#include "ssd.h"
#include "buffer.h"
#include "ftl.h"
#include "flash.h"
#include "assert.h"

extern int secno_num_per_page, secno_num_sub_page;

struct ssd_info *handle_new_request(struct ssd_info *ssd)
{
	struct request *new_request;

	ssd->dram->current_time = ssd->current_time;
	new_request = ssd->request_work; 

	if(new_request->size != 8)
	{
		printf("ERROR: new_request->size = %d\n", new_request->size);
		getchar();
	}

	new_request->begin_time = ssd->current_time;

	if(new_request->operation == READ)
	{
		handle_read_request(ssd, new_request);
	}
	else
	{
		handle_write_request(ssd, new_request);
	}
	
	new_request->cmplt_flag = 1;
	return ssd;
}

Status handle_write_request(struct ssd_info *ssd, struct request *req)
{
	unsigned int lpn = -1, new_ppn = INVALID_PPN, dup_ppn = INVALID_PPN, fing = 0;
	struct local loc;

	lpn = req->lsn / secno_num_per_page;
	fing = req->fing;

	if(fing < 1 || fing > UNIQUE_PAGE_NB)
	{
		printf("ERROR: fing = %u\n", fing);
		getchar();
	}

	dup_ppn = ssd->dram->map->F2P_entry[fing].pn;

	if(dup_ppn != INVALID_PPN)
		find_location_ppn(ssd, dup_ppn, &loc);

	if(dup_ppn != INVALID_PPN && use_remap(ssd, loc.block) == SUCCESS)
	{
		if (dup_ppn != ssd->dram->map->L2P_entry[lpn].pn)
		{
			invalidate_old_lpn(ssd, lpn);

			update_new_page_mapping(ssd, lpn, dup_ppn);
			update_nvram_oob(ssd, loc.block, 1);
			ssd->dram->map->in_nvram[lpn] = 1;

			req->response_time = update_nvram_ts(ssd, loc.block, NVRAM_WRITE_DELAY / 4) + FING_DELAY;
		}
		else
		{
			req->response_time = ssd->current_time + FING_DELAY;
		}
		
		ssd->reduced_writes++;
	}
	else
	{
		new_ppn = get_new_page(ssd);

		if(new_ppn == INVALID_PPN)
		{
			printf("ERROR: get new page fail\n");
			getchar();
		}

		find_location_ppn(ssd, new_ppn, &loc);

		req->response_time = ssd_page_write(ssd, loc.channel, loc.chip) + FING_DELAY;

		invalidate_old_lpn(ssd, lpn);
		
		update_new_page_mapping(ssd, lpn, new_ppn);
		ssd->dram->map->in_nvram[lpn] = 0;

		ssd->channel_head[loc.channel].chip_head[loc.chip].die_head[loc.die].plane_head[loc.plane].blk_head[loc.block].page_head[loc.page].fing = fing;
		ssd->dram->map->F2P_entry[fing].pn = new_ppn;

		if(ssd->free_sb_cnt <= (int)(MIN_SB_RATE * ssd->sb_cnt))
		{
			SuperBlock_GC(ssd, req);
		}
	}

	return SUCCESS;
}

Status handle_read_request(struct ssd_info *ssd, struct request *req)
{
	unsigned int lpn = -1, ppn = INVALID_PPN;
	struct local loc;

	lpn = req->lsn / secno_num_per_page;
	ppn = ssd->dram->map->L2P_entry[lpn].pn;

	if(ppn == INVALID_PPN)
	{
		printf("ERROR: READ INVALID_PPN\n");
		getchar();
	}
	else
	{
		find_location_ppn(ssd, ppn, &loc);

		req->response_time = ssd_page_read(ssd, loc.channel, loc.chip);
	}

	return SUCCESS;
}