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
#include "lsm_Tree.h"
extern int secno_num_per_page, secno_num_sub_page;

/***********************************DFTL*************************************************/
/*
	mapping buffer: cache hot mapping entries in two-level lists


	flag: write insert or read translation miss insert  
	    write��create a new translation subpage read 
		 read��when read translation miss, an extra translation read can occur and is hooked into sub read request
*/
struct ssd_info* insert2map_buffer(struct ssd_info* ssd, unsigned int lpn, struct request* req,unsigned int flag)
{
	unsigned int vpn, offset;
	unsigned int free_B, insert_B;
	struct buffer_group* buffer_node = NULL, * pt, key;
	struct sub_requet* sub_req = NULL;
	unsigned int r_vpn = 0, entry_cnt;
	char* r_bitmap;
	unsigned int i, tmp_lpn;

	switch (FTL)
	{
	case DFTL:
		vpn = lpn / ssd->map_entry_per_subpage;
		offset = lpn % ssd->map_entry_per_subpage;
		switch (DFTL)
		{
		case DFTL_BASE:
			insert_B = ssd->parameter->mapping_entry_size;   // unit is B
			key.group = lpn;
			break;
		case TPFTL:
			insert_B = ssd->parameter->mapping_entry_size;   // unit is B
			key.group = vpn;
			break;
		case SFTL:
			insert_B = ssd->parameter->subpage_capacity;        // manage at the granularity by translation page
			key.group = vpn;
			break;
		case FULLY_CACHED:
			insert_B = ssd->parameter->mapping_entry_size;        // manage at the granularity by translation page
			key.group = vpn;
			break;
		default:
			printf("Unidentifiable Phara\n");
			break;
		}
		break;
	case LSM_TREE_FTL:
		insert_B = ssd->parameter->mapping_entry_size;        // manage at the granularity by translation page
		key.group = lpn;
		break;
	case TAICHI_FTL:
		break;
	default:
		printf("Unidentifiable Phara\n");
		break;
	}

	//fprintf(ssd->sb_info, "%d\n",vpn);
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->mapping_buffer, (TREE_NODE*)& key);
	if (buffer_node == NULL)  // the mapping node is not in the mapping buffer 
	{
		if(flag == WRITE)
			ssd->write_tran_cache_miss++;
		//judge whether some mapping node is needed to be replaced
		free_B = ssd->dram->mapping_buffer->max_buffer_B - ssd->dram->mapping_buffer->buffer_B_count;
		while (free_B < insert_B) //replace
		{
			/*
			   insert to mapping command buffer
			   get tail node and invoke the insert to command buffer
			*/
			r_vpn = ssd->dram->mapping_buffer->buffer_tail->group;
			entry_cnt = ssd->dram->mapping_buffer->buffer_tail->entry_cnt;
			if (entry_cnt == 0)
			{
				printf("Look Here 15\n");
			}

			switch (FTL)
			{
			case DFTL:
				if (ssd->dram->tran_map->map_entry[r_vpn].dirty == 1)
				{
					ssd->tran_miss_wb++;
					insert2_command_buffer(ssd, ssd->dram->mapping_command_buffer, r_vpn, entry_cnt, req, SEQUENCE_MAPPING_DATA);
				}
				break;
			case LSM_TREE_FTL:
				if (ssd->dram->map->map_entry[r_vpn].dirty == 1)
				{
					insert2_mapping_command_buffer_in_order(ssd, r_vpn, req);
					ssd->tran_miss_wb++;
				}
				break;

			case TAICHI_FTL:
					break;
			default:
					break;
			}

			//delete the mapping node from the mapping command buffer 
			pt = ssd->dram->mapping_buffer->buffer_tail;
			avlTreeDel(ssd->dram->mapping_buffer, (TREE_NODE*)pt);

			if (ssd->dram->mapping_buffer->buffer_head->LRU_link_next == NULL) {
				ssd->dram->mapping_buffer->buffer_head = NULL;
				ssd->dram->mapping_buffer->buffer_tail = NULL;
			}
			else {
				ssd->dram->mapping_buffer->buffer_tail = ssd->dram->mapping_buffer->buffer_tail->LRU_link_pre;
				ssd->dram->mapping_buffer->buffer_tail->LRU_link_next = NULL;
			}
			pt->LRU_link_next = NULL;
			pt->LRU_link_pre = NULL;
			AVL_TREENODE_FREE(ssd->dram->mapping_buffer, (TREE_NODE*)pt);
			pt = NULL;

			switch (FTL)
			{
			case DFTL:
				switch (DFTL)
				{
				case DFTL_BASE:
				case TPFTL:
					ssd->dram->mapping_buffer->buffer_B_count -= entry_cnt * ssd->parameter->mapping_entry_size;
					break;
				case SFTL:
					ssd->dram->mapping_buffer->buffer_B_count -= entry_cnt * (ssd->parameter->mapping_entry_size / 2);
					break;
				default:
					break;
				}
				break;
			case LSM_TREE_FTL:
				ssd->dram->mapping_buffer->buffer_B_count -= entry_cnt * ssd->parameter->mapping_entry_size;
				break;
			case TAICHI_FTL:
				break;
			default:
				break;
			}

			free_B = ssd->dram->mapping_buffer->max_buffer_B - ssd->dram->mapping_buffer->buffer_B_count;
			ssd->dram->mapping_node_count--;
		}
		create_new_mapping_buffer(ssd, lpn, req,flag);
	}
	else  // there is the corresponding mapping node in the mappingb buffer 
	{
		//further judge whether the lpn mapping entry is cached in mapping buffer via bitmap
		if (ssd->dram->map->map_entry[lpn].cache_valid == 1)
		{
			ssd->write_tran_cache_hit++;
			/*
				int entry_cnt_map_buff = show_map_buffer(ssd);
				int statistis_cnt = get_cached_map_entry_cnt(ssd);
				if (entry_cnt_map_buff != statistis_cnt)
					printf("Look Here\n");
			*/
		}
		else
		{
			if(flag == WRITE)
				ssd->write_tran_cache_miss++;
			switch (FTL)
			{
			case DFTL:
				switch (DFTL)
				{
				case TPFTL:
					ssd->dram->map->map_entry[lpn].cache_valid = 1;
					ssd->dram->map->map_entry[lpn].dirty = 1;
					ssd->dram->mapping_buffer->buffer_B_count += ssd->parameter->mapping_entry_size;
					break;
				case DFTL_BASE:
					printf("Something cannot happen have happened!\n");
					//never happan
					getchar();
					break;
				case SFTL:
					//cache the mapping entry, new written lpn
					ssd->dram->map->map_entry[lpn].cache_valid = 1;
					break;
				default:
					break;
				}
				break;
			case LSM_TREE_FTL:
				ssd->dram->map->map_entry[lpn].cache_valid = 1;
				break;
			case TAICHI_FTL:
				break;
			default:
				break;
			}
		}
		//LRU management 
		if (ssd->dram->mapping_buffer->buffer_head != buffer_node)
		{
			if (ssd->dram->mapping_buffer->buffer_tail == buffer_node)
			{
				ssd->dram->mapping_buffer->buffer_tail = buffer_node->LRU_link_pre;
				buffer_node->LRU_link_pre->LRU_link_next = NULL;
			}
			else if (buffer_node != ssd->dram->mapping_buffer->buffer_head)
			{
				buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
				buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
			}
			buffer_node->LRU_link_next = ssd->dram->mapping_buffer->buffer_head;
			ssd->dram->mapping_buffer->buffer_head->LRU_link_pre = buffer_node;
			buffer_node->LRU_link_pre = NULL;
			ssd->dram->mapping_buffer->buffer_head = buffer_node;
		}
	}
	return ssd;
}

