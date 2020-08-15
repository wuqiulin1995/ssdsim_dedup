#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "ssd.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"
#include "assert.h"
extern int secno_num_per_page, secno_num_sub_page;

/**********************************************************************************************************************************************
*Buff strategy:Blocking buff strategy
*1--first check the buffer is full, if dissatisfied, check whether the current request to put down the data, if so, put the current request,
*if not, then block the buffer;
*
*2--If buffer is blocked, select the replacement of the two ends of the page. If the two full page, then issued together to lift the buffer
*block; if a partial page 1 full page or 2 partial page, then issued a pre-read request, waiting for the completion of full page and then issued
*And then release the buffer block.
***********************************************************************************************************************************************/
struct ssd_info *buffer_management(struct ssd_info *ssd)
{
	struct request *new_request;

#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n", ssd->current_time);
#endif

	ssd->dram->current_time = ssd->current_time;
	new_request = ssd->request_work; 

	if(new_request->size != 8)
	{
		printf("ERROR new_request->size = %d\n", new_request->size);
	}

	handle_write_buffer(ssd, new_request);

	if (new_request->subs == NULL)  //sub requests are cached in data buffer 
	{
		new_request->begin_time = ssd->current_time;
		new_request->response_time = ssd->current_time + 1000;
	}

	new_request->cmplt_flag = 1;
	ssd->buffer_full_flag = 0;
	return ssd;
}

struct ssd_info *handle_write_buffer(struct ssd_info *ssd, struct request *req)
{
	unsigned int full_page, lsn, lpn, last_lpn, first_lpn,i;
	unsigned int mask;
	unsigned int state, offset1 = 0, offset2 = 0;                                                                                       

	lsn = req->lsn;
	first_lpn = req->lsn/ secno_num_per_page;
	last_lpn = (req->lsn + req->size - 1) / secno_num_per_page;
	lpn = first_lpn;

	while (lpn <= last_lpn)     
	{
		state = 0; 
		offset1 = 0;
		offset2 = secno_num_per_page - 1;

		if (lpn == first_lpn)
			offset1 = lsn - lpn * secno_num_per_page;
		if (lpn == last_lpn)
			offset2 = (lsn + req->size - 1) % secno_num_per_page;

		for (i = offset1; i <= offset2; i++)
			state = SET_VALID(state, i);

		if (req->operation == READ)                                                   
			ssd = check_w_buff(ssd, lpn, state, req);
		else if (req->operation == WRITE)
			ssd = insert2buffer(ssd, lpn, state, NULL, req);
		lpn++;
	}
	return ssd;
}

struct ssd_info* check_w_buff(struct ssd_info *ssd, unsigned int lpn, int state, struct request *req)
{
	unsigned int sub_req_state , sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *buffer_node = NULL, key, *command_buffer_node = NULL, command_key;
	struct sub_request *sub_w = NULL;
	unsigned int chan;

	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE *)&key);
	if (buffer_node != NULL)
	{
		ssd->dram->data_buffer->read_hit++;
		return ssd;
	}

	command_key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_command_buffer, (TREE_NODE*)&command_key);
	if (command_buffer_node != NULL)
	{
		ssd->dram->data_buffer->read_hit++;
		return ssd;
	}

	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->read_data_buffer, (TREE_NODE*)&key);
	if (buffer_node != NULL)
	{
		ssd->dram->read_data_buffer->read_hit++;
		return ssd;
	}

	for (chan = 0; chan < ssd->parameter->channel_number; chan++)
	{
		sub_w = ssd->channel_head[chan].subs_w_head;
		while (sub_w != NULL)
		{
			if (sub_w->lpn == lpn)
			{
				ssd->dram->data_buffer->read_hit++;
				return ssd;
			}
			sub_w = sub_w->next_node;
		}
	}

	read_reqeust(ssd, lpn, req, state);
	ssd->dram->data_buffer->read_miss_hit++;

	return ssd;
}

struct ssd_info* insert2_read_buffer(struct ssd_info* ssd, unsigned int lpn, int state)
{
	int write_back_count = 0, sector_count;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;

