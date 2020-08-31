#define _CRTDBG_MAP_ALLOC
 
#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <crtdbg.h>  
#include <assert.h>

#include "initialize.h"
#include "interface.h"
#include "ssd.h"
#include "buffer.h"
#include "ftl.h"
#include "flash.h"

int secno_num_per_page, secno_num_sub_page;

char* parameters_file =  "page64GB.parameters";
char* trace_file = "homes.ascii";
char* warm_trace_file = "homes.ascii";
char* result_file_statistic = "statistic_file.txt";
char* result_file_ex =  "output_file.txt";

int main()
{
	struct ssd_info *ssd;
	
	ssd = (struct ssd_info*)malloc(sizeof(struct ssd_info));
	alloc_assert(ssd, "ssd");
	memset(ssd, 0, sizeof(struct ssd_info));
	
	strcpy_s(ssd->parameterfilename, 50, parameters_file);
	strcpy_s(ssd->tracefilename, 50, trace_file);
	strcpy_s(ssd->outputfilename, 50, result_file_ex);
	strcpy_s(ssd->statisticfilename, 50, result_file_statistic);

	printf("tracefile:%s begin simulate-------------------------\n", ssd->tracefilename);
	tracefile_sim(ssd);
	printf("tracefile:%s end simulate---------------------------\n\n\n", ssd->tracefilename);
	//getchar();

    // free_all_node(ssd);
	//_CrtDumpMemoryLeaks();  //Memory leak detection

	return 0;
}

void tracefile_sim(struct ssd_info *ssd)
{
	#ifdef DEBUG
	printf("enter main\n"); 
	#endif
	ssd = initiation(ssd);

	ssd->warm_flash_cmplt = 0;
	ssd->total_gc_count = 0;

	while(ssd->total_gc_count < 1)
	{
		strcpy_s(ssd->tracefilename, 50, warm_trace_file);
		warm_flash(ssd);	
		reset(ssd);
	}

	ssd->warm_flash_cmplt = 1;

	strcpy_s(ssd->tracefilename, 50, trace_file);
	ssd=simulate(ssd);

	statistic_output(ssd);  
	printf("\n the simulation is completed!\n");
}

void reset(struct ssd_info *ssd)
{
	unsigned int i, j;
	initialize_statistic(ssd);

	ssd->nvram_log->next_avail_time = 0;

	//reset the time 
	ssd->current_time = 0;
	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		ssd->channel_head[i].channel_busy_flag = 0;
		ssd->channel_head[i].current_time = 0;
		ssd->channel_head[i].current_state = CHANNEL_IDLE;
		ssd->channel_head[i].next_state_predict_time = 0;

		for (j = 0; j < ssd->channel_head[i].chip; j++)
		{
			ssd->channel_head[i].chip_head[j].current_state = CHIP_IDLE;
			ssd->channel_head[i].chip_head[j].current_time = 0;
			ssd->channel_head[i].chip_head[j].next_state_predict_time = 0;
		}
	}
	ssd->trace_over_flag = 0;
}

struct ssd_info *warm_flash(struct ssd_info *ssd)
{
	int flag = 1;
	errno_t err;

	printf("\n");
	printf("begin warm flash.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");

	if ((err = fopen_s(&(ssd->tracefile), ssd->tracefilename, "rb")) != 0)
	{
		printf("the trace file can't open\n");
		return NULL;
	}

	while (flag != 100)
	{
		flag = get_requests(ssd); 

		if (flag == 1 || (flag == 0 && ssd->request_work != NULL))
		{
			handle_new_request(ssd);

			if (ssd->request_work->cmplt_flag == 1)
			{
				if (ssd->request_work != ssd->request_tail)
					ssd->request_work = ssd->request_work->next_node;
				else
					ssd->request_work = NULL;
			}	
		}

		trace_output(ssd);

		if (flag == 0 && ssd->request_queue == NULL)
			flag = 100;
	}
	fclose(ssd->tracefile);
	return ssd;
}

/******************simulate() *********************************************************************
*Simulation () is the core processing function, the main implementation of the features include:
*1, get_requests() :Get a request from the trace file and hang to ssd-> request
*2��buffer_management()/distribute()/no_buffer_distribute() :Make read and write requests through 
the buff layer processing, resulting in read and write sub_requests,linked to ssd-> channel or ssd

*3��process() :Follow these events to handle these read and write sub_requests
*4��trace_output() :Process the completed request, and count the simulation results
**************************************************************************************************/
struct ssd_info *simulate(struct ssd_info *ssd)
{
	int flag=1;
	errno_t err;

