#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include "ssd.h"
#include "flash.h"
#include "buffer.h"
#include "ftl.h"
#include "fcl.h"
#include "initialize.h"

extern int secno_num_per_page, secno_num_sub_page;

//deal with read requests at one time
Status service_2_read(struct ssd_info* ssd, unsigned int channel)
{
	unsigned int chip, subs_count, i;
	unsigned int aim_die, plane_round, plane0, plane1;
	unsigned int mp_flag;
	struct sub_request *p_sub0, *p_sub1, *d_sub;
	struct sub_request **sub_r_request = NULL;
	unsigned int MP_flag;
	unsigned int max_sub_num;

	subs_count = 0;
	max_sub_num = ssd->parameter->chip_channel[channel]*ssd->parameter->die_chip * ssd->parameter->plane_die * PAGE_INDEX;

	sub_r_request = (struct sub_request**)malloc(max_sub_num * sizeof(struct sub_request*));
	alloc_assert(sub_r_request, "sub_r_request");

	for (i = 0; i < max_sub_num; i++)
		sub_r_request[i] = NULL;

	for (chip = 0; chip < ssd->channel_head[channel].chip; chip++)
	{
		if ((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE) ||
			((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE) &&
			(ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time)))
		{
			for (aim_die = 0; aim_die < ssd->parameter->die_chip; aim_die++)
			{
				MP_flag = 0;
				for (plane_round = 0; plane_round < ssd->parameter->plane_die / 2; plane_round++)
				{
					plane0 = plane_round * 2;
					plane1 = plane_round * 2 + 1;
					p_sub0 = get_first_plane_read_request(ssd, channel, chip, aim_die, plane0);
					p_sub1 = get_first_plane_read_request(ssd, channel, chip, aim_die, plane1);

					//judge whether multiple-read can be carried out 
					mp_flag = IS_Multi_Plane(ssd, p_sub0, p_sub1);

					if (mp_flag == 1) //multi plane read
					{
						ssd->m_plane_read_count++;
						sub_r_request[subs_count++] = p_sub0;
						sub_r_request[subs_count++] = p_sub1;
						Multi_Plane_Read(ssd, p_sub0, p_sub1);
						MP_flag = 1;
					}
				}
				if (MP_flag == 0)
				{
					d_sub = get_first_die_read_request(ssd, channel, chip, aim_die);

					if (d_sub != NULL)
					{
						sub_r_request[subs_count++] = d_sub;
						Read(ssd, d_sub);
					}
				}
			}
		}
	}

	if (subs_count == 0)
	{
		for (i = 0; i < max_sub_num; i++)
		{
			sub_r_request[i] = NULL;
		}
		free(sub_r_request);
		sub_r_request = NULL;
		return FAILURE;
	}

	compute_read_serve_time(ssd, channel, sub_r_request, subs_count);

	for (i = 0; i < subs_count; i++)
	{
		insert2_read_buffer(ssd, sub_r_request[i]->lpn, sub_r_request[i]->state);
	}

	//free the malloc 
	for (i = 0; i < max_sub_num; i++)
	{
		sub_r_request[i] = NULL;
	}
	free(sub_r_request);
	sub_r_request = NULL;

	return SUCCESS;
}


/****************************************
Write the request function of the request
*****************************************/
Status services_2_write(struct ssd_info * ssd, unsigned int channel)
{
	int j = 0,i = 0;
	unsigned int chip_token = 0;
	struct sub_request *sub = NULL;

	/************************************************************************************************************************
	*Because it is dynamic allocation, all write requests hanging in ssd-> subs_w_head, that is, do not know which allocation before writing on the channel
	*************************************************************************************************************************/
	if (ssd->subs_w_head != NULL || ssd->channel_head[channel].subs_w_head != NULL)
	{
		if (ssd->parameter->allocation_scheme == SUPERBLOCK_ALLOCATION)
		{
			for (j = 0; j < ssd->channel_head[channel].chip; j++)
			{
				if (ssd->channel_head[channel].subs_w_head == NULL)
					continue;

				if ((ssd->channel_head[channel].chip_head[j].current_state == CHIP_IDLE) || ((ssd->channel_head[channel].chip_head[j].next_state == CHIP_IDLE) && (ssd->channel_head[channel].chip_head[j].next_state_predict_time <= ssd->current_time)))
				{
					if (dynamic_advanced_process(ssd, channel, j) == NULL)
						ssd->channel_head[channel].channel_busy_flag = 0;
					else
						ssd->channel_head[channel].channel_busy_flag = 1;
				}
	
			}
		}
	}
	else
	{
		ssd->channel_head[channel].channel_busy_flag = 0;
	}
	return SUCCESS;
}

