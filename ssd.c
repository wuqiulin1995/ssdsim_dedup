#define _CRTDBG_MAP_ALLOC
 
#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <crtdbg.h>  
#include <assert.h>

#include "ssd.h"
#include "initialize.h"
#include "interface.h"
#include "buffer.h"
#include "ftl.h"
#include "fcl.h"
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
		/*interface layer*/
		flag = get_requests(ssd);

		/*buffer layer*/
		if (flag == 1 || (flag == 0 && ssd->request_work != NULL))
		{
			if (ssd->parameter->data_dram_capacity != 0)
			{
				if (ssd->buffer_full_flag == 0)				//buffer don't block,it can be handle.
				{
					buffer_management(ssd);
				}
			}
			else
			{
				no_buffer_distribute(ssd);
			}
			if (ssd->request_work->cmplt_flag == 1)
			{
				if (ssd->request_work != ssd->request_tail)
					ssd->request_work = ssd->request_work->next_node;
				else
					ssd->request_work = NULL;
			}

		}

		/*ftl+fcl+flash layer*/
		process(ssd);

		trace_output(ssd);

		if (flag == 0 && ssd->request_queue == NULL)
			flag = 100;
	}
	flush_sub_request(ssd);
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
		/*interface layer*/
		flag = get_requests(ssd); 

		// /*buffer layer*/
		// if (flag == 1 || (flag == 0 && ssd->request_work != NULL))
		// {   
		// 	if (ssd->parameter->data_dram_capacity !=0)
		// 	{
		// 		if (ssd->buffer_full_flag == 0)				//buffer don't block,it can be handle.
		// 		{
		// 			buffer_management(ssd);
		// 		}
		// 	} 
		// 	else
		// 	{
		// 		no_buffer_distribute(ssd);
		// 	}

		// 	if (ssd->request_work->cmplt_flag == 1)
		// 	{
		// 		if (ssd->request_work != ssd->request_tail)
		// 			ssd->request_work = ssd->request_work->next_node;
		// 		else
		// 			ssd->request_work = NULL;
		// 	}
		// }

		// /*ftl+fcl+flash layer*/
		// process(ssd); 

		handle_new_request(ssd);

		trace_output(ssd);
	
		if (flag == 0 && ssd->request_queue == NULL)
			flag = 100;
	}

	fclose(ssd->tracefile);
	return ssd;
}


/********************************************************
*the main function :Controls the state change processing 
*of the read request and the write request
*********************************************************/
struct ssd_info *process(struct ssd_info *ssd)
{
	int old_ppn = -1, flag_die = -1;
	unsigned int i,j,k,m,p,chan, random_num;
	unsigned int flag = 0, new_write = 0, chg_cur_time_flag = 1, flag2 = 0, flag_gc = 0;
	unsigned int count1;
	__int64 time, channel_time = 0x7fffffffffffffff;
	unsigned int process_cnt = 0;
#ifdef DEBUG
	printf("enter process,  current time:%I64u\n", ssd->current_time);
#endif

	/*********************************************************
	*flag=0, processing read and write sub_requests
	*flag=1, processing gc request
	**********************************************************/
	for (i = 0; i<ssd->parameter->channel_number; i++)
	{
		if ((ssd->channel_head[i].subs_r_head == NULL) && (ssd->channel_head[i].subs_w_head == NULL) && (ssd->subs_w_head == NULL))
		{
			flag = 1;
			ssd->current_time += 1000;
		}
		else
		{
			flag = 0;
			break;
		}
	}
	if (flag == 1)
	{
		ssd->flag = 1;
		return ssd;
	}
	else
	{
		ssd->flag = 0;
	}

	/*********************************************************
	*Gc operation is completed, the read and write state changes
	**********************************************************/
	time = ssd->current_time;