struct ssd_info* create_new_mapping_buffer(struct ssd_info* ssd, unsigned int lpn, struct request* req, unsigned int flag)
{
	unsigned int vpn, offset;
	struct buffer_group* new_node = NULL;
	unsigned int i;
	unsigned int ppn, tmp_lpn;
	struct sub_request* sub, * tran_read;

	//create a new mapping node 
	new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
	alloc_assert(new_node, "buffer_group_node");

	if (new_node == NULL)
	{
		printf("Buffer Node Allocation Error!\n");
		getchar();
		return ssd;
	}

	//different DFTL schemes lead to different loading granularity
	switch (FTL)
	{
	case DFTL:
		vpn = lpn / ssd->map_entry_per_subpage;
		offset = lpn % ssd->map_entry_per_subpage;
		switch (DFTL)
		{
		case DFTL_BASE:
			new_node->entry_cnt = 1;
			ssd->dram->map->map_entry[lpn].dirty = 1;
			ssd->dram->map->map_entry[lpn].cache_valid = 1;
			new_node->group = lpn;
			//new_node->ppn = ssd->dram->map->map_entry[lpn].pn;
			break;
		case TPFTL:
			new_node->entry_cnt = 1;
			ssd->dram->map->map_entry[lpn].dirty = 1;
			ssd->dram->map->map_entry[lpn].cache_valid = 1;
			new_node->group = vpn;
			//new_node->ppn = ssd->dram->tran_map->map_entry[vpn].pn;
			break;
		case SFTL:
			//read one translation subpage
			/*
				req == NULL  means read translation miss and create translation read and hook into sub read request
			*/
			if (ssd->dram->tran_map->map_entry[vpn].state != 0 && flag == WRITE)  //not the first entry in the cached node 
			{
				tran_read = tran_read_sub_reqeust(ssd, vpn); //generate a new translation read
				if (tran_read != NULL)  //hook the sub translation read request into the request
				{
					tran_read->next_node = NULL;
					tran_read->next_subs = NULL;
					tran_read->update_cnt = 0;
					if (req == NULL)
						req = tran_read;
					else
					{
						tran_read->next_subs = req->subs;
						req->subs = tran_read;
						tran_read->total_request = req;
					}
				}
			}

			new_node->entry_cnt = ssd->map_entry_per_subpage;  //including invalid mapping entries 
			for (i = 0; i < ssd->map_entry_per_subpage; i++)
			{
				tmp_lpn = vpn * ssd->map_entry_per_subpage + i;
				if (ssd->dram->map->map_entry[tmp_lpn].state != 0)
					ssd->dram->map->map_entry[tmp_lpn].cache_valid = 1;
			}
			switch (flag)
			{
			case READ:
				ssd->dram->tran_map->map_entry[vpn].dirty = 0;
				break;
			case WRITE:
				ssd->dram->tran_map->map_entry[vpn].dirty = 1;
				break;
			default:
				break;
			}
			new_node->group = vpn;
			//new_node->ppn = ssd->dram->tran_map->map_entry[vpn].pn;
			break;
		default:
			printf("Unidentifiable Phara\n");
			break;
		}
		ssd->dram->tran_map->map_entry[vpn].dirty = 1;
		break;
	case LSM_TREE_FTL:
		new_node->entry_cnt = 1;
		ssd->dram->map->map_entry[lpn].cache_valid = 1;
		new_node->group = lpn;
		switch (flag)
		{
		case READ:
			ssd->dram->map->map_entry[lpn].dirty = 0;
			break;
		case WRITE:
			ssd->dram->map->map_entry[lpn].dirty = 1;
			break;
		default:
			break;
		}
		break;
	case TAICHI_FTL:
		new_node->entry_cnt = 1;
		ssd->dram->map->map_entry[lpn].dirty = 1;
		ssd->dram->map->map_entry[lpn].cache_valid = 1;
		new_node->group = lpn;
		//new_node->ppn = ssd->dram->map->map_entry[lpn].pn;
		break;
	default:
		break;
	}


	new_node->LRU_link_pre = NULL;
	new_node->LRU_link_next = ssd->dram->mapping_buffer->buffer_head;
	if (ssd->dram->mapping_buffer->buffer_head != NULL)
	{
		ssd->dram->mapping_buffer->buffer_head->LRU_link_pre = new_node;
	}
	else
	{
		ssd->dram->mapping_buffer->buffer_tail = new_node;
	}
	ssd->dram->mapping_buffer->buffer_head = new_node;
	new_node->LRU_link_pre = NULL;
	avlTreeAdd(ssd->dram->mapping_buffer, (TREE_NODE*)new_node);

	switch (FTL)
	{
	case DFTL:
		switch (DFTL)
		{
		case DFTL_BASE:
		case TPFTL:
			ssd->dram->mapping_buffer->buffer_B_count += new_node->entry_cnt * ssd->parameter->mapping_entry_size;
			break;
		case SFTL:
			ssd->dram->mapping_buffer->buffer_B_count += new_node->entry_cnt * (ssd->parameter->mapping_entry_size / 2);
			break;
		default:
			break;
		}
		break;
	case LSM_TREE_FTL:
		ssd->dram->mapping_buffer->buffer_B_count += new_node->entry_cnt * ssd->parameter->mapping_entry_size;
		break;
	case TAICHI_FTL:
		break;
	default:
		break;
	}

	ssd->dram->mapping_node_count++;
	return ssd;
}

struct sub_request * tran_read_sub_reqeust(struct ssd_info* ssd, unsigned int vpn)
{
	struct sub_request* sub;
	struct local* tem_loc = NULL;
	unsigned int pn;

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));
	memset(sub, 0, sizeof(struct sub_request));

	if (sub == NULL)
	{
		return NULL;
	}
	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->update_cnt = 0;
	sub->read_flag = MAPPING_READ;
	pn = ssd->dram->tran_map->map_entry[vpn].pn;

	if (pn == -1) //hit in sub reqeust queues
	{
		ssd->req_read_hit_cnt++;

		tem_loc = (struct local*)malloc(sizeof(struct local));
		memset(tem_loc, 0, sizeof(struct local));
		sub->location = tem_loc;
		sub->lpn = vpn;
		sub->current_state = SR_R_DATA_TRANSFER;
		sub->current_time = ssd->current_time;
		sub->next_state = SR_COMPLETE;                                         //��Ϊ���״̬
		sub->next_state_predict_time = ssd->current_time + 1000;
		sub->complete_time = ssd->current_time + 1000;
	}
	else
	{
		sub->state = FULL_FLAG;  // all sectors needed to be read
		sub->size = secno_num_sub_page;
		sub->ppn = pn;
		sub->location = find_location_pun(ssd, pn);
		sub->lpn = vpn;
		creat_one_read_sub_req(ssd, sub);  //insert into channel read  req queue
	}
	return sub;
}


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
	unsigned int full_page, lsn, lun, last_lun, first_lun,i;
	unsigned int mask;
	unsigned int state,offset1 = 0, offset2 = 0, flag = 0;                                                                                       

	lsn = req->lsn;
	last_lun = (req->lsn + req->size - 1) / secno_num_sub_page;
	first_lun = req->lsn/ secno_num_sub_page;
	lun = first_lun;

	while (lun <= last_lun)     
	{
		state = 0; 
		offset1 = 0;
		offset2 = secno_num_sub_page - 1;

		if (lun == first_lun)
			offset1 = lsn - lun* secno_num_sub_page;
		if (lun == last_lun)
			offset2 = (lsn + req->size - 1) % secno_num_sub_page;

		for (i = offset1; i <= offset2; i++)
			state = SET_VALID(state, i);

		if (req->operation == READ)                                                   
			ssd = check_w_buff(ssd, lun, state, req);
		else if (req->operation == WRITE)
			ssd = insert2buffer(ssd, lun, state, NULL, req, USER_DATA);
		lun++;
	}
	return ssd;
}