//get the first read request from the given plane
struct sub_request* get_first_plane_read_request(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int chan;
	struct sub_request* temp, *front;
	temp = NULL;

	temp = ssd->channel_head[channel].subs_r_head;

	while (temp != NULL)
	{
		if (temp->current_state != SR_WAIT)
		{
			temp = temp->next_node;
			continue;
		}
		if (temp->location->channel == channel && temp->location->chip == chip && temp->location->die == die && temp->location->plane == plane) //the first request allocated to the given plane
		{
			if (Is_migrated(ssd, temp) == TRUE)
			{
				front = temp;
				temp = temp->next_node;

				//delete from the channel sub request
				chan = front->location->channel;
				delete_from_channel(ssd, chan, front);
				continue;
			}

			return temp;
			break;
		}
		temp = temp->next_node;
	}
	return NULL;
}


Status Is_migrated(struct ssd_info* ssd, struct sub_request* sub)
{
	//unsigned int lun;
	unsigned int channel, chip, die, plane, block, page, unit;

	//lun = sub->lpn;
	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	plane = sub->location->plane;
	block = sub->location->block;
	page = sub->location->page;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].ref_cnt == 0)
	{
		//remark the complete state 
		sub->next_state = SR_COMPLETE;
		sub->next_state_predict_time = sub->current_time + 1000;  // hit in the buffer or sub request
		sub->complete_time = sub->next_state_predict_time;
		return TRUE;
	}
	return FALSE;
}


//get the first read request from the given die
struct sub_request* get_first_die_read_request(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die)
{
	struct sub_request *temp, *front;
	unsigned int chan;
	temp = NULL;

	temp = ssd->channel_head[channel].subs_r_head;

	while (temp != NULL)
	{
		if (temp->current_state != SR_WAIT)
		{
			temp = temp->next_node;
			continue;
		}
		if (temp->location->channel == channel && temp->location->chip == chip && temp->location->die == die) //the first request allocated to the given plane
		{
			if (Is_migrated(ssd, temp) == TRUE)
			{
				front = temp;
				temp = temp->next_node;

				//delete from the channel sub request
				chan = front->location->channel;
				delete_from_channel(ssd, chan, front);
				continue;
			}

			return temp;
			break;
		}
		temp = temp->next_node;
	}
	return NULL;
}

//get the first request from the given plane
struct sub_request* get_first_plane_write_request(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	struct sub_request* temp;
	temp = NULL;

	temp = ssd->channel_head[channel].subs_w_head;

	while (temp != NULL)
	{
		if (temp->current_state != SR_WAIT)
		{
			temp = temp->next_node;
			continue;
		}
		if (temp->location->channel == channel && temp->location->chip == chip && temp->location->die == die && temp->location->plane == plane) //the first request allocatde to the given plane
		{
			return temp;	
		}
		temp = temp->next_node;
	}
	return NULL;
}

//get the first request from the given die
struct sub_request* get_first_die_write_request(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die)
{
	struct sub_request* temp;
	temp = NULL;

	temp = ssd->channel_head[channel].subs_w_head;

	while (temp != NULL)
	{
		if(temp->current_state != SR_WAIT)
		{
			temp = temp->next_node;
			continue;
		}
		if (temp->location->channel == channel && temp->location->chip == chip && temp->location->die == die)
		{
			return temp;
		}
		temp = temp->next_node;
	}
	return NULL;
}

//judge whether reqeusts can be performed in multi-plane 
int IS_Multi_Plane(struct ssd_info* ssd, struct sub_request* sub0, struct sub_request* sub1)
{
	int flag;
	if (sub0 == NULL || sub1 == NULL)
		return 0;
	return (sub0->location->page == sub1->location->page);
}