	printf("\n");
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");

	if((err=fopen_s(&(ssd->tracefile),ssd->tracefilename,"rb"))!=0)
	{  
		printf("the trace file can't open\n");
		return NULL;
	}

	while(flag!=100)      
	{
		flag = get_requests(ssd); 

		if (flag == 1 || (flag == 0 && ssd->request_work != NULL))
		{
			handle_new_request(ssd);

			if (ssd->request_work->cmplt_flag == 1)
			{
				if (ssd->request_work != ssd->request_tail)
					ssd->request_work = ssd->request_work->next_node;
				else
					ssd->request_work = NULL;
			}	
		}

		trace_output(ssd);
	
		if (flag == 0 && ssd->request_queue == NULL)
			flag = 100;
	}

	fclose(ssd->tracefile);
	return ssd;
}

/**********************************************************************
*The trace_output () is executed after all the sub requests of each request 
*are processed by the process () 
*Print the output of the relevant results to the outputfile file
**********************************************************************/
void trace_output(struct ssd_info *ssd)
{
	int flag = 1;
	__int64 start_time, end_time;
	struct request *req, *pre_node;
	struct sub_request *sub, *tmp,*tmp_update;
	unsigned int chan, chip;
	struct sub_request *tem;

#ifdef DEBUG
	printf("enter trace_output,  current time:%I64u\n", ssd->current_time);
#endif

	pre_node = NULL;
	req = ssd->request_queue;
	start_time = 0;
	end_time = 0;

	if (req == NULL)
		return;
	while (req != NULL)
	{
		sub = req->subs;
		flag = 1;
		start_time = 0;
		end_time = 0;
		if (req->response_time != 0)
		{
			//fprintf(ssd->outputfile, "%16I64u %10u %6u %2u %16I64u %16I64u %10I64u\n", req->time, req->lsn, req->size, req->operation, req->begin_time, req->response_time, req->response_time - req->time);
			//fflush(ssd->outputfile);

			if (req->response_time - req->time == 0)
			{
				printf("the request simulation time is 0?? \n");
			}

			if (req->operation == READ)
			{
				ssd->read_request_count++;
				ssd->read_avg = ssd->read_avg + (req->response_time - req->time);
			}
			else
			{
				ssd->write_request_count++;
				ssd->write_avg = ssd->write_avg + (req->response_time - req->time);

				if(req->response_time - req->time > ssd->max_write_delay_print)
					ssd->max_write_delay_print = req->response_time - req->time;

				if(ssd->warm_flash_cmplt == 1 && ssd->write_request_count > 1 && ssd->write_request_count % 50000 == 1)
				{
					ssd->avg_write_delay_print = (ssd->write_avg - ssd->last_write_avg) / 50000;
					ssd->last_write_avg = ssd->write_avg;

					fprintf(ssd->stat_file, "%lu, %u, %u, %lld, %lld, %u, %lld, %u, %lld\n", 
											ssd->write_request_count, ssd->total_oob_entry, ssd->invalid_oob_entry,
											ssd->avg_write_delay_print, ssd->max_write_delay_print, 
											ssd->nvram_gc_print, ssd->avg_nvram_gc_delay, 
											ssd->gcr_nvram_print, ssd->avg_gcr_nvram_delay);
					fflush(ssd->stat_file);

					ssd->avg_write_delay_print = 0;
					ssd->max_write_delay_print = 0;

					ssd->nvram_gc_print = 0;
					ssd->nvram_gc_delay_print = 0;
					ssd->avg_nvram_gc_delay = 0;

					ssd->gcr_nvram_print = 0;
					ssd->gcr_nvram_delay_print = 0;
					ssd->avg_gcr_nvram_delay = 0;
				}
			}

			if (pre_node == NULL)
			{
				if (req->next_node == NULL)
				{
					free(req);
					req = NULL;
					ssd->request_queue = NULL;
					ssd->request_tail = NULL;
					ssd->request_queue_length--;
				}
				else
				{
					ssd->request_queue = req->next_node;
					pre_node = req;
					req = req->next_node;
					free(pre_node);
					pre_node = NULL;
					ssd->request_queue_length--;
				}
			}
			else
			{
				if (req->next_node == NULL)
				{
					pre_node->next_node = NULL;
					free(req);
					req = NULL;
					ssd->request_tail = pre_node;
					ssd->request_queue_length--;
				}
				else
				{
					pre_node->next_node = req->next_node;
					free(req);
					req = pre_node->next_node;
					ssd->request_queue_length--;
				}
			}
		}
		else
		{		
			printf("the request response time is 0?? \n");
			getchar();
		}
	}
}