struct ssd_info *handle_read_cache(struct ssd_info *ssd, struct request *req)           //��������棬�����
{
	return ssd;
}

struct ssd_info * check_w_buff(struct ssd_info *ssd, unsigned int lpn, int state, struct request *req)
{
	unsigned int sub_req_state , sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *buffer_node = NULL, key, *command_buffer_node = NULL, command_key;
	unsigned int chunk, off;
	struct sub_request* sub_w = NULL;
	unsigned int chan, unit;
/*
	chunk = lpn / MAX_LSN_PER_CHUNK;
	off = lpn % MAX_LSN_PER_CHUNK;
*/
	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE *)&key);
	if (buffer_node != NULL)
	{
		ssd->dram->data_buffer->read_hit++;
		return ssd;
	}

	command_key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_command_buffer, (TREE_NODE*)& command_key);
	if (command_buffer_node != NULL)
	{
		ssd->dram->data_buffer->read_hit++;
		return ssd;
	}

	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->read_data_buffer, (TREE_NODE*)& key);
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
			for (unit = 0; unit < ssd->parameter->subpage_page; unit++)
			{
				if (sub_w->luns[unit] == lpn)
				{
					ssd->dram->data_buffer->read_hit++;
					return ssd;
				}
			}
			sub_w = sub_w->next_node;
		}
	}

	read_reqeust(ssd, lpn, req, state, USER_DATA);
	ssd->dram->data_buffer->read_miss_hit++;

	return ssd;
	
	/*
	chunk = lpn / MAX_LSN_PER_CHUNK;
	off = lpn % MAX_LSN_PER_CHUNK;

	key.group = chunk;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE*)& key);

	if (buffer_node == NULL)
	{
		read_reqeust(ssd, lpn, req, state, USER_DATA);
		ssd->dram->data_buffer->read_miss_hit++;
	}
	else
	{
		if (buffer_node->state[off] > 0)
		{
			ssd->dram->data_buffer->read_hit++;
		}
		else
		{
			read_reqeust(ssd, lpn, req, state, USER_DATA);
			ssd->dram->data_buffer->read_miss_hit++;
		}
	}
	return ssd;
	*/
}

struct ssd_info* insert2_read_buffer(struct ssd_info* ssd, unsigned int lpn, int state)
{
	int write_back_count, flag = 0;
	unsigned int sector_count, free_sector = 0;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;

	switch (DATA_BUFFER_SCHEME)
	{
		case LPN_DATA_BUFFER:
			sector_count = secno_num_sub_page;
			key.group = lpn;
			buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->read_data_buffer, (TREE_NODE*)& key);

			if (buffer_node == NULL)
			{
				ssd->dram->read_data_buffer->read_miss_hit++;
				free_sector = ssd->dram->read_data_buffer->max_buffer_sector - ssd->dram->read_data_buffer->buffer_sector_count;
				if (free_sector >= sector_count)
				{
					flag = 1;
				}
				if (flag == 0)
				{
					write_back_count = sector_count - free_sector;
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
						ssd->dram->read_data_buffer->buffer_sector_count = ssd->dram->read_data_buffer->buffer_sector_count - secno_num_sub_page;

						write_back_count = write_back_count - secno_num_sub_page;
						//printf("write_back_count:%d\n", write_back_count);
					}
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
			break;
		default:
			break;
	}
	return ssd;
}

/*******************************************************************************
*The function is to write data to the buffer,Called by buffer_management()
********************************************************************************/
struct ssd_info * insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req, unsigned int data_type)
{
	int write_back_count, flag = 0;                                  
	unsigned int sector_count, free_sector = 0;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;

	struct sub_request* sub_req = NULL, * update = NULL;
	struct buffer_group* temp_node;
	struct buffer_group* replaced_node;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	unsigned int add_size;

	unsigned int chunk_id;
	unsigned int lsn_in_chunk,temp_chunk_lsn_count;

	switch (DATA_BUFFER_SCHEME)
	{
	case LPN_DATA_BUFFER:
		//sector_count = size(state); 
		sector_count = secno_num_sub_page;  //after 4KB aligning
		key.group = lpn;
		buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE*)& key);

		if (buffer_node == NULL)
		{
			free_sector = ssd->dram->data_buffer->max_buffer_sector - ssd->dram->data_buffer->buffer_sector_count;

			if (free_sector >= sector_count)
			{
				flag = 1;
			}
			if (flag == 0)
			{
				write_back_count = sector_count - free_sector;
				while (write_back_count > 0)
				{
					sub_req_state = ssd->dram->data_buffer->buffer_tail->stored;
					//sub_req_size = size(ssd->dram->data_buffer->buffer_tail->stored);
					sub_req_size = secno_num_sub_page;   //after aligning
					sub_req_lpn = ssd->dram->data_buffer->buffer_tail->group;

					insert2_command_buffer(ssd, ssd->dram->data_command_buffer, sub_req_lpn, sub_req_state, req, data_type);  //deal with tail sub-request
					ssd->dram->data_buffer->write_miss_hit++;
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
		break;
	case CHUNK_DATA_BUFFER:
		chunk_id = lpn / MAX_LSN_PER_CHUNK;
		lsn_in_chunk = lpn % MAX_LSN_PER_CHUNK;

		sector_count = size(state);
		key.group = chunk_id;
		buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->data_buffer, (TREE_NODE*)& key); 

		if (buffer_node == NULL)
		{//chunk miss
			ssd->dram->data_buffer->write_miss_hit++;
			//free sector
			free_sector = ssd->dram->data_buffer->max_buffer_sector - ssd->dram->data_buffer->buffer_sector_count;

			if (sector_count > free_sector)
			{
				weed_out_from_data_buffer(ssd,req,sector_count,lpn);
			}

			//insert a new data node 
			new_node = NULL;
			new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
			alloc_assert(new_node, "buffer_group_node");
			memset(new_node, 0, sizeof(struct buffer_group));

			//insert trunk into avl tree
			new_node->group = chunk_id;

			new_node->lsn_count = 0;
			for (int i = 0; i < MAX_LSN_PER_CHUNK; i++)
			{
				new_node->lsns[i] = 0;
				new_node->state[i] = 0;
			}

			new_node->lsns[lsn_in_chunk] = lpn;
			new_node->lsn_count = 1;
			new_node->stored = 1;
			new_node->state[lsn_in_chunk] = state;

			//handle lru link list
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
			//
			ssd->dram->data_buffer->buffer_sector_count += sector_count;
		}
		else
		{
			//chunk hit then judge whether lsn hits
			if (buffer_node->state[lsn_in_chunk] > 0) {
				ssd->dram->data_buffer->write_hit++;
			}
			else
			{
				ssd->dram->data_buffer->write_miss_hit++;

				free_sector = ssd->dram->data_buffer->max_buffer_sector - ssd->dram->data_buffer->buffer_sector_count;

				if (sector_count > free_sector)
				{
					weed_out_from_data_buffer(ssd, req, sector_count,lpn);
				}
				ssd->dram->data_buffer->buffer_sector_count += secno_num_sub_page;
				buffer_node->lsns[lsn_in_chunk] = lpn;
				buffer_node->state[lsn_in_chunk] = state;
				buffer_node->stored++;
				buffer_node->lsn_count++;
			}
			//insert to head 
			if (ssd->dram->data_buffer->buffer_head != buffer_node)
			{
				//not head
				if (ssd->dram->data_buffer->buffer_tail == buffer_node)
				{
					//tail
					ssd->dram->data_buffer->buffer_tail = buffer_node->LRU_link_pre;
					buffer_node->LRU_link_pre->LRU_link_next = NULL;
				}
				else if (buffer_node != ssd->dram->data_buffer->buffer_head)
				{
					//not head
					buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
					buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
				}
				//put buffer_node at the head of LRU list
				buffer_node->LRU_link_next = ssd->dram->data_buffer->buffer_head;
				ssd->dram->data_buffer->buffer_head->LRU_link_pre = buffer_node;
				buffer_node->LRU_link_pre = NULL;
				ssd->dram->data_buffer->buffer_head = buffer_node;
			}
		}
		break;
	default:
		break;
	}
	return ssd;
}