//perform requests in multi-plane
Status Multi_Plane_Read(struct ssd_info* ssd, struct sub_request* sub0, struct sub_request* sub1)
{
	//read the data 
	if (NAND_multi_plane_read(ssd, sub0, sub1) == FAILURE)
	{
		printf("Read Failure\n");
		getchar();
		return FAILURE;
	}

	//modify the state 
	Update_read_state(ssd, sub0);
	Update_read_state(ssd, sub1);
	return SUCCESS;
}

//update the state for completed read request
Status Update_read_state(struct ssd_info* ssd, struct sub_request* sub)
{
	unsigned int channel, chip, die, plane, block, page;
	
	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	
	ssd->channel_head[channel].chip_head[chip].die_head[die].read_cnt--;
	sub->current_state = SR_R_DATA_TRANSFER;
	sub->next_state = SR_COMPLETE;
	return SUCCESS;
}


//perform requests in multi-plane
Status Multi_Plane_Write(struct ssd_info* ssd, struct sub_request* sub0, struct sub_request* sub1)
{
	//write the data 
	if (NAND_multi_plane_program(ssd, sub0, sub1) == FAILURE)
	{
		printf("Program Failure\n");
		getchar();
		return FAILURE;
	}

	//build the mapping table 
	Add_mapping_entry(ssd, sub0);
	Add_mapping_entry(ssd, sub1);
	return SUCCESS;
}

//conduct request 
Status Write(struct ssd_info* ssd, struct sub_request* sub)
{
	if (NAND_program(ssd, sub) == FAILURE)
	{
		printf("Program Failure\n");
		getchar();
		return FAILURE;
	}

	//build the mapping table 
	Add_mapping_entry(ssd, sub);
	return SUCCESS;
}

//conduct request 
Status Read(struct ssd_info* ssd, struct sub_request* sub)
{
	if (NAND_read(ssd, sub) == FAILURE)
	{
		printf("Read Failure\n");
		getchar();
		return FAILURE;
	}

	//update the read request
	Update_read_state(ssd, sub);
	return SUCCESS;
}

Status Add_mapping_entry(struct ssd_info* ssd, struct sub_request* sub)
{
	unsigned int channel, chip, die, plane, block, page;
	unsigned int lpn = -1;
	struct local* location;

	//form mapping entry
	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	plane = sub->location->plane;
	block = sub->location->block;
	page = sub->location->page;

	lpn = sub->lpn;
	if (ssd->dram->map->L2P_entry[lpn].pn != INVALID_PPN)
		Invalidate_old_lpn(ssd, lpn);

	ssd->dram->map->L2P_entry[lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
	
	return SUCCESS;
}

void Invalidate_old_lpn(struct ssd_info* ssd, unsigned int lpn)
{
	unsigned int ppn;
	unsigned int channel, chip, die, plane, block, page;
	//struct sub_request* sub_read;
	//struct request* req;
	struct local* loc;
	unsigned int status;

	ppn = ssd->dram->map->L2P_entry[lpn].pn;
	//2. invalidate the correspending physical page
	loc = find_location_ppn(ssd, ppn);
	channel = loc->channel;
	chip = loc->chip;
	die = loc->die;
	plane = loc->plane;
	block = loc->block;
	page = loc->page;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].ref_cnt = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;

	free(loc);
	loc = NULL;
}

