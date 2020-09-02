#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "ssd.h"
#include "initialize.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"

extern int secno_num_per_page, secno_num_sub_page;
/********    get_request    ******************************************************
*	1.get requests that arrived already
*	2.add those request node to ssd->reuqest_queue
*	return	0: reach the end of the trace
*			-1: no request has been added
*			1: add one request to list
********************************************************************************/
int get_requests(struct ssd_info *ssd)
{
	char buffer[200];
	unsigned int lsn = 0, large_lsn = 0, max_lsn = 0, fing = 0;
	int size = 0, ope = -1, i = 0, j = 0;
	struct request *request1;
	int flag = 1;
	long filepoint;
	__int64 time_t;
	__int64 nearest_event_time;

#ifdef DEBUG
	printf("enter get_requests,  current time:%I64u\n", ssd->current_time);
#endif

	if (ssd->trace_over_flag == 1)
	{
		nearest_event_time = find_nearest_event(ssd);
		if (nearest_event_time != 0x7fffffffffffffff)
			ssd->current_time = (nearest_event_time> ssd->current_time)? nearest_event_time: ssd->current_time;
		else
			ssd->current_time += 5000000;
		return 0;
	}

	large_lsn = (int)((secno_num_per_page * ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip * ssd->parameter->chip_num) / (1 + ssd->parameter->overprovide));
	max_lsn = (int)(large_lsn * 1);

	while (TRUE)
	{
		filepoint = ftell(ssd->tracefile);
		fgets(buffer, 200, ssd->tracefile);
		sscanf(buffer, "%I64u %d %d %d %d", &time_t, &lsn, &size, &ope, &fing);
		
		//time_t = time_t / 10;

		if (feof(ssd->tracefile))      //if the end of trace
			break;
		
		if (size != 8)
		{
			printf("ERROR: request size is not 4KB!!!\n");
			continue;
		}

		if (lsn > max_lsn)
		{
			lsn = lsn % max_lsn;
		}

		if (ssd->warm_flash_cmplt == 0 && ope == READ)
			ope = WRITE;

		// if (ssd->warm_flash_cmplt == 1 && ope == READ)
		// 	continue;

		// if (ssd->request_lz_count > 1000000)
		// 	ssd->trace_over_flag = 1;

		if (ssd->parameter->data_dram_capacity == 0)
			break;
		if (size < (ssd->parameter->data_dram_capacity / SECTOR))
			break;
	}

	if ((lsn < 0) && (size < 0) && (ope < 0) && (fing < 1))
	{
		return 100;
	}

	if (lsn<ssd->min_lsn)
		ssd->min_lsn = lsn;
	if (lsn>ssd->max_lsn)
		ssd->max_lsn = lsn;

	nearest_event_time = find_nearest_event(ssd);  //Find the time of the latest event

	/**********************************************************************
	*nearest_event_time = 0x7fffffffffffffff,the channel and chip is idle
	*current_time should be update to trace time
	***********************************************************************/
	if (nearest_event_time == 0x7fffffffffffffff)
	{
		ssd->current_time = (time_t > ssd->current_time) ? time_t : ssd->current_time;
		if (ssd->buffer_full_flag == 1)			   
		{
			fseek(ssd->tracefile, filepoint, 0);
			return -1;
		}
		else if (ssd->request_queue_length >= ssd->parameter->queue_length)  //request queue is full, request should be block
		{
			fseek(ssd->tracefile, filepoint, 0);
			return 0;
		}
		
	}
	else
	{
		if (ssd->buffer_full_flag == 1)			 
		{
			fseek(ssd->tracefile, filepoint, 0);
			ssd->current_time = (nearest_event_time > ssd->current_time) ? nearest_event_time : ssd->current_time;
			return -1;
		}
		
		/**********************************************************************
		*nearest_event_time < time_t, flash has not yet finished,request should be block
		*current_time should be update to nearest_event_time
		***********************************************************************/
		if (nearest_event_time<time_t)
		{
			fseek(ssd->tracefile, filepoint, 0);
			if (ssd->current_time <= nearest_event_time)
				ssd->current_time = nearest_event_time;
			return -1;
		}
		else
		{
			/**********************************************************************
			*nearest_event_time > time_t, reqeuet should be read in and process
			*current_time should be update to trace time
			***********************************************************************/
			if (ssd->request_queue_length >= ssd->parameter->queue_length)
			{
				fseek(ssd->tracefile, filepoint, 0);
				ssd->current_time = (nearest_event_time > ssd->current_time) ? nearest_event_time : ssd->current_time;
				return -1;
			}
			else
			{
				ssd->current_time = (time_t > ssd->current_time) ? time_t : ssd->current_time;
			}
		}
	}

	if (time_t < 0)
	{
		printf("error!\n");
		while (1){}
	}


	if (feof(ssd->tracefile))    
	{
		request1 = NULL;
		ssd->trace_over_flag = 1;
		return 0;
	}

	request1 = (struct request*)malloc(sizeof(struct request));
	alloc_assert(request1, "request");
	memset(request1, 0, sizeof(struct request));

	request1->time = time_t;
	request1->lsn = lsn;
	request1->size = size;
	request1->fing = fing;
	request1->operation = ope;
	request1->begin_time = time_t;

	request1->response_time = 0;
	request1->next_node = NULL;
	request1->subs = NULL;
	filepoint = ftell(ssd->tracefile);		// set the file point

	if (ssd->request_queue == NULL)          //The queue is empty
	{
		ssd->request_queue = request1;
		ssd->request_tail = request1;
		ssd->request_work = request1;
		ssd->request_queue_length++;
	}
	else
	{
		ssd->request_tail->next_node = request1;
		ssd->request_tail = request1;
		if (ssd->request_work == NULL)
			ssd->request_work = request1;
		ssd->request_queue_length++;
	}

	ssd->request_lz_count++;

	if (ssd->request_lz_count % 200000 == 0)
		printf("request:%lu\n", ssd->request_lz_count);

	if (request1->operation == READ)             //Calculate the average request size ,1 for read 0 for write
	{
		ssd->ave_read_size = (ssd->ave_read_size*ssd->read_request_count + request1->size) / (ssd->read_request_count + 1);
	}
	else
	{
		ssd->ave_write_size = (ssd->ave_write_size*ssd->write_request_count + request1->size) / (ssd->write_request_count + 1);
	}

	filepoint = ftell(ssd->tracefile);
	fgets(buffer, 200, ssd->tracefile);    //find the arrival time of the next request
	sscanf(buffer, "%I64u %d %d %d %d", &time_t, &lsn, &size, &ope, &fing);
	ssd->next_request_time = time_t;
	fseek(ssd->tracefile, filepoint, 0);

	return 1;
}