//Adjust chunk-level mapping scheme 
int Adjust_chunk_mapping(struct ssd_info* ssd, struct buffer_group* node, struct request *req)
{
	unsigned int node_len, cache_len;
	unsigned int chunk_id, tmp_lpn;
	unsigned int lpn;

	switch (FTL)
	{
	case DFTL:
	case LSM_TREE_FTL:
		return;
		break;
	case CHUNK_LEVEL_FTL:
	case TAICHI_FTL:
		node_len = node->lsn_count;
		chunk_id = node->group;

		//judge if corresponding chunk mapping entry need to be replaced 
		cache_len = ssd->dram->chunk_map->map_entry[chunk_id].cache_size;
		if (node_len > cache_len)
		{
			//the lpns of the chunk node form the new mapping entry in chunk-level mapping table
			//send all mapping enrty of cached lpn in chunk-level mapping table to flash
			for (int i = 0; i < MAX_LSN_PER_CHUNK; i++)
			{
				if (ssd->dram->chunk_map->map_entry[chunk_id].bitmap[i] == 1)
				{
					lpn = chunk_id * MAX_LSN_PER_CHUNK + i;
					insert2map_buffer(ssd, lpn, req, WRITE);
					
					ssd->dram->chunk_map->map_entry[chunk_id].bitmap[i] = 0;
					ssd->dram->chunk_map->map_entry[chunk_id].cache_size--;
				}
			}
			if (ssd->dram->chunk_map->map_entry[chunk_id].cache_size != 0)
				printf("Bitmap Error in Chunk-level FTL\n");
			else
				ssd->dram->chunk_map->map_entry[chunk_id].state = 0;  //state remark which mapping scheme is used to manage mapping entries.
		}
		else
		{
			//the mapping entry of all lpns of the chunk node are managed in lsm_Tree
			if (ssd->dram->chunk_map->map_entry[chunk_id].state == 0)
				printf("Mapping sheme selection error in Taichi-FTL\n");
		}
		break;
	default:
		break;
	}
	return 1;
}