	sector_count = secno_num_per_page;   //after 4KB aligning
	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->read_data_buffer, (TREE_NODE*)& key);

	if (buffer_node == NULL)
	{
		ssd->dram->read_data_buffer->read_miss_hit++;
		write_back_count = ssd->dram->read_data_buffer->buffer_sector_count + sector_count - ssd->dram->read_data_buffer->max_buffer_sector;
		while (write_back_count > 0)
		{
			pt = ssd->dram->read_data_buffer->buffer_tail;
			avlTreeDel(ssd->dram->read_data_buffer, (TREE_NODE*)pt);
			if (ssd->dram->read_data_buffer->buffer_head->LRU_link_next == NULL)
			{
				ssd->dram->read_data_buffer->buffer_head = NULL;
				ssd->dram->read_data_buffer->buffer_tail = NULL;
			}
			else
			{
				ssd->dram->read_data_buffer->buffer_tail = ssd->dram->read_data_buffer->buffer_tail->LRU_link_pre;
				ssd->dram->read_data_buffer->buffer_tail->LRU_link_next = NULL;
			}
			pt->LRU_link_next = NULL;
			pt->LRU_link_pre = NULL;
			AVL_TREENODE_FREE(ssd->dram->read_data_buffer, (TREE_NODE*)pt);
			pt = NULL;
			ssd->dram->read_data_buffer->buffer_sector_count = ssd->dram->read_data_buffer->buffer_sector_count - secno_num_per_page;

			write_back_count = write_back_count - secno_num_per_page;
		}
		
		new_node = NULL;
		new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;

		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = ssd->dram->read_data_buffer->buffer_head;		
		if (ssd->dram->read_data_buffer->buffer_head != NULL)
		{
			ssd->dram->read_data_buffer->buffer_head->LRU_link_pre = new_node;
		}
		else
		{
			ssd->dram->read_data_buffer->buffer_tail = new_node;
		}
		ssd->dram->read_data_buffer->buffer_head = new_node;
		avlTreeAdd(ssd->dram->read_data_buffer, (TREE_NODE*)new_node);
		ssd->dram->read_data_buffer->buffer_sector_count += sector_count;
	}
	else
	{
		ssd->dram->read_data_buffer->read_hit++;
		if (ssd->dram->read_data_buffer->buffer_head != buffer_node)
		{
			if (ssd->dram->read_data_buffer->buffer_tail == buffer_node)
			{
				ssd->dram->read_data_buffer->buffer_tail = buffer_node->LRU_link_pre;
				buffer_node->LRU_link_pre->LRU_link_next = NULL;
			}
			else
			{
				buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
				buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
			}

			buffer_node->LRU_link_next = ssd->dram->read_data_buffer->buffer_head;
			ssd->dram->read_data_buffer->buffer_head->LRU_link_pre = buffer_node;
			buffer_node->LRU_link_pre = NULL;
			ssd->dram->read_data_buffer->buffer_head = buffer_node;
		}
	}

	return ssd;
}

/*******************************************************************************
*The function is to write data to the buffer,Called by buffer_management()
********************************************************************************/
struct ssd_info* insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req)
{
	int write_back_count = 0, sector_count;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;

