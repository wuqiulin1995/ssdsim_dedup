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
	unsigned int i = 0,j=0;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].plane_erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num = ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_subpage_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = -1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].start_time = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_cnt = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].close_open_read_flag = 0;

	for (i = 0; i<ssd->parameter->page_block; i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state = PG_SUB;
		//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state = 0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn = -1;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].type = -1;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].smt = NULL;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].read_disturb_cnt = 0;

		for (j = 0; j < ssd->parameter->subpage_page; j++)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lun_state[j] = 0;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].luns[j] = -1;
		}
	}
	ssd->erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page += ssd->parameter->page_block;

	return SUCCESS;
}

/******************************************************************************************
*function is to read out the old active page, set invalid, migrate to the new valid page
*******************************************************************************************/
Status move_page(struct ssd_info * ssd, struct local *location, unsigned int move_plane,unsigned int * transfer_size)
{
	return SUCCESS;
}

Status NAND_read(struct ssd_info *ssd, struct sub_request * req)
{
	unsigned int chan, chip, die, plane, block, page,subpage;
	unsigned int lpn;
	unsigned int i, j, start_page_number, end_page_number, block_read_cnt;
	int last_page;
	int block_type;

	chan = req->location->channel;
	chip = req->location->chip;
	die = req->location->die;
	plane = req->location->plane;
	block = req->location->block;
	page = req->location->page;
	subpage = req->location->sub_page;

	block_type = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].block_type;

	ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_cnt++;
	block_read_cnt = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_cnt;

	start_page_number = page / PAGE_TYPE * PAGE_TYPE;
	end_page_number = start_page_number + PAGE_TYPE - 1;
	last_page = ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page;

	if (last_page < ssd->parameter->page_block - 1)
	{
		ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].close_open_read_flag = 1;
	}

	for (i = 0; i <= last_page; i++)
	{
		if (i < start_page_number || i > end_page_number)
		{
			ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].read_disturb_cnt++;
		}
	}

	switch (block_type)
	{
	case USER_BLOCK:
		if (req->read_flag == UPDATE_READ)
			ssd->data_update_cnt++;
		else
			ssd->data_read_cnt++;
		break;
	case MAPPING_BLOCK:
		if (req->read_flag == UPDATE_READ)
			ssd->tran_update_cnt++;
		else
			ssd->tran_read_cnt++;
		break;
	default: 
		break;
	}
	ssd->read_count++;

	if (block_read_cnt >= BLOCK_READ_THRESHOLD)
	{
		refresh(ssd, req);
		ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].read_cnt = 0;
		ssd->channel_head[chan].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].close_open_read_flag = 0;
	}

	return SUCCESS;
}


Status NAND_program(struct ssd_info* ssd, struct sub_request* req)
{
	unsigned int channel, chip, die, plane, block, page, lpn;
	unsigned int i, lun;
	unsigned int req_type, block_type;

	req_type = req->req_type;

	//lpn = req->lpn;
	channel = req->location->channel;
	chip = req->location->chip;
	die = req->location->die;
	plane = req->location->plane;
	block = req->location->block;
	page = req->location->page;


	block_type = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].block_type;

	if (block_type != req_type)
	{
		printf("LOOK HERE, Error: Block Allocation Error!!\n");
	}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;  //inlitialization is -1
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_write_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

	if (page == 0)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].start_time = ssd->current_time;
	}

	switch (req_type)
	{
	case USER_DATA:
		//program the data 
		for (i = 0; i < ssd->parameter->subpage_page; i++)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].luns[i] = req->luns[i];
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_state[i] = req->lun_state[i];
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_types[i] = req->types[i];

			switch (req->types[i])
			{
			case USER_DATA:
				ssd->data_program_cnt++;
				break;
			case FRESH_USER_DATA:
				ssd->fresh_data_program_cnt++;
				break;
			case GC_USER_DATA:
				ssd->gc_data_program_cnt++;
				break;
			default:
				break;
			}
		}
		break;
	case MAPPING_DATA:
		switch (FTL)
		{
		case DFTL:
			//program the data 
			for (i = 0; i < ssd->parameter->subpage_page; i++)
			{
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].luns[i] = req->luns[i];
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_state[i] = req->lun_state[i];
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_types[i] = req->types[i];
				switch (req->types[i])
				{
				case SEQUENCE_MAPPING_DATA:
					ssd->tran_program_cnt++;
					break;
				case GC_SEQUENCE_MAPPING_DATA:
					ssd->gc_tran_program_cnt++;
					break;
				default:
					break;
				}
			}
			break;
		case LSM_TREE_FTL:
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = req->lpn;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state = req->state;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].smt = req->smt;

			ssd->tran_program_cnt += ssd->parameter->subpage_page;
			if (req->smt->smt_id == -1)
				printf("Look Here 21\n");
			//fprintf(ssd->flash_info,"smt id = %d \n",req->lpn);
			//fflush(ssd->flash_info);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].written_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].type = req_type;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page != page)
		return FAILURE;

	ssd->write_flash_count++;
	ssd->program_count++;
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

/*********************************************************************************************
*this function is a simulation of a real write operation, to the pre-processing time to use
*********************************************************************************************/
Status write_page(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int active_block, unsigned int *ppn)
{
	int last_write_page = 0;
	last_write_page = ++(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page);
	if (last_write_page >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page = 0;
		printf("error! the last write page larger than max!!\n");
		return ERROR;
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[last_write_page].written_count++;
	ssd->write_flash_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].test_pre_count++;
	
	*ppn = find_ppn(ssd, channel, chip, die, plane, active_block, last_write_page);
	
	return SUCCESS;
}

/***********************************************************************************************************
*function is to modify the page page to find the state and the corresponding dram in the mapping table value
***********************************************************************************************************/
struct ssd_info *flash_page_state_modify(struct ssd_info *ssd, struct sub_request *sub, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn, full_page;
	struct local *location;
	struct direct_erase *new_direct_erase, *direct_erase_node;

	return ssd;
}