//replace data node 
int weed_out_from_data_buffer(struct ssd_info* ssd,struct request *req,unsigned int sector_count,unsigned int lpn)
{
	unsigned int free_sector, temp_chunk_lsn_count;
	struct buffer_group* replaced_node;
	struct buffer_group* temp_node;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0,i;
	unsigned int lsn_in_chunk, chunk,chunk_id,replace_chunk_id;
	struct buffer_group * pt;

	ssd->debug_cnt++;
	//free sector

	chunk = lpn / MAX_LSN_PER_CHUNK;
	free_sector = ssd->dram->data_buffer->max_buffer_sector - ssd->dram->data_buffer->buffer_sector_count;
	replaced_node = NULL;
	while (sector_count > free_sector)
	{
		//find the chunk to be replaced
		temp_chunk_lsn_count = 0;
		temp_node = ssd->dram->data_buffer->buffer_tail;
		for (i = 0; i < LRU_WINDOW_SIZE && temp_node != NULL; i++) {
			if (temp_node->group != chunk)
			{
				if (temp_node == NULL)
					break;
				if (temp_node->lsn_count > temp_chunk_lsn_count) {
					temp_chunk_lsn_count = temp_node->lsn_count;
					replaced_node = temp_node;
				}
			}
			temp_node = temp_node->LRU_link_pre;
		}

		if (replaced_node == NULL)
		{
			printf("DATA BUFFER REPLACEMEMT ERROR\n");
		}
		
		//now temp_node is selected to be replaced and set bitmap information of corresponding chunk in mapping table
		replace_chunk_id = replaced_node->group;

		//judge which chunk mapping entries are cached 
		Adjust_chunk_mapping(ssd,replaced_node,req);

		for (i = 0; i < MAX_LSN_PER_CHUNK; ++i)
		{
			switch (FTL)
			{
			case DFTL:
				break;
			case CHUNK_LEVEL_FTL:
			case TAICHI_FTL:
				if (replaced_node->state[i] > 0)  //cached lpn
				//this position has been placed with lsn
					ssd->dram->chunk_map->map_entry[replace_chunk_id].bitmap[i] = 1;
				else
					ssd->dram->chunk_map->map_entry[replace_chunk_id].bitmap[i] = 0;
				break;
			case LSM_TREE_FTL:
				break;
			default:
				printf("enheng \n");
				break;
			}
		}

		for (i = 0; i < MAX_LSN_PER_CHUNK; ++i) 
		{
			if (replaced_node->state[i] > 0) {  //cached lpn
				//this position has been placed with lsn
				sub_req_state = replaced_node->state[i];
				sub_req_lpn = replaced_node->lsns[i];
				sub_req_size = secno_num_sub_page;
				insert2_command_buffer(ssd, ssd->dram->data_command_buffer, sub_req_lpn, sub_req_state, req, USER_DATA);  //deal with tail sub-request
				ssd->dram->data_buffer->buffer_sector_count -= sub_req_size;
			}
		}
		//fprintf(ssd->sb_info, "\n");
		free_sector = ssd->dram->data_buffer->max_buffer_sector - ssd->dram->data_buffer->buffer_sector_count;

		//delete replaced_node
		pt = replaced_node;
		avlTreeDel(ssd->dram->data_buffer, (TREE_NODE*)pt);
		if (pt == ssd->dram->data_buffer->buffer_tail) {
			if (ssd->dram->data_buffer->buffer_head->LRU_link_next == NULL) {
				ssd->dram->data_buffer->buffer_head = NULL;
				ssd->dram->data_buffer->buffer_tail = NULL;
			}
			else {
				ssd->dram->data_buffer->buffer_tail = ssd->dram->data_buffer->buffer_tail->LRU_link_pre;
				ssd->dram->data_buffer->buffer_tail->LRU_link_next = NULL;
			}
		}
		else if (pt == ssd->dram->data_buffer->buffer_head) {
			ssd->dram->data_buffer->buffer_head = pt->LRU_link_next;
			ssd->dram->data_buffer->buffer_head->LRU_link_pre = NULL;
		}
		else {
			pt->LRU_link_next->LRU_link_pre = pt->LRU_link_pre;
			pt->LRU_link_pre->LRU_link_next = pt->LRU_link_next;
		}
		pt->LRU_link_next = NULL;
		pt->LRU_link_pre = NULL;
		AVL_TREENODE_FREE(ssd->dram->data_buffer, (TREE_NODE*)pt);
		pt = NULL;
	}
	return SUCCESS;
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

//Orderly insert to mapping command buffer 
struct ssd_info* insert2_mapping_command_buffer_in_order(struct ssd_info* ssd, unsigned int lpn, struct request* req)
{
	struct  buffer_group *front, *tmp;
	struct  buffer_group *new_node;
	new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
	alloc_assert(new_node, "buffer_group_node");
	memset(new_node, 0, sizeof(struct buffer_group));


	new_node->LRU_link_next = NULL;
	new_node->LRU_link_pre = NULL;
	new_node->group = lpn;

	tmp = ssd->dram->mapping_command_buffer->buffer_head;
	front = tmp;
	if (tmp == NULL)
	{
		ssd->dram->mapping_command_buffer->buffer_head = new_node;
		ssd->dram->mapping_command_buffer->count++;
		return ssd;
	}

	while(tmp != NULL)
	{
		if (tmp->LRU_link_next == NULL)  //insert to tail
		{
			if (new_node->group < tmp->group)
			{
				if (tmp->LRU_link_pre == NULL)
				{
					new_node->LRU_link_next = tmp;
					tmp->LRU_link_pre = new_node;
					ssd->dram->mapping_command_buffer->buffer_head = new_node;
				}
				else
				{
					front->LRU_link_next = new_node;
					new_node->LRU_link_pre = front;
					new_node->LRU_link_next = tmp;
					tmp->LRU_link_pre = tmp;
				}
				ssd->dram->mapping_command_buffer->count++;
			}
			else if(new_node->group == tmp->group)
			{
				//no need to insert
				free(new_node);
				new_node = NULL;
			}
			else
			{
				tmp->LRU_link_next = new_node;
				new_node->LRU_link_pre = tmp;
				ssd->dram->mapping_command_buffer->count++;
			}
			break;
		}
		if (tmp->group < lpn)
		{
			front = tmp;
			tmp = tmp->LRU_link_next;
		}
		else if (tmp->group == lpn)
		{
			//no need to insert
			free(new_node);
			new_node = NULL;
			break;
		}
		else //insert the node
		{
			if (tmp ==  ssd->dram->mapping_command_buffer->buffer_head) //insert to the head  
			{
				new_node->LRU_link_next = tmp;
				tmp->LRU_link_pre = new_node;
				ssd->dram->mapping_command_buffer->buffer_head = new_node;
			}
			else
			{
				front->LRU_link_next = new_node;
				new_node->LRU_link_pre = front;
				new_node->LRU_link_next = tmp;
				tmp->LRU_link_pre = tmp;
			}
			ssd->dram->mapping_command_buffer->count++;

			break;
		}
	}

	if (ssd->dram->mapping_command_buffer->count == ssd->lsmTree->dataTableItems)
	{
		//trigger SMT dump
		smt_dump(ssd, req);
	}

	return ssd;
}

int check_buffer_sorted(struct ssd_info* ssd) {
	struct buffer_group* node;
	struct buffer_group* node_next;
	node = ssd->dram->mapping_command_buffer->buffer_head;
	node_next = node->LRU_link_next;
	while (node_next) {
		if (node_next->group < node->group) {
			return FAILURE;
		}
		node = node_next;
		node_next = node->LRU_link_next;
	}
	return SUCCESS;
}

void show_mapping_command_buffer(struct ssd_info* ssd)
{
	struct  buffer_group* node;

	fprintf(ssd->smt_info, "******data**********\n");
	node = ssd->dram->mapping_command_buffer->buffer_head;
	while (node)
	{
		fprintf(ssd->smt_info, "%d ", node->group);
		node = node->LRU_link_next;
	}
	fprintf(ssd->smt_info, "\n");
	fflush(ssd->smt_info);
}

int show_map_buffer(struct ssd_info* ssd)
{
	unsigned int cnt = 0;
	struct buffer_group* node;

	node = ssd->dram->mapping_buffer->buffer_head;

	while (node)
	{
		fprintf(ssd->buffer_info, "lpn = %d  cached state = %d ", node->group,ssd->dram->map->map_entry[node->group].cache_valid);
		cnt++;
		node = node->LRU_link_next;
	}
	fflush(ssd->buffer_info);
	return cnt;
}

int get_cached_map_entry_cnt(struct ssd_info* ssd)
{
	int cnt = 0;
	int max_para = ssd->parameter->plane_die * ssd->parameter->die_chip * ssd->parameter->chip_num;
	int page_num = (ssd->parameter->page_block * ssd->parameter->block_plane * max_para) / (1 + ssd->parameter->overprovide);
	int sub_page_num = page_num * ssd->parameter->subpage_page;
	for (int i = 0; i < sub_page_num; i++)
	{
		if (ssd->dram->map->map_entry[i].cache_valid == 1)
			cnt++;
	}
	return cnt;
}

int get_mapping_command_buffer(struct ssd_info* ssd)
{
	struct  buffer_group* node;
	unsigned int count = 0;
	node = ssd->dram->mapping_command_buffer->buffer_head;
	while (node)
	{
		count++;
		node = node->LRU_link_next;
	}
	return count;
}
//SMT dump
struct ssd_info* smt_dump(struct ssd_info* ssd, struct request* req)
{
	struct  buffer_group *node, *tmp;
	unsigned int i;
	struct SMT* s;
	unsigned int lpn, ppn;

	//fprintf(ssd->smt, "*************Sorted Mapping Table***************\n");
	s = (struct SMT*)malloc(sizeof(struct SMT));
	s->hit_times = 0;
	s->ppn = -1;
	s->allocated_ppn_flag = 0;
	//s->read_count = 0;
	s->dataBlockTable = (struct dataBlock*)malloc(sizeof(struct dataBlock));
	s->dataBlockTable->table = (Addr * *)malloc(sizeof(Addr*) * ssd->lsmTree->dataTableItems);

	for (i = 0; i < ssd->lsmTree->dataTableItems; i++)
	{
		node = ssd->dram->mapping_command_buffer->buffer_head;
		lpn = node->group;
		ppn = ssd->dram->map->map_entry[lpn].pn;
		s->dataBlockTable->table[i] = (Addr*)malloc(sizeof(Addr) * 2);
		s->dataBlockTable->table[i][0] = lpn;
		s->dataBlockTable->table[i][1] = ppn;

		ssd->dram->map->map_entry[lpn].cache_valid = 0;
	    //delete mapping entries from the mapping command  buffer
		tmp = node;
		node = node->LRU_link_next;
		ssd->dram->mapping_command_buffer->buffer_head = node;
		free(tmp);
		tmp = NULL;
		ssd->dram->mapping_command_buffer->count--;
	}
	s->manifest[0] = s->dataBlockTable->table[0][0];
	s->manifest[1] = s->dataBlockTable->table[ssd->lsmTree->dataTableItems - 1][0];
	s->smt_id = ssd->smt_id++;
	
	check_smt(ssd, s);

	insertToL0(ssd,s,req);

//fprintf(ssd->smt, "\n");
//fflush(ssd->smt);
	return ssd;
}


/*
   insert to data commond buffer
	  data_type = USER_DATA, state records the sector bitmap
	  data_type = MAPPING_DATA, state records the replaced mapping entry
 */
struct ssd_info* insert2_command_buffer(struct ssd_info* ssd, struct buffer_info* command_buffer, unsigned int lpn, unsigned int state, struct request* req, unsigned int data_type)
{
	unsigned int i = 0, j = 0;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group* command_buffer_node = NULL, * pt, * new_node = NULL, key;
	struct sub_request* sub_req = NULL;
	int tmp;
	unsigned int loop, off;
	unsigned int lun_state, lun, tem_lun, lun_type;
	unsigned int blk_type = 0;
	//unsigned int used_size, max_size;

	key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(command_buffer, (TREE_NODE*)& key);

	if (state == 0)
	{
		printf("Debug Look Here 13\n");
	}

	switch (data_type)
	{
	case USER_DATA:    // in data command buffer 
	case FRESH_USER_DATA:
	case GC_USER_DATA:
		blk_type = USER_BLOCK;
		break;
	case SEQUENCE_MAPPING_DATA:   // in mapping data command buffer 
	case LSM_TREE_MAPPING_DATA:
	case GC_SEQUENCE_MAPPING_DATA:
		blk_type = MAPPING_BLOCK;
		break;
	default:
		break;
	}

	if (command_buffer_node == NULL)
	{
		if (data_type == SEQUENCE_MAPPING_DATA || data_type == LSM_TREE_MAPPING_DATA)
			ssd->write_tran_cache_miss;

		if (data_type == USER_DATA)
			ssd->dram->data_buffer->write_miss_hit++;

		new_node = NULL;
		new_node = (struct buffer_group*)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;
		new_node->data_type = data_type;   // node type 
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
			if (blk_type == USER_BLOCK)
			{
				loop = command_buffer->command_buff_page / ssd->parameter->subpage_page;

				//printf("begin to flush command_buffer\n");
				for (i = 0; i < loop; i++)
				{
					sub_req = (struct sub_request*)malloc(sizeof(struct sub_request));
					alloc_assert(sub_req, "sub_request");
					memset(sub_req, 0, sizeof(struct sub_request));
					sub_req->tran_read = NULL;
					sub_req->lun_count = 0;
					sub_req->smt = NULL;
					if (blk_type == USER_BLOCK)
						sub_req->req_type = USER_DATA;
					if (blk_type == MAPPING_BLOCK)
						sub_req->req_type = MAPPING_DATA;

					for (j = 0; j < ssd->parameter->subpage_page; j++)
					{
						lun = command_buffer->buffer_tail->group;
						lun_state = command_buffer->buffer_tail->stored;
						lun_type = command_buffer->buffer_tail->data_type;
						sub_req->luns[sub_req->lun_count] = lun;
						sub_req->lun_state[sub_req->lun_count] = lun_state;
						sub_req->types[sub_req->lun_count++] = lun_type;

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
					}
					create_sub_w_req(ssd, sub_req, req, blk_type);
				}
				if (command_buffer->command_buff_page != 0)
				{
					printf("command buff flush failed\n");
					getchar();
				}
			}
			else //mapping block
			{
				switch (FTL)
				{
				case DFTL:  //sub page size 
					loop = command_buffer->command_buff_page / ssd->parameter->subpage_page;

					//printf("begin to flush command_buffer\n");
					for (i = 0; i < loop; i++)
					{
						sub_req = (struct sub_request*)malloc(sizeof(struct sub_request));
						alloc_assert(sub_req, "sub_request");
						memset(sub_req, 0, sizeof(struct sub_request));
						sub_req->tran_read = NULL;
						sub_req->lun_count = 0;
						sub_req->smt = NULL;
						if (blk_type == USER_BLOCK)
							sub_req->req_type = USER_DATA;
						if (blk_type == MAPPING_BLOCK)
							sub_req->req_type = MAPPING_DATA;

						for (j = 0; j < ssd->parameter->subpage_page; j++)
						{
							lun = command_buffer->buffer_tail->group;
							lun_state = command_buffer->buffer_tail->stored;
							lun_type = command_buffer->buffer_tail->data_type;
							sub_req->luns[sub_req->lun_count] = lun;
							sub_req->lun_state[sub_req->lun_count] = lun_state;
							sub_req->types[sub_req->lun_count++] = lun_type;

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
						}
						create_sub_w_req(ssd, sub_req, req, blk_type);
					}
					if (command_buffer->command_buff_page != 0)
					{
						printf("command buff flush failed\n");
						getchar();
					}
					break;
				case LSM_TREE_FTL:  //page size  not used in this version
					loop = command_buffer->command_buff_page;

					//printf("begin to flush command_buffer\n");
					for (i = 0; i < loop; i++)
					{
						sub_req = (struct sub_request*)malloc(sizeof(struct sub_request));
						alloc_assert(sub_req, "sub_request");
						memset(sub_req, 0, sizeof(struct sub_request));
						sub_req->tran_read = NULL;
						sub_req->lun_count = 0;
						sub_req->req_type = MAPPING_DATA;

						lun = command_buffer->buffer_tail->group;
						lun_state = command_buffer->buffer_tail->stored;
						lun_type = command_buffer->buffer_tail->data_type;
						sub_req->lpn = lun;
						sub_req->state = lun_state;

						create_sub_w_req(ssd, sub_req, req, blk_type);
						command_buffer->command_buff_page--;

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
					}
					if (command_buffer->command_buff_page != 0)
					{
						printf("command buff flush failed\n");
						getchar();
					}
					break;
				case TAICHI_FTL:
					break;
				default:
					break;
				}
			}
		}
	}
	else
	{
		if (blk_type == MAPPING_BLOCK)
			ssd->write_tran_cache_hit++;
		else
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
		if (blk_type == USER_DATA)
			command_buffer_node->stored = command_buffer_node->stored | state;
		else
		{
			command_buffer_node->stored = ssd->map_entry_per_subpage;
		}
	}

	/*
	  apply for free superblock  for used data
	  the reason why apply for new superblock at this point is to reduce the influence of mixture of GC data and user data
	*/
	if (ssd->open_sb[blk_type] == NULL)
		find_active_superblock(ssd, req, blk_type);

	if (ssd->open_sb[blk_type]->next_wr_page == ssd->parameter->page_block) //no free superpage in the superblock
		find_active_superblock(ssd, req, blk_type);
	return ssd;
}

