#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>

#include "initialize.h"
#include "interface.h"
#include "ssd.h"
#include "buffer.h"
#include "ftl.h"
#include "flash.h"

Status erase_operation(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block)
{
	unsigned int i = 0;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = -1;

	for (i = 0; i<ssd->parameter->page_block; i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].fing = 0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn_entry = NULL;
	}
	ssd->erase_count++;

	return SUCCESS;
}

__int64 ssd_page_read(struct ssd_info *ssd, unsigned int channel, unsigned int chip)
{
	__int64 start_transfer_ts = 0, read_time = 0, transfer_time = 0;

	read_time = ssd->parameter->time_characteristics.tR;
	transfer_time = 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tRC;

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

	start_prog_ts = ssd->channel_head[channel].next_state_predict_time;

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

Status update_flash_ts(struct ssd_info *ssd, __int64 blocking_to_ts)
{
	int i = 0, j = 0;

	for(i = 0; i < ssd->parameter->channel_number; i++)
	{
		for(j = 0; j < ssd->channel_head[i].chip; j++)
		{
			if(ssd->channel_head[i].chip_head[j].next_state_predict_time < blocking_to_ts)
			{
				ssd->channel_head[i].chip_head[j].next_state_predict_time = blocking_to_ts;
			}
		}
	}

	return SUCCESS;
}

__int64 update_nvram_ts(struct ssd_info *ssd, __int64 need_time)
{
	if(ssd->nvram_log->next_avail_time < ssd->current_time)
	{
		ssd->nvram_log->next_avail_time = ssd->current_time + need_time;
	}
	else
	{
		ssd->nvram_log->next_avail_time += need_time;
	}

	return 	ssd->nvram_log->next_avail_time;
}