	sector_count = secno_num_per_page;  //after 4KB aligning
	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE*)& key);

	if (buffer_node == NULL)
	{
		ssd->dram->data_buffer->write_miss_hit++;

		write_back_count = ssd->dram->data_buffer->buffer_sector_count + sector_count - ssd->dram->data_buffer->max_buffer_sector;
		while (write_back_count > 0)
		{
			sub_req_state = ssd->dram->data_buffer->buffer_tail->stored;
			//sub_req_size = size(ssd->dram->data_buffer->buffer_tail->stored);
			sub_req_size = secno_num_per_page;   //after aligning
			sub_req_lpn = ssd->dram->data_buffer->buffer_tail->group;

			insert2_command_buffer(ssd, ssd->dram->data_command_buffer, sub_req_lpn, sub_req_state, req);  //deal with tail sub-request
			ssd->dram->data_buffer->buffer_sector_count = ssd->dram->data_buffer->buffer_sector_count - sub_req_size;

			pt = ssd->dram->data_buffer->buffer_tail;
			avlTreeDel(ssd->dram->data_buffer, (TREE_NODE*)pt);
			if (ssd->dram->data_buffer->buffer_head->LRU_link_next == NULL) {
				ssd->dram->data_buffer->buffer_head = NULL;
				ssd->dram->data_buffer->buffer_tail = NULL;
			}
			else {
				ssd->dram->data_buffer->buffer_tail = ssd->dram->data_buffer->buffer_tail->LRU_link_pre;
				ssd->dram->data_buffer->buffer_tail->LRU_link_next = NULL;
			}
			pt->LRU_link_next = NULL;
			pt->LRU_link_pre = NULL;
			AVL_TREENODE_FREE(ssd->dram->data_buffer, (TREE_NODE*)pt);
			pt = NULL;

			write_back_count = write_back_count - sub_req_size;
		}

		new_node = NULL;
		new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));
		new_node->group = lpn;
		new_node->stored = state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = ssd->dram->data_buffer->buffer_head;
		if (ssd->dram->data_buffer->buffer_head != NULL) {
			ssd->dram->data_buffer->buffer_head->LRU_link_pre = new_node;
		}
		else {
			ssd->dram->data_buffer->buffer_tail = new_node;
		}
		ssd->dram->data_buffer->buffer_head = new_node;
		new_node->LRU_link_pre = NULL;
		avlTreeAdd(ssd->dram->data_buffer, (TREE_NODE*)new_node);
		ssd->dram->data_buffer->buffer_sector_count += sector_count;
	}
	else
	{
		ssd->dram->data_buffer->write_hit++;
		if (req != NULL)
		{
			if (ssd->dram->data_buffer->buffer_head != buffer_node)
			{
				if (ssd->dram->data_buffer->buffer_tail == buffer_node)
				{
					ssd->dram->data_buffer->buffer_tail = buffer_node->LRU_link_pre;
					buffer_node->LRU_link_pre->LRU_link_next = NULL;
				}
				else if (buffer_node != ssd->dram->data_buffer->buffer_head)
				{
					buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
					buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
				}
				buffer_node->LRU_link_next = ssd->dram->data_buffer->buffer_head;
				ssd->dram->data_buffer->buffer_head->LRU_link_pre = buffer_node;
				buffer_node->LRU_link_pre = NULL;
				ssd->dram->data_buffer->buffer_head = buffer_node;
			}
			req->complete_lsn_count += size(state);
		}
	}
	
	return ssd;
}

/*********************************************************************************************
*The no_buffer_distribute () function is processed when ssd has no dram��
*This is no need to read and write requests in the buffer inside the search, directly use the 
*creat_sub_request () function to create sub-request, and then deal with.
*********************************************************************************************/
struct ssd_info *no_buffer_distribute(struct ssd_info *ssd)
{
	return ssd;
}

/***********************************************************************************
*According to the status of each page to calculate the number of each need to deal 
*with the number of sub-pages, that is, a sub-request to deal with the number of pages
************************************************************************************/
unsigned int size(unsigned int stored)
{
	unsigned int i, total = 0, mask = 0x80000000;

#ifdef DEBUG
	printf("enter size\n");
#endif
	for (i = 1; i <= 32; i++)
	{
		if (stored & mask) total++;     
		stored <<= 1;
	}
#ifdef DEBUG
	printf("leave size\n");
#endif
	return total;
}

/*
   insert to data commond buffer
 */
struct ssd_info* insert2_command_buffer(struct ssd_info* ssd, struct buffer_info* command_buffer, unsigned int lpn, unsigned int state, struct request* req)
{
	unsigned int i = 0, loop = 0;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *command_buffer_node = NULL, *pt, *new_node = NULL, key;
	struct sub_request *sub_req = NULL;

	key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(command_buffer, (TREE_NODE*)& key);

	if (state == 0)
	{
		printf("Debug Look Here 13\n");
	}