/****************************************************************************************************************************
*When ssd supports advanced commands, the function of this function is to deal with high-level command write request
*According to the number of requests, decide which type of advanced command to choose (this function only deal with write requests, 
*read requests have been assigned to each channel, so the implementation of the election between the corresponding command)
*****************************************************************************************************************************/
struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd, unsigned int channel, unsigned int chip)
{
	unsigned int subs_count = 0;
	struct sub_request *sub = NULL;
	struct sub_request ** subs = NULL;
	struct sub_request * p_sub0, * p_sub1, * d_sub;
	unsigned int max_sub_num = 0;
	unsigned int plane0, plane1, mp_flag;
	unsigned int i = 0, j = 0, k = 0;
	unsigned int die,plane_round;
	unsigned int MP_flag;  //max 2 multiple plane operation in modern SSD

	p_sub0 = NULL;
	p_sub1 = NULL;

	max_sub_num = (ssd->parameter->die_chip)*(ssd->parameter->plane_die)*PAGE_INDEX;
	subs = (struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs, "sub_request");
	for (i = 0; i < max_sub_num; i++)
		subs[i] = NULL;  
	
	sub = ssd->channel_head[channel].subs_w_head;

	//perform requests in die-level round robin : interleave requests
	for (die = 0; die < ssd->parameter->die_chip; die++) 
	{
		MP_flag = 0;

		//perform request in multi-plane whenever possible (plane 0 & plane 1 and plane 2 & plane 3)
		for (plane_round = 0; plane_round < ssd->parameter->plane_die / 2; plane_round++)
		{
			//get the first request from each plane
			plane0 = plane_round * 2;
			plane1 = plane_round * 2 + 1;
			p_sub0 = get_first_plane_write_request(ssd, channel, chip, die, plane0);
			p_sub1 = get_first_plane_write_request(ssd, channel, chip, die, plane1);

			//judge whether sub requests can perform in multi-plane program 
			mp_flag = IS_Multi_Plane(ssd, p_sub0, p_sub1);

			if (mp_flag == 1) //multi-plane
			{
				ssd->m_plane_prog_count++;
				subs[subs_count++] = p_sub0;
				subs[subs_count++] = p_sub1;
				Multi_Plane_Write(ssd, p_sub0, p_sub1);
				MP_flag = 1;
			}			
		}
		if (MP_flag==0) // normal write
		{
			d_sub = get_first_die_write_request(ssd, channel, chip, die);
		
			if (d_sub == NULL)  // maybe p_sub0 == NULL and p_sub1 != NULL  or   p_sub1 == NULL and p_sub0 != NULL
			{
				if (p_sub0 != NULL)
					d_sub = p_sub0;
				if (p_sub1 != NULL)
					d_sub = p_sub1;
			}

			if (d_sub != NULL)
			{
				subs[subs_count++] = d_sub;
				Write(ssd, d_sub);
			}
		}
	}

	if (subs_count == 0)
	{
		for (i = 0; i < max_sub_num; i++)
		{
			subs[i] = NULL;
		}
		free(subs);
		subs = NULL;
		return ssd;
	}

	//compute write server time 
	compute_write_serve_time(ssd, channel, chip, subs, subs_count);

	for (i = 0; i < max_sub_num; i++)
	{
		subs[i] = NULL;
	}
	free(subs);
	subs = NULL;

	return ssd;
}


/****************************************************************************
*this function is to calculate the processing time and the state transition 
*of the processing when processing the write request for the advanced command
*****************************************************************************/
struct ssd_info *compute_write_serve_time(struct ssd_info *ssd, unsigned int channel, unsigned int chip, struct sub_request **subs, unsigned int subs_count)
{
	unsigned int i = 0;
	struct sub_request * last_sub = NULL;
	int prog_time = 0;
	
	for (i = 0; i < subs_count; i++)
	{
		subs[i]->current_state = SR_W_TRANSFER;
		if (last_sub == NULL)
		{
			subs[i]->current_time = ssd->current_time;
		}
		else
		{
			subs[i]->current_time = last_sub->complete_time + ssd->parameter->time_characteristics.tDBSY;
		}

		subs[i]->next_state = SR_COMPLETE;
		subs[i]->next_state_predict_time = subs[i]->current_time + 7*ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity*ssd->parameter->time_characteristics.tWC;
		subs[i]->complete_time = subs[i]->next_state_predict_time;

		if (subs[i]->complete_time < subs[i]->begin_time)
			printf("Look Here 18\n");

		last_sub = subs[i];

		delete_from_channel(ssd, channel, subs[i]);
	}

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;
	ssd->channel_head[channel].next_state_predict_time = (last_sub->complete_time>ssd->channel_head[channel].next_state_predict_time) ? last_sub->complete_time : ssd->channel_head[channel].next_state_predict_time;

	prog_time = ssd->parameter->time_characteristics.tPROGO;

	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_WRITE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
	ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + prog_time;

	for (i = 0; i < subs_count; i++)
	{
		subs[i]->next_state_predict_time = subs[i]->next_state_predict_time + prog_time;
		subs[i]->complete_time = subs[i]->next_state_predict_time;
	}
	return ssd;
}