//req -> write sub request  
Status update_read_request(struct ssd_info *ssd, unsigned int lpn, unsigned int state, struct sub_request *req, unsigned int commond_buffer_type)  //in this code, the state is for sector and maximal 32 bits
{
	struct sub_request *sub_r = NULL;
	struct sub_request * sub = NULL;
	struct channel_info * p_ch = NULL;
	struct local * loc = NULL;
	struct local * tem_loc = NULL;
	unsigned int flag = 0;
	int i= 0;
	unsigned int chan, chip, die, plane;
	unsigned int update_cnt;
	unsigned int off=0;
	struct sub_request* tmp_update;

	unsigned int pn,state1 = 0;

	if (commond_buffer_type == SEQUENCE_MAPPING_DATA)
	{
		printf("Cannot Happen in S-FTL\n");
	}

	//create sub quest 
	sub = (struct local*)malloc(sizeof(struct sub_request));
	if (sub == NULL)
	{
		return NULL;
	}

	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->tran_read = NULL;
	if (commond_buffer_type == USER_COMMAND_BUFFER)
	{
		pn = translate(ssd,lpn,sub);
		state1 = ssd->dram->map->map_entry[lpn].state;
	}
	if (state1 == 0) //hit in reqeust queue
	{
		ssd->req_read_hit_cnt++;

		tem_loc = (struct local *)malloc(sizeof(struct local));
		sub->location = tem_loc;
		sub->current_state = SR_R_DATA_TRANSFER;
		sub->current_time = ssd->current_time;
		sub->next_state = SR_COMPLETE;                                       
		sub->next_state_predict_time = ssd->current_time + 1000;
		sub->complete_time = ssd->current_time + 1000;

		insert2update_reqs(ssd, req, sub);
		return SUCCESS;
	}

	sub->location = find_location_pun(ssd, pn);
	
	sub->read_flag = UPDATE_READ;

	if (ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].page_head[sub->location->page].luns[sub->location->sub_page] != lpn)
	{
		printf("Update Read Error!\n");
		getchar();
	}

	sub->lpn = lpn;
	sub->ppn = pn;
	sub->size = secno_num_sub_page;

	creat_one_read_sub_req(ssd, sub);  //insert into channel read  req queue
	insert2update_reqs(ssd, req, sub); 

	return SUCCESS;
}