	if (command_buffer_node == NULL)
	{
		ssd->dram->data_buffer->write_miss_hit++;

		new_node = NULL;
		new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = command_buffer->buffer_head;
		if (command_buffer->buffer_head != NULL)
		{
			command_buffer->buffer_head->LRU_link_pre = new_node;
		}
		else
		{
			command_buffer->buffer_tail = new_node;
		}
		command_buffer->buffer_head = new_node;
		new_node->LRU_link_pre = NULL;
		avlTreeAdd(command_buffer, (TREE_NODE*)new_node);
		command_buffer->command_buff_page++;

		if (command_buffer->command_buff_page >= command_buffer->max_command_buff_page)
		{
			loop = command_buffer->command_buff_page;

			//printf("begin to flush command_buffer\n");
			for (i = 0; i < loop; i++)
			{
				sub_req = (struct sub_request*)malloc(sizeof(struct sub_request));
				alloc_assert(sub_req, "sub_request");
				memset(sub_req, 0, sizeof(struct sub_request));

				sub_req->lpn = command_buffer->buffer_tail->group;
				sub_req->state = command_buffer->buffer_tail->stored;

				//delete the data node from command buffer
				pt = command_buffer->buffer_tail;
				avlTreeDel(command_buffer, (TREE_NODE*)pt);
				if (command_buffer->buffer_head->LRU_link_next == NULL) {
					command_buffer->buffer_head = NULL;
					command_buffer->buffer_tail = NULL;
				}
				else {
					command_buffer->buffer_tail = command_buffer->buffer_tail->LRU_link_pre;
					command_buffer->buffer_tail->LRU_link_next = NULL;
				}
				pt->LRU_link_next = NULL;
				pt->LRU_link_pre = NULL;
				AVL_TREENODE_FREE(command_buffer, (TREE_NODE*)pt);
				pt = NULL;

				command_buffer->command_buff_page--;

				create_sub_w_req(ssd, sub_req, req);
			}
			if (command_buffer->command_buff_page != 0)
			{
				printf("command buff flush failed\n");
				getchar();
			}
		}
	}
	else
	{
		ssd->dram->data_buffer->write_hit++;

		if (command_buffer->buffer_head != command_buffer_node)
		{
			if (command_buffer->buffer_tail == command_buffer_node)      
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = NULL;
				command_buffer->buffer_tail = command_buffer_node->LRU_link_pre;
			}
			else
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = command_buffer_node->LRU_link_next;     //������м�ڵ㣬�����ǰһ���ͺ�һ���ڵ��ָ��
				command_buffer_node->LRU_link_next->LRU_link_pre = command_buffer_node->LRU_link_pre;
			}
			command_buffer_node->LRU_link_next = command_buffer->buffer_head;              //�ᵽ����
			command_buffer->buffer_head->LRU_link_pre = command_buffer_node;
			command_buffer_node->LRU_link_pre = NULL;
			command_buffer->buffer_head = command_buffer_node;
		}
			
		command_buffer_node->stored = command_buffer_node->stored | state;
	}

	/*
	  apply for free superblock  for used data
	  the reason why apply for new superblock at this point is to reduce the influence of mixture of GC data and user data
	*/
	if (ssd->open_sb == NULL)
		find_active_superblock(ssd, req);

	if (ssd->open_sb->next_wr_page == ssd->parameter->page_block) //no free superpage in the superblock
		find_active_superblock(ssd, req);
	return ssd;
}

unsigned int translate(struct ssd_info* ssd, unsigned int lpn, struct sub_request* sub)
{
	unsigned int ppn = INVALID_PPN;

	ppn = ssd->dram->map->L2P_entry[lpn].pn;

	return ppn;
}

Status read_reqeust(struct ssd_info *ssd, unsigned int lpn, struct request *req, unsigned int state)
{
	struct sub_request *sub = NULL;
	struct local *loc = NULL;
	unsigned int pn = 0;

	//create a sub request 
	sub = (struct sub_request*)malloc(sizeof(struct sub_request));
	alloc_assert(sub, "sub_request");
	memset(sub, 0, sizeof(struct sub_request));

	if (sub == NULL)
	{
		return FAILURE;
	}
	sub->next_node = NULL;
	sub->next_subs = NULL;

	sub->next_subs = req->subs;
	req->subs = sub;
	sub->total_request = req;

	//address translation 
	pn = translate(ssd, lpn, sub);

	if (pn == -1) //hit in sub reqeust queue
	{
		ssd->req_read_hit_cnt++;
		loc = (struct local*)malloc(sizeof(struct local));
		sub->location = loc;

		sub->lpn = lpn;
		sub->current_state = SR_R_DATA_TRANSFER;
		sub->current_time = ssd->current_time;
		sub->next_state = SR_COMPLETE;                                      
		sub->next_state_predict_time = ssd->current_time + 1000;
		sub->complete_time = ssd->current_time + 1000;

		return SUCCESS;
	}
	else
	{
		sub->lpn = lpn;
		sub->ppn = pn;
		sub->location = find_location_ppn(ssd, pn);
		sub->state = state;
		sub->size = size(state);

		creat_one_read_sub_req(ssd, sub);  //insert into channel read  req queue
		return SUCCESS;
	}
}