//compute read time 
struct ssd_info* compute_read_serve_time(struct ssd_info* ssd, unsigned int channel, struct sub_request** subs, unsigned int subs_count)
{
	int i = 0;
	struct sub_request* last_sub = NULL;
	int read_time = 0;
	int chip, chip_channel = 0;
	int *chip_flag = NULL;

	chip_channel = ssd->channel_head[channel].chip;
	chip_flag = (int*)malloc(chip_channel*sizeof(int));
	alloc_assert(chip_flag, "chip_flag");
	for(i = 0; i < chip_channel; i++)
	{
		chip_flag[i] = 0;
	}

	read_time = ssd->parameter->time_characteristics.tR;

	for (i = 0; i < subs_count; i++)
	{
		chip = subs[i]->location->chip;
		if(chip_flag[chip] == 0)
		{
			ssd->channel_head[channel].chip_head[chip].current_state = CHIP_READ_BUSY;
			ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
			ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->current_time + read_time;

			chip_flag[chip] = 1;
		}

		subs[i]->current_state = SR_R_DATA_TRANSFER;
		if (last_sub == NULL)
		{
			chip = subs[i]->location->chip;
			subs[i]->current_time = ssd->channel_head[channel].chip_head[chip].next_state_predict_time;
		}
		else
		{
			subs[i]->current_time = last_sub->complete_time + ssd->parameter->time_characteristics.tDBSY;
		}
		subs[i]->next_state = SR_COMPLETE;
		subs[i]->next_state_predict_time = subs[i]->current_time + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tWC;
		subs[i]->complete_time = subs[i]->next_state_predict_time;

		last_sub = subs[i];
		delete_from_channel(ssd, channel, subs[i]);
	}

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;

	ssd->channel_head[channel].next_state_predict_time = (last_sub->complete_time > ssd->channel_head[channel].next_state_predict_time) ? last_sub->complete_time : ssd->channel_head[channel].next_state_predict_time;

	free(chip_flag);
	chip_flag = NULL;

	return ssd;
}

/*****************************************************************************************
*Function is to remove the request from ssd-> subs__head or ssd-> channel_head [channel] .subs__head
******************************************************************************************/
struct ssd_info* delete_from_channel(struct ssd_info* ssd, unsigned int channel, struct sub_request* sub_req)
{
	struct sub_request* sub = NULL, * p, * del_sub;
	struct sub_request* head, * tail;
	unsigned int op_flag;

	op_flag = sub_req->operation;

	if (op_flag == READ)
	{
		head = ssd->channel_head[channel].subs_r_head;
		tail = ssd->channel_head[channel].subs_r_tail;
	}
	else 
	{
		head = ssd->channel_head[channel].subs_w_head;
		tail = ssd->channel_head[channel].subs_w_tail;
	}
		
	sub = head;
	p = sub;
	while (sub != NULL)
	{
		if (sub == sub_req)
		{
			if (sub == head)
			{
				if (head!=tail)
				{
					if (op_flag == READ)
					{
						ssd->channel_head[channel].subs_r_head = sub->next_node;
						sub = ssd->channel_head[channel].subs_r_head;
					}
					else
					{
						ssd->channel_head[channel].subs_w_head = sub->next_node;
						sub = ssd->channel_head[channel].subs_w_head;
					}
					break;
				}
				else
				{
					if (op_flag == READ)
					{
						ssd->channel_head[channel].subs_r_head = NULL;
						ssd->channel_head[channel].subs_r_tail = NULL;
					}
					else
					{
						ssd->channel_head[channel].subs_w_head = NULL;
						ssd->channel_head[channel].subs_w_tail = NULL;
					}
					break;
				}
			}
			else
			{
				if (sub->next_node != NULL) //not tail
				{
					p->next_node = sub->next_node;
					sub = p->next_node;
					break;
				}
				else
				{
					if (op_flag == READ)
					{
						ssd->channel_head[channel].subs_r_tail = p;
						ssd->channel_head[channel].subs_r_tail->next_node = NULL;
					}
					else
					{
						ssd->channel_head[channel].subs_w_tail = p;
						ssd->channel_head[channel].subs_w_tail->next_node = NULL;
					}
					break;
				}
			}
		}
		p = sub;
		sub = sub->next_node;
	}
	return ssd;
}