void insert2update_reqs(struct ssd_info* ssd, struct sub_request* req, struct sub_request* update)
{
	switch (req->update_cnt)
	{
	case 0:
		req->update_0 = update;
		break;
	case 1:
		req->update_1 = update;
		break;
	case 2:
		req->update_2 = update;
		break;
	case 3:
		req->update_3 = update;
		break;
	default:
		break;
	}
	req->update_cnt++;
}


unsigned int translate(struct ssd_info* ssd, unsigned int lpn, struct sub_request* sub)
{
	unsigned int ppn = INVALID_PPN;
	unsigned int vpn;
	struct sub_qequest* tran_read;

	sub->tran_read = NULL;

	switch (FTL)
	{
	case DFTL:
		vpn = lpn / ssd->map_entry_per_subpage;
		if (DFTL)  // dftl schemes are used 
		{
			if (ssd->dram->map->map_entry[lpn].state != 0)  //valid mapping entry
			{
				if (ssd->dram->map->map_entry[lpn].cache_valid == 1 || ssd->dram->tran_map->map_entry[vpn].state == 0)  //cached in mapping buffer or sub  request queue
				{
					ppn = ssd->dram->map->map_entry[lpn].pn;
					ssd->read_tran_cache_hit++;
				}
				else  //obtain the mapping entry from the flash
				{
					ssd->read_tran_cache_miss++;
					//create one read translation requets
					tran_read = tran_read_sub_reqeust(ssd, vpn);
					//hook the translation read into sub request
					sub->tran_read_count = 1;
					//dram->lsm_tree_mapping_command_buf->smts = (struct SMT**)malloc(sizeof(struct SMT*) * dram->lsm_tree_mapping_command_buf->max_smt_count);
					sub->tran_read = (struct sub_request**)malloc(sizeof(struct sub_request*));
					sub->tran_read[0] = tran_read;
					ppn = ssd->dram->map->map_entry[lpn].pn;
					//insert to mapping buffer 
					ssd->dram->tran_map->map_entry[vpn].state++;
					//create_new_mapping_buffer(ssd, lpn, NULL);
					insert2map_buffer(ssd, lpn, sub->total_request, READ);
				}
			}
		}
		else
		{
			if (ssd->dram->map->map_entry[lpn].state == 0) //
			{
				ppn = -1;
			}
			else
			{
				ppn = ssd->dram->map->map_entry[lpn].pn;
			}
		}
		break;
	case LSM_TREE_FTL:
		if (ssd->dram->map->map_entry[lpn].cache_valid == 1)  // cached in mapping cache
		{
			ppn = ssd->dram->map->map_entry[lpn].pn;
			ssd->read_tran_cache_hit++;
		}
		else
		{
			ssd->read_tran_cache_miss++;
			//create one read translation requets
			search_lpn(ssd, sub, lpn,READ);
			ppn = ssd->dram->map->map_entry[lpn].pn;
			//insert to mapping buffer 
			insert2map_buffer(ssd, lpn, sub->total_request, READ);
		}
		break;
	case TAICHI_FTL:
		
		break;
	default:
		break;
	}



	return ppn;
}

int read_reqeust(struct ssd_info *ssd, unsigned int lpn, struct request *req, unsigned int state,unsigned int data_type)
{
	struct sub_request* sub = NULL;
	struct local * loc = NULL;
	struct local *tem_loc = NULL;
	unsigned int data_size;
	unsigned int off = 0;
	unsigned int read_size;

	unsigned int pn = 0;

	//create a sub request 
	sub = (struct sub_request*)malloc(sizeof(struct sub_request));
	alloc_assert(sub, "sub_request");
	memset(sub, 0, sizeof(struct sub_request));

	if (sub == NULL)
	{
		return NULL;
	}
	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->update_cnt = 0;
	if (req == NULL)  //request is NULL means update reqd in this version. sub->update
		req = sub;
	else
	{
		sub->next_subs = req->subs;
		req->subs = sub;
		sub->total_request = req;
	}

	//address translation 
	pn = translate(ssd, lpn, sub);

	if (pn == -1) //hit in sub reqeust queue
	{
		ssd->req_read_hit_cnt++;
		tem_loc = (struct local*)malloc(sizeof(struct local));
		sub->location = tem_loc;

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
		sub->read_flag = REQ_READ;
		if (data_type == USER_DATA)
		{
			sub->state = state;
			sub->size = size(state);
		}
		else  //translation data 
		{
			sub->state = FULL_FLAG;  // all sectors needed to be read
			sub->size = secno_num_sub_page;
		}
		sub->ppn = pn;
		sub->location = find_location_pun(ssd, pn);
		sub->lpn = lpn;

		creat_one_read_sub_req(ssd, sub);  //insert into channel read  req queue
		return SUCCESS;
	}
}

Status create_sub_w_req(struct ssd_info* ssd, struct sub_request* sub, struct request* req, unsigned int block_type)
{
	unsigned int i;
	unsigned int lun, lun_state;
	struct sub_request* sub_w = NULL, * tmp = NULL;
	unsigned int used_block_type = block_type;
	unsigned int chunk_id;

	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->operation = WRITE;
	sub->location = (struct local*)malloc(sizeof(struct local));
	alloc_assert(sub->location, "sub->location");
	memset(sub->location, 0, sizeof(struct local));
	sub->current_state = SR_WAIT;
	sub->current_time = ssd->current_time;
	sub->size = secno_num_per_page;
	sub->begin_time = ssd->current_time;

	if (sub->begin_time < 0)
		printf("Look here:begin time error 1\n");

	sub->update_cnt = 0;
	sub->tran_read = NULL;

	if (req != NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
		sub->total_request = req;
	}

	//allocate free page 
	ssd->open_sb[used_block_type]->pg_off = (ssd->open_sb[used_block_type]->pg_off + 1) % ssd->sb_pool[used_block_type].blk_cnt;
	sub->location->channel = ssd->open_sb[used_block_type]->pos[ssd->open_sb[used_block_type]->pg_off].channel;
	sub->location->chip = ssd->open_sb[used_block_type]->pos[ssd->open_sb[used_block_type]->pg_off].chip;
	sub->location->die = ssd->open_sb[used_block_type]->pos[ssd->open_sb[used_block_type]->pg_off].die;
	sub->location->plane = ssd->open_sb[used_block_type]->pos[ssd->open_sb[used_block_type]->pg_off].plane;
	sub->location->block = ssd->open_sb[used_block_type]->pos[ssd->open_sb[used_block_type]->pg_off].block;
	sub->location->page = ssd->open_sb[used_block_type]->next_wr_page;

	if (ssd->open_sb[used_block_type]->next_wr_page == ssd->parameter->page_block)
	{
		printf("Debug LOOK Here 14\n");
	}

	if (ssd->open_sb[used_block_type]->pg_off == ssd->sb_pool[used_block_type].blk_cnt - 1)
		ssd->open_sb[used_block_type]->next_wr_page++;
	sub->ppn = find_ppn(ssd, sub->location->channel, sub->location->chip, sub->location->die, sub->location->plane, sub->location->block, sub->location->page);

	//old data read 
	switch (block_type)
	{
	case USER_BLOCK:
		//handle update write 
		for (i = 0; i < sub->lun_count; i++)
		{
			lun = sub->luns[i];
			lun_state = sub->lun_state[i]; // data_type == 0 (user data) state is the sector state; data_type = 1 (mapping data),state is the valid mapping entry count
			
			//pre set the mapping table 
			switch (FTL)
			{
			case CHUNK_LEVEL_FTL:
			case TAICHI_FTL:
				chunk_id = lun / MAX_LSN_PER_CHUNK;
				if (ssd->dram->chunk_map->map_entry[chunk_id].state == 0)
				{
					if (ssd->dram->chunk_map->map_entry[chunk_id].map_state == READY)
					{
						ssd->dram->chunk_map->map_entry[chunk_id].pn = find_pun(ssd, sub->location->channel, sub->location->chip, sub->location->die, sub->location->plane, sub->location->block, sub->location->page, i);
						ssd->dram->chunk_map->map_entry[chunk_id].map_state = PREDISTRIBUTION;
					}
					ssd->dram->chunk_map->map_entry[chunk_id].state = 1;
				}
				break;
			default:
				break;
			}

			/*
			if (ssd->dram->map->map_entry[lun].state != 0)  // read the old mapping entry for invalidating the old data 
			{
				//update_read_request(ssd, lun, lun_state, sub, USER_DATA);
				Get_old_ppn(ssd, lun);
			}
			*/
			
		}
		break;
	case MAPPING_BLOCK:  //no old data data
		switch (FTL)
		{
		case DFTL:
			break;
		case LSM_TREE_FTL:
			//sub->smt->ppn = sub->ppn;
			//fprintf(ssd->allocation_info, "smt ID = %d, ppn = %d ", sub->smt->smt_id, sub->smt->ppn);
			//fprintf(ssd->allocation_info, "channel = %d, chip = %d  die = %d  plane = %d block = %d page = %d\n", sub->location->channel, sub->location->chip, sub->location->die, sub->location->plane, sub->location->block, sub->location->page);
			//fflush(ssd->allocation_info);
			break;
		case CHUNK_LEVEL_FTL:
			break;
		case TAICHI_FTL:
			break;
		default:
			break;
		}

	default:
		break;
	}

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

	//Write_cnt(ssd, sub->location->channel);
	return SUCCESS;
}

