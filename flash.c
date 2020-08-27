#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>

#include "initialize.h"
#include "ssd.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"


/******************************************************************************************
*function is to erase the operation, the channel, chip, die, plane under the block erase
*******************************************************************************************/
Status erase_operation(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block)
{
	unsigned int i = 0;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].plane_erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num = ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = -1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;

	for (i = 0; i<ssd->parameter->page_block; i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn = -1;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].ref_cnt = -1;
	}
	ssd->erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page += ssd->parameter->page_block;

	return SUCCESS;
}

Status NAND_read(struct ssd_info *ssd, struct sub_request * req)
{
	unsigned int chan, chip, die, plane, block, page;

	chan = req->location->channel;
	chip = req->location->chip;
	die = req->location->die;
	plane = req->location->plane;
	block = req->location->block;
	page = req->location->page;

	ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_read_count++;

	ssd->data_read_cnt++;

	return SUCCESS;
}


Status NAND_program(struct ssd_info* ssd, struct sub_request* req)
{
	unsigned int channel, chip, die, plane, block, page, lpn;

	channel = req->location->channel;
	chip = req->location->chip;
	die = req->location->die;
	plane = req->location->plane;
	block = req->location->block;
	page = req->location->page;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;  //inlitialization is -1
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_write_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = req->lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].ref_cnt = 1;

	ssd->data_program_cnt++;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page != page)
		return FAILURE;

	return SUCCESS;
}

Status NAND_multi_plane_program(struct ssd_info* ssd, struct sub_request* req0, struct sub_request* req1)
{
	unsigned flag0,flag1;
	flag0 = NAND_program(ssd, req0);
	flag1 = NAND_program(ssd, req1);

	return (flag0 & flag1);
}

Status NAND_multi_plane_read(struct ssd_info* ssd, struct sub_request* req0, struct sub_request* req1)
{
	unsigned flag0, flag1;
	flag0 = NAND_read(ssd, req0);
	flag1 = NAND_read(ssd, req1);

	return (flag0 & flag1);
}

__int64 ssd_page_read(struct ssd_info *ssd, unsigned int channel, unsigned int chip)
{
	__int64 start_transfer_ts = 0, read_time = 0, transfer_time = 0;

	read_time = ssd->parameter->time_characteristics.tR;
	transfer_time = 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tWC;

	if ((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE) ||
			((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE) &&
			(ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time)))
	{
		ssd->channel_head[channel].chip_head[chip].current_state = CHIP_READ_BUSY;
		ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->current_time + read_time;
	}
	else
	{
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time += read_time;
	}

	start_transfer_ts = ssd->channel_head[channel].chip_head[chip].next_state_predict_time;
	
	if ((ssd->channel_head[channel].current_state == CHANNEL_IDLE) ||
		((ssd->channel_head[channel].next_state == CHANNEL_IDLE) &&
		(ssd->channel_head[channel].next_state_predict_time <= start_transfer_ts)))
	{
		ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = start_transfer_ts + transfer_time;
	}
	else
	{
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time += transfer_time;
	}

	ssd->data_read_cnt++;

	return ssd->channel_head[channel].next_state_predict_time;
}

__int64 ssd_page_write(struct ssd_info *ssd, unsigned int channel, unsigned int chip)
{
	__int64 start_prog_ts = 0, prog_time = 0, transfer_time = 0;

	prog_time = ssd->parameter->time_characteristics.tPROG;
	transfer_time = 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tWC;

	if ((ssd->channel_head[channel].current_state == CHANNEL_IDLE) ||
		((ssd->channel_head[channel].next_state == CHANNEL_IDLE) &&
		(ssd->channel_head[channel].next_state_predict_time <= ssd->current_time)))
	{
		ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + transfer_time;
	}
	else
	{
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time += transfer_time;
	}

	start_prog_ts = ssd->channel_head[channel].chip_head[chip].next_state_predict_time;

	if ((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE) ||
			((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE) &&
			(ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= start_prog_ts)))
	{
		ssd->channel_head[channel].chip_head[chip].current_state = CHIP_WRITE_BUSY;
		ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->current_time + prog_time;
	}
	else
	{
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time += prog_time;
	}	

	ssd->data_program_cnt++;

	return ssd->channel_head[channel].chip_head[chip].next_state_predict_time;
}