/**********************************************************************************************************
*Find all the sub-requests for the earliest arrival of the next state of the time
*1.if next_state_predict_time <= current_time, sub request is block.
*2.if next_state_predict_time > current_time,update the nearest time of next_state_predict_time
*traverse all the channels and chips...
***********************************************************************************************************/
__int64 find_nearest_event(struct ssd_info *ssd)
{
	unsigned int i, j;
	__int64 time = 0x7fffffffffffffff;
	__int64 time1 = 0x7fffffffffffffff;
	__int64 time2 = 0x7fffffffffffffff;

	for (i = 0; i<ssd->parameter->channel_number; i++)
	{
		if (ssd->channel_head[i].next_state == CHANNEL_IDLE)
			if (time1>ssd->channel_head[i].next_state_predict_time)
				if (ssd->channel_head[i].next_state_predict_time>ssd->current_time)
					time1 = ssd->channel_head[i].next_state_predict_time;
		for (j = 0; j<ssd->parameter->chip_channel[i]; j++)
		{
			if ((ssd->channel_head[i].chip_head[j].next_state == CHIP_IDLE) || (ssd->channel_head[i].chip_head[j].next_state == CHIP_DATA_TRANSFER))
				if (time2>ssd->channel_head[i].chip_head[j].next_state_predict_time)
					if (ssd->channel_head[i].chip_head[j].next_state_predict_time>ssd->current_time)
						time2 = ssd->channel_head[i].chip_head[j].next_state_predict_time;
		}
	}

	/*****************************************************************************************************
	*time return: A.next state is CHANNEL_IDLE and next_state_predict_time> ssd->current_time
	*			  B.next state is CHIP_IDLE and next_state_predict_time> ssd->current_time
	*			  C.next state is CHIP_DATA_TRANSFER and next_state_predict_time> ssd->current_time
	*A/B/C all not meet��return 0x7fffffffffffffff,means channel and chip is idle
	*****************************************************************************************************/
	time = (time1>time2) ? time2 : time1;
	return time;
}