	for (process_cnt = 0; process_cnt < ssd->process_enhancement; process_cnt++)
	{
		for (chan = 0; chan < ssd->parameter->channel_number; chan++)
		{
			i = chan % ssd->parameter->channel_number;
			flag_gc = 0;
			ssd->channel_head[i].channel_busy_flag = 0;
			if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE && ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
			{
				if ((ssd->channel_head[i].channel_busy_flag == 0) && (ssd->channel_head[i].subs_r_head != NULL))					  //chg_cur_time_flag=1,current_time has changed��chg_cur_time_flag=0,current_time has not changed  			
				{
					//set high prority to read request
					service_2_read(ssd, i);
				}

				if (ssd->channel_head[i].channel_busy_flag == 0)
					services_2_write(ssd, i);
			}
		}
	}

	return ssd;
}

void flush_sub_request(struct ssd_info* ssd)
{
	unsigned int i;
	ssd->current_time = 99999999999999;
	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		while (ssd->channel_head[i].subs_r_head != NULL || ssd->channel_head[i].subs_w_head != NULL)					  //chg_cur_time_flag=1,current_time has changed��chg_cur_time_flag=0,current_time has not changed  			
		{
			service_2_read(ssd, i);
			services_2_write(ssd, i);
		}
	}
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
				printf("the response time is 0?? \n");
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

				if(ssd->warm_flash_cmplt == 1 && ssd->write_request_count > 1 && ssd->write_request_count % 10000 == 1)
				{
					ssd->avg_write_delay_print = (ssd->write_avg - ssd->last_write_avg) / 10000;
					ssd->last_write_avg = ssd->write_avg;

					fprintf(ssd->stat_file, "%lld, %lld\n", ssd->avg_write_delay_print, ssd->max_write_delay_print);
					fflush(ssd->stat_file);

					ssd->avg_write_delay_print = 0;
					ssd->max_write_delay_print = 0;
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
					free((void *)pre_node);
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
					free((void *)req);
					req = pre_node->next_node;
					ssd->request_queue_length--;
				}
			}
		}
		else
		{
			flag = 1;
			while (sub != NULL)
			{
				if (start_time == 0)
					start_time = sub->begin_time;
				if (start_time > sub->begin_time)
					start_time = sub->begin_time;
				if (end_time < sub->complete_time)
					end_time = sub->complete_time;
				if ((sub->current_state == SR_COMPLETE) || ((sub->next_state == SR_COMPLETE) && (sub->next_state_predict_time <= ssd->current_time)))	// if any sub-request is not completed, the request is not completed
				{
					if (sub->complete_time <= sub->begin_time)
						printf("Look Here 1\n");
					sub = sub->next_subs;
				}
				else
				{
					flag = 0;
					break;
				}
			}

			if (flag == 1)
			{
				//fprintf(ssd->outputfile, "%16I64u %10u %6u %2u %16I64u %16I64u %10I64u\n", req->time, req->lsn, req->size, req->operation, start_time, end_time, end_time - req->time);
				//fflush(ssd->outputfile);

				if (end_time - start_time <= 0)
				{
					printf("the response time is 0?? \n");
					getchar();
				}

				if (req->operation == READ)
				{
					ssd->read_request_count++;
					ssd->read_avg = ssd->read_avg + (end_time - req->time);

					// if (ssd->warm_flash_cmplt == 0) 
					// {
					// 	fprintf(ssd->outputfile, "Read begin time %16I64u  end time  %16I64u  run time  %16I64u \n", req->time, end_time, (end_time - req->time));
					// 	fflush(ssd->outputfile);
					// }

					// if (ssd->read_avg / ssd->read_request_count > 100000000000000000)
					// {
					// 	tem = req->subs;
					// 	while (tem)
					// 	{
					// 		if (ssd->warm_flash_cmplt == 0)
					// 		{
					// 			fprintf(ssd->sb_info, "end time %16I64u  ", tem->complete_time);
					// 		}
					// 		tem = tem->next_subs;
					// 	}
					// 	if (ssd->warm_flash_cmplt == 0)
					// 	{
					// 		fflush(ssd->sb_info);
					// 	}
					// 	printf("Look Here 2\n");
					// }
				}
				else
				{
					ssd->write_request_count++;
					ssd->write_avg = ssd->write_avg + (end_time - req->time);

					// if (ssd->warm_flash_cmplt == 0)
					// {
					// 	fprintf(ssd->outputfile, "Write begin time %16I64u  end time  %16I64u  run time  %16I64u \n", req->time, end_time, (end_time - req->time));
					// 	fflush(ssd->outputfile);
					// }

					// if (ssd->write_avg / ssd->write_request_count > 1000000000000000)
					// {
					// 	tem = req->subs;
					// 	while (tem)
					// 	{
					// 		if (ssd->warm_flash_cmplt == 0)
					// 		{
					// 			fprintf(ssd->sb_info, "begin time %16I64u   end time %16I64u\n", tem->begin_time, tem->complete_time);
					// 		}
					// 		tem = tem->next_subs;
					// 	}
					// 	if (ssd->warm_flash_cmplt == 0)
					// 	{
					// 		fflush(ssd->sb_info);
					// 	}
					// 	printf("Look Here 3\n");
					// }

					if(end_time - req->time > ssd->max_write_delay_print)
						ssd->max_write_delay_print = end_time - req->time;

					if(ssd->warm_flash_cmplt == 1 && ssd->write_request_count > 1 && ssd->write_request_count % 10000 == 1)
					{
						ssd->avg_write_delay_print = (ssd->write_avg - ssd->last_write_avg) / 10000;
						ssd->last_write_avg = ssd->write_avg;

						fprintf(ssd->stat_file, "%lld, %lld\n", ssd->avg_write_delay_print, ssd->max_write_delay_print);
						fflush(ssd->stat_file);

						ssd->avg_write_delay_print = 0;
						ssd->max_write_delay_print = 0;
					}
				}
				while (req->subs != NULL)
				{
					tmp = req->subs;
					req->subs = tmp->next_subs;

					free(tmp->location);
					tmp->location = NULL;
					free(tmp);
					tmp = NULL;

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
				pre_node = req;
				req = req->next_node;
			}
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

	// for(i = 0;i<ssd->parameter->channel_number;i++)
	// {
	// 	for (p = 0; p < ssd->parameter->chip_channel[i]; p++)
	// 	{
	// 		for (j = 0; j < ssd->parameter->die_chip; j++)
	// 		{
	// 			for (k = 0; k < ssd->parameter->plane_die; k++)
	// 			{
	// 				for (m = 0; m < ssd->parameter->block_plane; m++)
	// 				{
	// 					if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].erase_count > 0)
	// 					{
	// 						ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_erase_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].erase_count;
	// 					}

	// 					if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_read_count > 0)
	// 					{
	// 						ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_read_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_read_count;
	// 					}

	// 					if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_write_count > 0)
	// 					{
	// 						ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_program_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_write_count;
	// 					}

	// 					if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].pre_write_count > 0)
	// 					{
	// 						ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].pre_plane_write_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].pre_write_count;
	// 					}
	// 				}
					
	// 				fprintf(ssd->statisticfile, "the %d channel, %d chip, %d die, %d plane has : ", i, p, j, k);
	// 				fprintf(ssd->statisticfile, "%3lu erase operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_erase_count);
	// 				fprintf(ssd->statisticfile, "%3lu read operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_read_count);
	// 				fprintf(ssd->statisticfile, "%3lu write operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_program_count);
	// 				fprintf(ssd->statisticfile, "%3lu pre_process write operations\n", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].pre_plane_write_count);
	// 			}
	// 		}
	// 	}
	// }

	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"---------------------------statistic data---------------------------\n");
	fprintf(ssd->statisticfile,"min lsn: %13d\n",ssd->min_lsn);
	fprintf(ssd->statisticfile,"max lsn: %13d\n",ssd->max_lsn);
	// fprintf(ssd->statisticfile, "the request read hit count: %13lu\n", ssd->req_read_hit_cnt);
	fprintf(ssd->statisticfile, "\n");

	fprintf(ssd->statisticfile, "the write operation leaded by pre_process write count: %13lu\n", ssd->pre_all_write);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "erase count: %13lu\n",ssd->erase_count);
	fprintf(ssd->statisticfile, "direct erase count: %13lu\n",ssd->direct_erase_count);
	fprintf(ssd->statisticfile, "gc count: %13lu\n", ssd->gc_count);

	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "data read cnt: %13d\n", ssd->data_read_cnt);
	fprintf(ssd->statisticfile, "data program: %13d\n", ssd->data_program_cnt);

	// fprintf(ssd->statisticfile, "\n\n\n");
	// fprintf(ssd->statisticfile, "\nclose superblock count: %13d\n", ssd->close_superblock_cnt);
	// fprintf(ssd->statisticfile, "\nreallocate write_request count: %13d\n", ssd->reallocate_write_request_cnt);
	// fprintf(ssd->statisticfile, "\n\n\n\n");

	// fprintf(ssd->statisticfile, "multi-plane program count: %13lu\n", ssd->m_plane_prog_count);
	// fprintf(ssd->statisticfile, "multi-plane read count: %13lu\n", ssd->m_plane_read_count);
	// fprintf(ssd->statisticfile, "\n");

	// fprintf(ssd->statisticfile, "mutli plane one shot program count : %13lu\n", ssd->mutliplane_oneshot_prog_count);
	// fprintf(ssd->statisticfile, "one shot program count : %13lu\n", ssd->ontshot_prog_count);
	// fprintf(ssd->statisticfile, "\n");

	// fprintf(ssd->statisticfile, "half page read count : %13lu\n", ssd->half_page_read_count);
	// fprintf(ssd->statisticfile, "one shot read count : %13lu\n", ssd->one_shot_read_count);
	// fprintf(ssd->statisticfile, "mutli plane one shot read count : %13lu\n", ssd->one_shot_mutli_plane_count);
	// fprintf(ssd->statisticfile, "\n");

	// fprintf(ssd->statisticfile, "erase suspend count : %13lu\n", ssd->suspend_count);
	// fprintf(ssd->statisticfile, "erase resume  count : %13lu\n", ssd->resume_count);
	// fprintf(ssd->statisticfile, "suspend read  count : %13lu\n", ssd->suspend_read_count);
	// fprintf(ssd->statisticfile, "\n");

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
	// fprintf(ssd->statisticfile,"buffer read hits: %13lu\n",ssd->dram->data_buffer->read_hit);
	// fprintf(ssd->statisticfile,"buffer read miss: %13lu\n",ssd->dram->data_buffer->read_miss_hit);
	// fprintf(ssd->statisticfile,"buffer write hits: %13lu\n",ssd->dram->data_buffer->write_hit);
	// fprintf(ssd->statisticfile,"buffer write miss: %13lu\n",ssd->dram->data_buffer->write_miss_hit);
	
	// fprintf(ssd->statisticfile, "half page read count : %13lu\n", ssd->half_page_read_count);
	// fprintf(ssd->statisticfile, "mutli plane one shot program count : %13lu\n", ssd->mutliplane_oneshot_prog_count);
	// fprintf(ssd->statisticfile, "one shot read count : %13lu\n", ssd->one_shot_read_count);
	// fprintf(ssd->statisticfile, "mutli plane one shot read count : %13lu\n", ssd->one_shot_mutli_plane_count);
	// fprintf(ssd->statisticfile, "erase suspend count : %13lu\n", ssd->suspend_count);
	// fprintf(ssd->statisticfile, "erase resume  count : %13lu\n", ssd->resume_count);
	// fprintf(ssd->statisticfile, "suspend read  count : %13lu\n", ssd->suspend_read_count);

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
	struct direct_erase * erase_node=NULL;

//	struct gc_operation *gc_node = NULL;

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
					while (ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node != NULL)
					{
						erase_node = ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node;
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node = erase_node->next_node;
						assert(erase_node);
						free(erase_node);
						erase_node = NULL;
					}
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