/*******************************************************************************
*statistic_output() output processing of a request after the relevant processing information
*1��Calculate the number of erasures per plane, ie plane_erase and the total number of erasures
*2��Print min_lsn, max_lsn, read_count, program_count and other statistics to the file outputfile.
*3��Print the same information into the file statisticfile
*******************************************************************************/
void statistic_output(struct ssd_info *ssd)
{
	unsigned int i,j,k,m,p;

#ifdef DEBUG
	printf("enter statistic_output,  current time:%I64u\n",ssd->current_time);
#endif

	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"---------------------------statistic data---------------------------\n");
	fprintf(ssd->statisticfile,"min lsn: %13d\n",ssd->min_lsn);
	fprintf(ssd->statisticfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->statisticfile, "\n");

	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "erase count: %13lu\n",ssd->erase_count);
	fprintf(ssd->statisticfile, "gc count: %13lu\n", ssd->gc_count);

	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "data read cnt: %13d\n", ssd->data_read_cnt);
	fprintf(ssd->statisticfile, "data program: %13d\n", ssd->data_program_cnt);

	fprintf(ssd->statisticfile, "\n");
	
	fprintf(ssd->statisticfile,"read request count: %13lu\n",ssd->read_request_count);
	fprintf(ssd->statisticfile,"write request count: %13lu\n",ssd->write_request_count);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->statisticfile,"write request average size: %13f\n",ssd->ave_write_size);
	fprintf(ssd->statisticfile, "\n");
	if (ssd->read_request_count != 0)
		fprintf(ssd->statisticfile, "read request average response time: %16I64u\n", ssd->read_avg / ssd->read_request_count);
	if (ssd->write_request_count != 0)
		fprintf(ssd->statisticfile, "write request average response time: %16I64u\n", ssd->write_avg / ssd->write_request_count);
	fprintf(ssd->statisticfile, "\n");

	fflush(ssd->statisticfile);

	fclose(ssd->outputfile);
	fclose(ssd->statisticfile);
}


/***********************************************
*free_all_node(): release all applied nodes
************************************************/
void free_all_node(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,n,p;
	struct buffer_group *pt=NULL;

	avlTreeDestroy( ssd->dram->data_buffer);
	ssd->dram->data_buffer =NULL;
	avlTreeDestroy(ssd->dram->read_data_buffer);
	ssd->dram->read_data_buffer = NULL;
	avlTreeDestroy(ssd->dram->data_command_buffer);
	ssd->dram->data_command_buffer = NULL;

	free(ssd->dram->map->L2P_entry);
	ssd->dram->map->L2P_entry = NULL;
	free(ssd->dram->map);
	ssd->dram->map = NULL;
	free(ssd->dram);
	ssd->dram=NULL;

	for (p = 0; p < ssd->parameter->block_plane; p++)
	{
		free(ssd->sb_pool[p].pos);
	}
	free(ssd->sb_pool);
	ssd->sb_pool = NULL;

	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		for (j = 0; j < ssd->parameter->chip_channel[i]; j++)
		{
			for (k = 0; k < ssd->parameter->die_chip; k++)
			{
				for (l = 0; l < ssd->parameter->plane_die; l++)
				{
					for (n = 0; n < ssd->parameter->block_plane; n++)
					{
						assert(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
						free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head = NULL;
					}
					assert(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
					free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head = NULL;
				}
				assert(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
				free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
				ssd->channel_head[i].chip_head[j].die_head[k].plane_head = NULL;
			}
			assert(ssd->channel_head[i].chip_head[j].die_head);
			free(ssd->channel_head[i].chip_head[j].die_head);
			ssd->channel_head[i].chip_head[j].die_head = NULL;
		}
		assert(ssd->channel_head[i].chip_head);
		free(ssd->channel_head[i].chip_head);
		ssd->channel_head[i].chip_head = NULL;
	}
	assert(ssd->channel_head);
	free(ssd->channel_head);
	ssd->channel_head = NULL;
	free(ssd->parameter);
	ssd->parameter = NULL;
	free(ssd);
	ssd=NULL;
}

/*****************************************************
*Assert,malloc failed��printf��malloc ... error��
******************************************************/
void alloc_assert(void *p, char *s)
{
	if (p != NULL) return;
	printf("malloc %s error\n", s);
	getchar();
	exit(-1);
}