void  Get_old_ppn(struct ssd_info* ssd, unsigned int lpn)
{
	unsigned int pun, vpn;
//	unsigned int channel, chip, die, plane, block, page, unit;
	struct sub_request* sub_read;
	struct request* req;
	struct local* loc;
	unsigned int address;
	unsigned int chunk_id,offset;

	//1. get the old mapping entry
	switch (FTL)
	{
	case DFTL:
		switch (DFTL)
		{
		case FULLY_CACHED:
			pun = ssd->dram->map->map_entry[lpn].pn;
			break;
		case SFTL:
			if (ssd->dram->map->map_entry[lpn].cache_valid == 1) //cached 
				pun = ssd->dram->map->map_entry[lpn].pn;
			else  //not cached 
			{
				vpn = lpn / ssd->map_entry_per_subpage;
				if (ssd->dram->tran_map->map_entry[vpn].state == 0)
				{
					//printf("Maybe Mapping entry in command buffer or sub request queue\n");
					pun = ssd->dram->map->map_entry[lpn].pn;
					break;
				}

				//create a read sub request
				sub_read = (struct sub_request*)malloc(sizeof(struct sub_request));
				if (sub_read == NULL)
				{
					printf("MALLOC ERROR\N");
					return;
				}
				sub_read->next_node = NULL;
				sub_read->next_subs = NULL;
				sub_read->update_cnt = 0;
				sub_read->tran_read = NULL;

				//req = sub->total_request;
				//sub_read->next_subs = req->subs;
				//req->subs = sub_read;
				//sub_read->total_request = req;

				sub_read->read_flag = INVALIDATE_READ;
				sub_read->lpn = vpn;
				sub_read->ppn = ssd->dram->tran_map->map_entry[vpn].pn;
				sub_read->location = find_location_pun(ssd, sub_read->ppn);
				sub_read->size = secno_num_sub_page;
				creat_one_read_sub_req(ssd, sub_read); //read the corresponding mapping entry and find corresponding pun of old lun
				pun = ssd->dram->map->map_entry[lpn].pn;
			}
			break;
		default:
			break;
		}

		break;
	case LSM_TREE_FTL:
		if (ssd->dram->map->map_entry[lpn].cache_valid == 1) //cached in mapping cache 
			pun = ssd->dram->map->map_entry[lpn].pn;
		else
		{
			if (ssd->dram->map->map_entry[lpn].state == 0)  //maybe in sub request or command buffer 
				pun = ssd->dram->map->map_entry[lpn].pn;
			else
			{
				address = search_lpn(ssd, NULL, lpn,WRITE);
				if (address == -1)
				{
					printf("LSM Tree Maintain Error, Cannot Find Corresponding Mapping Entries!\n");
					getchar();
				}
				pun = ssd->dram->map->map_entry[lpn].pn;
				if (pun != address)
				{
					printf("Mapping Table Matain Error in LSM-Tree\n");
					getchar();
				}
			}
		}
		break;
	case TAICHI_FTL:
		if (ssd->dram->map->map_entry[lpn].cache_valid == 1) //cached in mapping cache 
			pun = ssd->dram->map->map_entry[lpn].pn;
		else
		{
			//1. first reaseach the corresponding mapping entry in chunk-level table 
			chunk_id = lpn / MAX_LSN_PER_CHUNK;
			offset = lpn % MAX_LSN_PER_CHUNK;
			if (ssd->dram->chunk_map->map_entry[chunk_id].bitmap[offset] == 1)
			{
				pun = ssd->dram->map->map_entry[lpn].pn;
			}
			else//2. search lpn in lsm-Tree
			{
				address = search_lpn(ssd, NULL, lpn, WRITE);
				pun = ssd->dram->map->map_entry[lpn].pn;
			}
		}
		break;
	default:
		break;
	}
}

Status creat_one_read_sub_req(struct ssd_info *ssd, struct sub_request* sub)
{
	unsigned int lpn,flag;
	struct channel_info * p_ch = NULL;
	struct local *loc = NULL;
	struct sub_request* sub_r;
	unsigned int channel, chip, die, plane, block, page, subpage,flash_lpn;
	unsigned int page_type;

	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	plane = sub->location->plane;
	block = sub->location->block;
	page = sub->location->page;

	page_type = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].type;

	switch (page_type)
	{
	case USER_DATA:
		subpage = sub->location->sub_page;
		flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].luns[subpage];
		sub->size = secno_num_sub_page;
		break;
	case MAPPING_DATA:
		switch (FTL)
		{
		case DFTL:
			subpage = sub->location->sub_page;
			flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].luns[subpage];
			sub->size = secno_num_sub_page;
			break;
		case LSM_TREE_FTL:
			flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
			sub->size = secno_num_per_page;
			break;
		case CHUNK_LEVEL_FTL:
			//flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
			break;
		case TAICHI_FTL:
			//flash_lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	lpn = sub->lpn;

	if (flash_lpn != lpn)
	{
		printf("Read Request Error\n");
	}

	sub->begin_time = ssd->current_time;
	if (sub->begin_time < 0)
		printf("Look here: begin time error 2\n");

	sub->current_state = SR_WAIT;
	sub->current_time = ssd->current_time;
	sub->next_state = SR_R_C_A_TRANSFER;
	sub->next_state_predict_time = 0x7fffffffffffffff;
	sub->suspend_req_flag = NORMAL_TYPE;

	loc = sub->location;
	p_ch = &ssd->channel_head[loc->channel];
	
	sub->operation = READ;
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
		if (sub->read_flag == INVALIDATE_READ)
		{
			free(sub->location);
			sub->location = NULL;
			free(sub);
			sub = NULL;
		}
		else
		{
			sub->current_state = SR_R_DATA_TRANSFER;
			sub->current_time = ssd->current_time;
			sub->next_state = SR_COMPLETE;
			sub->next_state_predict_time = ssd->current_time + 1000;
			sub->complete_time = ssd->current_time + 1000;
		}
	}
	//Read_cnt(ssd, loc->channel);
	return SUCCESS;
}