Status create_sub_w_req(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	unsigned int i;
	struct sub_request *sub_w = NULL;

	sub->operation = WRITE;
	sub->size = secno_num_per_page;
	sub->current_state = SR_WAIT;
	sub->current_time = ssd->current_time;
	sub->begin_time = ssd->current_time;
	if (sub->begin_time < 0)
		printf("Look here:begin time error 1\n");

	sub->next_node = NULL;
	sub->next_subs = NULL;

	if (req != NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
		sub->total_request = req;
	}

	sub->location = (struct local*)malloc(sizeof(struct local));
	alloc_assert(sub->location, "sub->location");
	memset(sub->location, 0, sizeof(struct local));

	//allocate free page 
	ssd->open_sb->pg_off = (ssd->open_sb->pg_off + 1) % ssd->open_sb->blk_cnt;
	sub->location->channel = ssd->open_sb->pos[ssd->open_sb->pg_off].channel;
	sub->location->chip = ssd->open_sb->pos[ssd->open_sb->pg_off].chip;
	sub->location->die = ssd->open_sb->pos[ssd->open_sb->pg_off].die;
	sub->location->plane = ssd->open_sb->pos[ssd->open_sb->pg_off].plane;
	sub->location->block = ssd->open_sb->pos[ssd->open_sb->pg_off].block;
	sub->location->page = ssd->open_sb->next_wr_page;

	if (sub->location->page == ssd->parameter->page_block)
	{
		printf("Debug LOOK Here 14\n");
	}

	if (ssd->open_sb->pg_off == ssd->open_sb->blk_cnt - 1)
		ssd->open_sb->next_wr_page++;

	sub->ppn = find_ppn(ssd, sub->location->channel, sub->location->chip, sub->location->die, sub->location->plane, sub->location->block, sub->location->page);

	//insert into sub request queue 
	sub_w = ssd->channel_head[sub->location->channel].subs_w_head;
	if (ssd->channel_head[sub->location->channel].subs_w_tail != NULL)
	{
		ssd->channel_head[sub->location->channel].subs_w_tail->next_node = sub;
		ssd->channel_head[sub->location->channel].subs_w_tail = sub;
	}
	else
	{
		ssd->channel_head[sub->location->channel].subs_w_head = sub;
		ssd->channel_head[sub->location->channel].subs_w_tail = sub;
	}

	return SUCCESS;
}

Status creat_one_read_sub_req(struct ssd_info *ssd, struct sub_request *sub)
{
	unsigned int flag;
	struct channel_info *p_ch = NULL;
	struct local *loc = NULL;
	struct sub_request *sub_r;
	unsigned int channel, chip, die, plane, block, page, flash_lpn;

	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	plane = sub->location->plane;
	block = sub->location->block;
	page = sub->location->page;

	flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;

	if (flash_lpn != sub->lpn)
	{
		printf("Read Request Error\n");
	}

	sub->operation = READ;
	sub->size = secno_num_per_page;
	sub->begin_time = ssd->current_time;
	sub->current_time = ssd->current_time;
	if (sub->begin_time < 0)
		printf("Look here: begin time error 2\n");
	sub->current_state = SR_WAIT;
	sub->next_state = SR_R_C_A_TRANSFER;
	sub->next_state_predict_time = 0x7fffffffffffffff;

	loc = sub->location;
	p_ch = &ssd->channel_head[loc->channel];
	sub_r = ssd->channel_head[loc->channel].subs_r_head;

	flag = 0;
	while (sub_r != NULL)
	{
		if (sub_r->ppn == sub->ppn)                          
		{
			flag = 1;
			break;
		}
		sub_r = sub_r->next_node;
	}
	
	if (flag == 0)          
	{
		ssd->channel_head[loc->channel].chip_head[loc->chip].die_head[loc->die].read_cnt++;
		if (ssd->channel_head[loc->channel].subs_r_tail != NULL)
		{
			ssd->channel_head[loc->channel].subs_r_tail->next_node = sub;       
			ssd->channel_head[loc->channel].subs_r_tail = sub;
		}
		else
		{
			ssd->channel_head[loc->channel].subs_r_head = sub;
			ssd->channel_head[loc->channel].subs_r_tail = sub;
		}
	}
	else
	{
		sub->current_state = SR_R_DATA_TRANSFER;
		sub->current_time = ssd->current_time;
		sub->next_state = SR_COMPLETE;
		sub->next_state_predict_time = ssd->current_time + 1000;
		sub->complete_time = ssd->current_time + 1000;
	}
	return SUCCESS;
}