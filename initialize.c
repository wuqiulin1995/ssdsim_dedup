#define _CRTDBG_MAP_ALLOC
 
#include <stdlib.h>
#include <crtdbg.h>
#include "initialize.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"
#include "lsm_Tree.h"

#define FALSE		0
#define TRUE		1

#define ACTIVE_FIXED 0
#define ACTIVE_ADJUST 1

extern int secno_num_per_page, secno_num_sub_page;
extern double cache_size_ts[10] = { 0.001, 0.01, 0.02,0.03, 0.04, 0.05};

/************************************************************************
* Compare function for AVL Tree                                        
************************************************************************/
extern int keyCompareFunc(TREE_NODE *p , TREE_NODE *p1)
{
	struct buffer_group *T1=NULL,*T2=NULL;

	T1=(struct buffer_group*)p;
	T2=(struct buffer_group*)p1;


	if(T1->group< T2->group) return 1;
	if(T1->group> T2->group) return -1;

	return 0;
}


extern int freeFunc(TREE_NODE *pNode)
{
	
	if(pNode!=NULL)
	{
		free((void *)pNode);
	}
	
	
	pNode=NULL;
	return 1;
}


/**********   initiation   ******************
*initialize the ssd struct to simulate the ssd hardware
*1.this function allocate memory for ssd structure 
*2.set the infomation according to the parameter file
*******************************************/
struct ssd_info *initiation(struct ssd_info *ssd)
{
	unsigned int x=0,y=0,i=0,j=0,k=0,l=0,m=0,n=0;
	errno_t err;
	char buffer[300];
	struct parameter_value *parameters;
	FILE *fp=NULL;
	
	//Import the configuration file for ssd
	parameters=load_parameters(ssd->parameterfilename);
	ssd->parameter=parameters;
	ssd->min_lsn=0x7fffffff; //找到trace里面一个比较小的逻辑地址值
	ssd->page=ssd->parameter->chip_num*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block;
	ssd->parameter->update_reqeust_max = (ssd->parameter->data_dram_capacity / ssd->parameter->page_capacity) / INDEX;
	secno_num_per_page = ssd->parameter->page_capacity / SECTOR;  //没用
	secno_num_sub_page = ssd->parameter->subpage_capacity / SECTOR; 

	//Initialize the statistical parameters
	initialize_statistic(ssd);

	ssd->debug_cnt = 0;

	//Initialize channel_info
	ssd->channel_head=(struct channel_info*)malloc(ssd->parameter->channel_number * sizeof(struct channel_info));
	alloc_assert(ssd->channel_head,"ssd->channel_head");
	memset(ssd->channel_head,0,ssd->parameter->channel_number * sizeof(struct channel_info));
	initialize_channels(ssd );

   //initialize the superblock info 
	intialize_sb(ssd);

	ssd->sb_info = fopen("superblock_info.txt", "w");
	ssd->read_req = fopen("read requests.txt", "w");
	ssd->write_req = fopen("write requests.txt","w");
	ssd->smt_info = fopen("smt informnation.txt","w");
	ssd->flash_info = fopen("flash write informnation.txt", "w");
	ssd->allocation_info = fopen("allocation informnation.txt", "w");
	ssd->buffer_info = fopen("buffer infomation.txt","w");

	//ssd->die_read_req = fopen("Read_Request_Count_Per_Die.txt", "w");
	//show sb info 
	//show_sb_info(ssd);;

	//Initialize dram_info
	ssd->dram = (struct dram_info *)malloc(sizeof(struct dram_info));
	alloc_assert(ssd->dram, "ssd->dram");
	memset(ssd->dram, 0, sizeof(struct dram_info));
	initialize_dram(ssd);

	if ((err = fopen_s(&ssd->outputfile,ssd->outputfilename,"w")) != 0)
	{
		printf("the output file can't open\n");
		return NULL;
	}

	if((err=fopen_s(&ssd->statisticfile,ssd->statisticfilename,"w"))!= 0)
	{
		printf("the statistic file can't open\n");
		return NULL;
	}

	if ((err = fopen_s(&ssd->read_distribution,ssd->read_disturb_filename,"w")) != 0)
	{
		printf("the statistic file of read_disturb can't open\n");
		return NULL;
	}

	fprintf(ssd->outputfile,"parameter file: %s\n",ssd->parameterfilename); 
	fprintf(ssd->outputfile,"trace file: %s\n",ssd->tracefilename);
	fprintf(ssd->statisticfile,"parameter file: %s\n",ssd->parameterfilename); 
	fprintf(ssd->statisticfile,"trace file: %s\n",ssd->tracefilename);
	fprintf(ssd->read_distribution,"parameter file: %s\n", ssd->parameterfilename);
	fprintf(ssd->read_distribution,"trace file: %s\n", ssd->tracefilename);

	fflush(ssd->outputfile);
	fflush(ssd->statisticfile);
	fflush(ssd->read_distribution);

	if((err=fopen_s(&fp,ssd->parameterfilename,"r"))!=0)
	{
		printf("\nthe parameter file can't open!\n");
		return NULL;
	}

	//fp=fopen(ssd->parameterfilename,"r");
	fprintf(ssd->outputfile,"\n-----------------------parameter file----------------------\n");
	fprintf(ssd->statisticfile,"\n-----------------------parameter file----------------------\n");
	fprintf(ssd->read_distribution,"\n-----------------------parameter file----------------------\n");

	while(fgets(buffer,300,fp))
	{
		fprintf(ssd->outputfile,"%s",buffer);
		fflush(ssd->outputfile);
		fprintf(ssd->statisticfile,"%s",buffer);
		fflush(ssd->statisticfile);
		fprintf(ssd->read_distribution,"%s", buffer);
		fflush(ssd->read_distribution);
	}

	fprintf(ssd->outputfile,"\n\n-----------------------simulation output-----------------------\n");
	fflush(ssd->outputfile);

	fprintf(ssd->statisticfile,"\n\n-----------------------simulation output----------------------\n");
	fflush(ssd->statisticfile);

	fprintf(ssd->read_distribution,"\n\n-----------------------simulation output----------------------\n");
	fflush(ssd->read_distribution);

	fclose(fp);
	printf("\n initiation is completed!\n");
    
	ssd->debug_smt = NULL;
	ssd->debug_sub = NULL;
	ssd->process_enhancement = 3;
	return ssd;
}

void initialize_statistic(struct ssd_info * ssd)
{
	ssd->read_count = 0;
	ssd->update_read_count = 0;
	ssd->req_read_count = 0;
	ssd->gc_read_count = 0;
	ssd->gc_read_hit_cnt = 0;
	ssd->req_read_hit_cnt = 0;
	ssd->update_read_hit_cnt = 0;

	ssd->program_count = 0;
	ssd->pre_all_write = 0;
	ssd->update_write_count = 0;
	ssd->gc_write_count = 0;
	ssd->erase_count = 0;
	ssd->direct_erase_count = 0;
	ssd->m_plane_read_count = 0;
	ssd->read_request_count = 0;
	ssd->write_flash_count = 0;
	ssd->write_request_count = 0;
	ssd->read_request_count = 0;
	ssd->ave_read_size = 0.0;
	ssd->ave_write_size = 0.0;
	ssd->gc_count = 0;
	ssd->mplane_erase_count = 0;

	//Initializes the global variable for ssd_info
	ssd->make_age_free_page = 0;
	ssd->buffer_full_flag = 0;
	ssd->request_lz_count = 0;
	ssd->resume_count = 0;
	ssd->plane_count = 0;
	ssd->read_avg = 0;
	ssd->write_avg = 0;
	ssd->write_request_count = 0;
	ssd->read_request_count = 0;
	ssd->m_plane_prog_count = 0;
	ssd->mutliplane_oneshot_prog_count = 0;
	ssd->one_shot_read_count = 0;
	ssd->read_tran_cache_hit = 0;
	ssd->read_tran_cache_miss = 0;
	ssd->write_tran_cache_hit = 0;
	ssd->write_tran_cache_miss = 0;
	ssd->smt_invalid_count = 0;
	ssd->tran_miss_wb = 0;
	ssd->lun_count = 0;
	ssd->data_read_cnt = 0;
	ssd->tran_read_cnt = 0;
	ssd->data_update_cnt = 0;
	ssd->tran_update_cnt = 0; 
	ssd->data_read_cnt = 0;
	ssd->tran_read_cnt = 0;
	ssd->data_program_cnt = 0;
	ssd->gc_data_program_cnt = 0;
	ssd->fresh_data_program_cnt = 0;
	ssd->fresh_block_cnt = 0;
	ssd->close_superblock_cnt = 0;
	ssd->reallocate_write_request_cnt = 0;
	ssd->tran_program_cnt = 0;
	ssd->gc_tran_program_cnt = 0;
}


struct dram_info * initialize_dram(struct ssd_info * ssd)
{
	unsigned int page_num,tran_page_num, sub_page_num;
	unsigned int i;
	unsigned int chunk_num;
	unsigned int sp_capacity;   // the capacity of superpage
	unsigned int max_para;  //sum plane count 

    //data buffer 
	struct dram_info *dram=ssd->dram;
	dram->data_buffer_capacity = ssd->parameter->data_dram_capacity;
	dram->mapping_buffer_capacity = ssd->parameter->mapping_dram_capacity;
	dram->read_data_buffer_capacity = ssd->parameter->read_dram_capacity;

	//data cache
	dram->data_buffer = (tAVLTree *)avlTreeCreate((void*)keyCompareFunc , (void *)freeFunc);
	dram->data_buffer->max_buffer_sector = (dram->data_buffer_capacity / SECTOR) - (ssd->sb_pool[0].blk_cnt * ssd->parameter->subpage_page * ssd->parameter->subpage_capacity / SECTOR);  // not uncluding command buffer ; unit is 512B = sector 
	dram->read_data_buffer = (tAVLTree*)avlTreeCreate((void*)keyCompareFunc, (void*)freeFunc);
	dram->read_data_buffer->max_buffer_sector = (dram->read_data_buffer_capacity / SECTOR);

	//mapping  cache 
	dram->mapping_buffer = (tAVLTree*)avlTreeCreate((void*)keyCompareFunc, (void*)freeFunc);
	max_para = ssd->parameter->plane_die * ssd->parameter->die_chip * ssd->parameter->chip_num;
	sp_capacity = max_para * ssd->parameter->page_capacity;
	switch (FTL)
	{
	case DFTL:
		if (DFTL)
		{
			dram->mapping_buffer->max_buffer_B = (dram->mapping_buffer_capacity- sp_capacity) / B;  // not uncluding command buffer ; unit is B
			dram->mapping_node_count = 0;
		}
		else  //fully cached mapping entries 
		{
			dram->mapping_buffer->max_buffer_B = 99999999;  // not uncluding command buffer ; unit is B
			dram->mapping_node_count = 0;
		}

		//Mapping Table: LPN -> PPN
		page_num = (ssd->parameter->page_block * ssd->parameter->block_plane * max_para) / (1 + ssd->parameter->overprovide);
		sub_page_num = page_num * ssd->parameter->subpage_page;
		dram->map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->map, "dram->map");
		memset(dram->map, 0, sizeof(struct map_info));
		dram->map->map_entry = (struct entry*)malloc(sizeof(struct entry) * sub_page_num);
		alloc_assert(dram->map->map_entry, "dram->map->map_entry");
		memset(dram->map->map_entry, 0, sizeof(struct entry) * sub_page_num);

		//Global Translation Table: VPN -> PPN
		ssd->map_entry_per_subpage = ssd->parameter->subpage_capacity / (ssd->parameter->mapping_entry_size / 2);
		dram->tran_map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->tran_map, "dram->tran_map");
		memset(dram->tran_map, 0, sizeof(struct map_info));
		tran_page_num = sub_page_num / ssd->map_entry_per_subpage;  //for DFTL
		dram->tran_map->map_entry = (struct entry*)malloc(sizeof(struct entry) * tran_page_num);
		memset(dram->tran_map->map_entry, 0, sizeof(struct entry) * tran_page_num);

		break;
	case LSM_TREE_FTL:
		/* maintain mapping table in two methods 
			 1.4 KB granularity mapping   -> easy to manage 
			 2.LSM-Tree mapping           -> actually store mapping table
		*/
		page_num = (ssd->parameter->page_block * ssd->parameter->block_plane * max_para) / (1 + ssd->parameter->overprovide);
		sub_page_num = page_num * ssd->parameter->subpage_page;
		dram->map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->map, "dram->map");
		memset(dram->map, 0, sizeof(struct map_info));
		dram->map->map_entry = (struct entry*)malloc(sizeof(struct entry) * sub_page_num);
		alloc_assert(dram->map->map_entry, "dram->map->map_entry");
		memset(dram->map->map_entry, 0, sizeof(struct entry) * sub_page_num);
		dram->mapping_buffer->max_buffer_B = (dram->mapping_buffer_capacity- sp_capacity) / B;  // not uncluding command buffer ; unit is B
		dram->mapping_node_count = 0;

		initialize_lsm(ssd);   //lsm_Tree matedata initialization
		break;
	case CHUNK_LEVEL_FTL:
		page_num = (ssd->parameter->page_block * ssd->parameter->block_plane * max_para) / (1 + ssd->parameter->overprovide);
		sub_page_num = page_num * ssd->parameter->subpage_page;

		dram->map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->map, "dram->map");
		memset(dram->map, 0, sizeof(struct map_info));
		dram->map->map_entry = (struct entry*)malloc(sizeof(struct entry) * sub_page_num);
		alloc_assert(dram->map->map_entry, "dram->map->map_entry");
		memset(dram->map->map_entry, 0, sizeof(struct entry) * sub_page_num);

		chunk_num = sub_page_num / MAX_LSN_PER_CHUNK;
		dram->chunk_map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->chunk_map, "dram->chunk_map");
		memset(dram->chunk_map, 0, sizeof(struct map_info));
		dram->chunk_map->map_entry = (struct entry*)malloc(sizeof(struct entry) * chunk_num);
		alloc_assert(dram->chunk_map->map_entry, "dram->chunk_map->map_entry");
		memset(dram->chunk_map->map_entry, 0, sizeof(struct entry) * chunk_num);

		for (i = 0; i < chunk_num; i++)
		{
			for (int j = 0; j < MAX_LSN_PER_CHUNK; j++)
			{
				dram->chunk_map->map_entry[i].bitmap[j] = 0;
			}
			dram->chunk_map->map_entry[i].map_state = READY;
		}
		break;
	case TAICHI_FTL:
		page_num = (ssd->parameter->page_block * ssd->parameter->block_plane * max_para) / (1 + ssd->parameter->overprovide);
		sub_page_num = page_num * ssd->parameter->subpage_page;

		dram->map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->map, "dram->map");
		memset(dram->map, 0, sizeof(struct map_info));
		dram->map->map_entry = (struct entry*)malloc(sizeof(struct entry) * sub_page_num);
		alloc_assert(dram->map->map_entry, "dram->map->map_entry");
		memset(dram->map->map_entry, 0, sizeof(struct entry) * sub_page_num);

		chunk_num = sub_page_num / MAX_LSN_PER_CHUNK;
		dram->chunk_map = (struct map_info*)malloc(sizeof(struct map_info));
		alloc_assert(dram->chunk_map, "dram->chunk_map");
		memset(dram->chunk_map, 0, sizeof(struct map_info));
		dram->chunk_map->map_entry = (struct entry*)malloc(sizeof(struct entry) * chunk_num);
		alloc_assert(dram->chunk_map->map_entry, "dram->chunk_map->map_entry");
		memset(dram->chunk_map->map_entry, 0, sizeof(struct entry) * chunk_num);
		for (i = 0; i < chunk_num; i++)
		{
			for (int j = 0; j < MAX_LSN_PER_CHUNK; j++)
			{
				dram->chunk_map->map_entry[i].bitmap[j] = 0;
			}
			dram->chunk_map->map_entry[i].map_state = READY;
		}

		break;
	default:

		break;
	}

	//command buffers for user data and mapping data
	dram->data_command_buffer = (tAVLTree *)avlTreeCreate((void*)keyCompareFunc, (void *)freeFunc);
	dram->data_command_buffer->max_command_buff_page = ssd->sb_pool[0].blk_cnt*ssd->parameter->subpage_page;
	switch (FTL)
	{
	case DFTL:
		dram->mapping_command_buffer = (tAVLTree*)avlTreeCreate((void*)keyCompareFunc, (void*)freeFunc);
		dram->mapping_command_buffer->max_command_buff_page = ssd->sb_pool[0].blk_cnt * ssd->parameter->subpage_page;
		break;
	case LSM_TREE_FTL:
		dram->mapping_command_buffer = (tAVLTree*)avlTreeCreate((void*)keyCompareFunc, (void*)freeFunc);
		dram->mapping_command_buffer->max_command_buff_page = ssd->sb_pool[0].blk_cnt; //transfer ssd->lsmTree->dataTableItems

		dram->lsm_tree_mapping_command_buf = (struct lsm_tree_command_buffer*)malloc(sizeof(struct lsm_tree_command_buffer));
		dram->lsm_tree_mapping_command_buf->max_smt_count = ssd->sb_pool[0].blk_cnt;
		dram->lsm_tree_mapping_command_buf->smt_count = 0;
		dram->lsm_tree_mapping_command_buf->smts = (struct SMT**)malloc(sizeof(struct SMT*) * dram->lsm_tree_mapping_command_buf->max_smt_count);
		for (i = 0; i < dram->lsm_tree_mapping_command_buf->max_smt_count; i++)
			dram->lsm_tree_mapping_command_buf->smts[i] = NULL;

		break;
	default:
		break;
	}
	/******************************************************************************************************************************************/


	return dram;
}

//initialize all superblocks 
void intialize_sb(struct ssd_info * ssd)
{
	int i,chan,chip,die,plane,block_off;
	int k;
	int sb_num,sb_size;
	int max_para = ssd->parameter->channel_number*ssd->parameter->chip_channel[0] * ssd->parameter->die_chip*ssd->parameter->plane_die;

	k = 0;
	switch (1)
	{
		case 1: //plane-level superblock 
			sb_num = ssd->parameter->block_plane;
			ssd->sb_pool = (struct super_block_info *)malloc(sizeof(struct super_block_info)*sb_num);
			sb_size = max_para;
			for (i = 0;  i< sb_num; i++)
			{
				ssd->sb_pool[i].ec = 0;
				ssd->sb_pool[i].blk_cnt = sb_size;
				ssd->sb_pool[i].next_wr_page = 0;
				ssd->sb_pool[i].pg_off = -1;
				ssd->sb_pool[i].pos = (struct local *)malloc(sizeof(struct local)*sb_size);
				ssd->sb_pool[i].blk_type = -1;
				ssd->sb_pool[i].gcing = 0;
				ssd->sb_pool[i].refresh_flag = 0;
				block_off = 0;

				for (chan = 0; chan < ssd->parameter->channel_number; chan++)
				{
					for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++)
					{
						for (die = 0; die < ssd->parameter->die_chip; die++)
						{
							for (plane = 0; plane < ssd->parameter->plane_die; plane++)
							{
								ssd->sb_pool[i].pos[block_off].channel = chan;
								ssd->sb_pool[i].pos[block_off].chip = chip;
								ssd->sb_pool[i].pos[block_off].die = die;
								ssd->sb_pool[i].pos[block_off].plane = plane;
								ssd->sb_pool[i].pos[block_off].block = k / max_para;
								k++;
								block_off++;
							}
						}
					}
				}
				block_off = 0;
			}	
			break;
		case 2: //die-level superblock 
			sb_num = ssd->parameter->block_plane * ssd->parameter->plane_die;
			ssd->sb_pool = (struct super_block_info *)malloc(sizeof(struct super_block_info)*sb_num);
			sb_size = max_para/ssd->parameter->plane_die;
			for (i = 0; i< sb_num; i++)
			{
				ssd->sb_pool[i].ec = 0;
				ssd->sb_pool[i].blk_cnt = sb_size;
				ssd->sb_pool[i].next_wr_page = 0;
				ssd->sb_pool[i].pg_off = -1;
				ssd->sb_pool[i].pos = (struct local *)malloc(sizeof(struct local)*sb_size);
				block_off = 0;

				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++)
					{
						for (chan = 0; chan < ssd->parameter->channel_number; chan++)
						{
							ssd->sb_pool[i].pos[block_off].channel = chan;
							ssd->sb_pool[i].pos[block_off].chip = chip;
							ssd->sb_pool[i].pos[block_off].die = die;

							//decide the plane no in the die
							ssd->sb_pool[i].pos[block_off].plane =Get_Plane(ssd,i);
							ssd->sb_pool[i].pos[block_off].block = k / max_para;
							k++;
							block_off++;
						}
					}
				}
				block_off = 0;
			}
			break;
		case 3://chip-level superblock 
			sb_num = ssd->parameter->block_plane * ssd->parameter->plane_die*ssd->parameter->die_chip;
			ssd->sb_pool = (struct super_block_info *)malloc(sizeof(struct super_block_info)*sb_num);
			sb_size = max_para / (ssd->parameter->plane_die*ssd->parameter->die_chip);
			for (i = 0; i< sb_num; i++)
			{
				ssd->sb_pool[i].ec = 0;
				ssd->sb_pool[i].blk_cnt = sb_size;
				ssd->sb_pool[i].next_wr_page = 0;
				ssd->sb_pool[i].pg_off = -1;
				ssd->sb_pool[i].pos = (struct local *)malloc(sizeof(struct local)*sb_size);
				block_off = 0;

				for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++)
				{
					for (chan = 0; chan < ssd->parameter->channel_number; chan++)
					{
						ssd->sb_pool[i].pos[block_off].channel = chan;
						ssd->sb_pool[i].pos[block_off].chip = chip;

						//decide the die no and plane no
						ssd->sb_pool[i].pos[block_off].die = Get_Die(ssd,i);
						ssd->sb_pool[i].pos[block_off].plane = Get_Plane(ssd,i);
						ssd->sb_pool[i].pos[block_off].block = k / max_para;
						k++;
						block_off++;
					}
				}
				block_off = 0;
			}
			break;
		case 4://channel-level superblock 
			sb_num = ssd->parameter->block_plane * ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0];
			ssd->sb_pool = (struct super_block_info *)malloc(sizeof(struct super_block_info)*sb_num);
			sb_size = max_para / (ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0]);
			for (i = 0; i< sb_num; i++)
			{
				ssd->sb_pool[i].ec = 0;
				ssd->sb_pool[i].blk_cnt = sb_size;
				ssd->sb_pool[i].next_wr_page = 0;
				ssd->sb_pool[i].pg_off = -1;
				ssd->sb_pool[i].pos = (struct local *)malloc(sizeof(struct local)*sb_size);
				block_off = 0;

			   for (chan = 0; chan < ssd->parameter->channel_number; chan++)
			   {
					ssd->sb_pool[i].pos[block_off].channel = chan;

					ssd->sb_pool[i].pos[block_off].chip = Get_Chip(ssd,i);
					ssd->sb_pool[i].pos[block_off].die = Get_Die(ssd,i);
					ssd->sb_pool[i].pos[block_off].plane = Get_Plane(ssd,i);
					ssd->sb_pool[i].pos[block_off].block = k / max_para;
					k++;
					block_off++;
				}
				block_off = 0;
			}
			break;
		default:
			break;
	}

	ssd->sb_cnt = sb_num;
	ssd->free_sb_cnt = ssd->sb_cnt;

	/*
	ssd->open_sb = (struct super_block_info *)malloc(sizeof(struct super_block_info));
	ssd->open_sb->blk_cnt = sb_size;
	ssd->open_sb->ec = 0;
	ssd->open_sb->next_wr_page = 0;
	ssd->open_sb->pg_off = -1;
	ssd->open_sb->pos = (struct super_block_info *)malloc(sizeof(struct super_block_info)*sb_num);

	for (i = 0; i < sb_size; i++)
	{
		ssd->open_sb->pos[i].channel = -1;
		ssd->open_sb->pos[i].chip = -1;
		ssd->open_sb->pos[i].die = -1;
		ssd->open_sb->pos[i].plane = -1;
		ssd->open_sb->pos[i].block = -1;
	}
	*/
}

//show pe info 
void show_sb_info(struct ssd_info * ssd)
{
	int i,j;
	int chan, chip, die, plane, block;
	for (i = 0; i < ssd->sb_cnt; i++)
	{
		fprintf(ssd->sb_info, "superblock %d\n", i);
		for (j = 0; j < ssd->sb_pool[i].blk_cnt; j++)
		{
			chan = ssd->sb_pool[i].pos[j].channel;
			chip = ssd->sb_pool[i].pos[j].chip;
			die = ssd->sb_pool[i].pos[j].die;
			plane = ssd->sb_pool[i].pos[j].plane;
			block = ssd->sb_pool[i].pos[j].block;
			fprintf(ssd->sb_info, "chan = %d  chip = %d  die = %d plane = %d  block = %d\n",chan,chip,die,plane,block);
		}
	}
	fflush(ssd->sb_info);
}


//return channel
int Get_Channel(struct ssd_info * ssd, int i)
{
	int off = i%(ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0]*ssd->parameter->channel_number);
	int chan = off / (ssd->parameter->chip_channel[0] * ssd->parameter->die_chip*ssd->parameter->plane_die);
	return chan;
}

//return chip
int Get_Chip(struct ssd_info * ssd, int i)
{
	int off = i % (ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->chip_channel[0]);
	int chip = off / (ssd->parameter->die_chip*ssd->parameter->plane_die);
	return chip;
}

//return die
int Get_Die(struct ssd_info * ssd, int i)
{
	int off = i % (ssd->parameter->die_chip*ssd->parameter->plane_die);
	int die = off / ssd->parameter->plane_die;
	return die;
}

//return plane 
int Get_Plane(struct ssd_info * ssd, int i)
{
	int off = i % ssd->parameter->plane_die;
	int plane = off;
	return plane;
}

struct page_info * initialize_page(struct page_info * p_page )
{
	int i = 0;
	p_page->valid_state =0;
	p_page->type = -1;
	p_page->free_state = PG_SUB;
	p_page->lpn = -1;
	p_page->written_count=0;
	p_page->smt = NULL;

	p_page->read_disturb_cnt = 0;

	for (i = 0; i < MAX_LUN_PER_PAGE; i++)
	{
		p_page->luns[i] = -1;
		p_page->lun_state[i] = 0;
	}

	return p_page;
}

struct blk_info * initialize_block(struct blk_info * p_block,struct parameter_value *parameter)
{
	unsigned int i;
	struct page_info * p_page;
	
	p_block->erase_count = 0;
	p_block->page_read_count = 0;
	p_block->page_write_count = 0;
	p_block->pre_write_count = 0;

	p_block->start_time = 0;
	p_block->read_cnt = 0;
	p_block->close_open_read_flag = 0;

	p_block->free_page_num = parameter->page_block;	// all pages are free
	p_block->last_write_page = -1;	// no page has been programmed

	p_block->page_head = (struct page_info *)malloc(parameter->page_block * sizeof(struct page_info));

	alloc_assert(p_block->page_head,"p_block->page_head");
	memset(p_block->page_head,0,parameter->page_block * sizeof(struct page_info));

	for(i = 0; i<parameter->page_block; i++)
	{
		p_page = &(p_block->page_head[i]);
		initialize_page(p_page );
	}
	return p_block;

}

struct plane_info * initialize_plane(struct plane_info * p_plane,struct parameter_value *parameter )
{
	unsigned int i;
	struct blk_info * p_block;
	p_plane->add_reg_ppn = -1;  //Plane address register additional register -1 means no data
	p_plane->free_page=parameter->block_plane*parameter->page_block;
	p_plane->plane_read_count = 0;
	p_plane->plane_program_count = 0;
	p_plane->plane_erase_count = 0;
	p_plane->pre_plane_write_count = 0;;

	p_plane->blk_head = (struct blk_info *)malloc(parameter->block_plane * sizeof(struct blk_info));
	alloc_assert(p_plane->blk_head,"p_plane->blk_head");
	memset(p_plane->blk_head,0,parameter->block_plane * sizeof(struct blk_info));

	for(i = 0; i<parameter->block_plane; i++)
	{
		p_block = &(p_plane->blk_head[i]);
		initialize_block( p_block ,parameter);			
	}
	return p_plane;
}

struct die_info * initialize_die(struct die_info * p_die,struct parameter_value *parameter,long long current_time )
{
	unsigned int i;
	struct plane_info * p_plane;

	p_die->die_read_count = 0;
	p_die->die_program_count = 0;
	p_die->die_erase_count = 0;
	p_die->read_cnt = 0;

	p_die->plane_head = (struct plane_info*)malloc(parameter->plane_die * sizeof(struct plane_info));
	alloc_assert(p_die->plane_head,"p_die->plane_head");
	memset(p_die->plane_head,0,parameter->plane_die * sizeof(struct plane_info));

	for (i = 0; i<parameter->plane_die; i++)
	{
		p_plane = &(p_die->plane_head[i]);
		initialize_plane(p_plane,parameter );
	}

	return p_die;
}

struct chip_info * initialize_chip(struct chip_info * p_chip,struct parameter_value *parameter,long long current_time )
{
	unsigned int i;
	struct die_info *p_die;
	
	p_chip->gc_signal = SIG_NORMAL;
	p_chip->erase_begin_time = 0;
	p_chip->erase_cmplt_time = 0;
	p_chip->erase_rest_time = 0;

	p_chip->current_state = CHIP_IDLE;
	p_chip->next_state = CHIP_IDLE;
	p_chip->current_time = current_time;
	p_chip->next_state_predict_time = 0;

	p_chip->die_num = parameter->die_chip;
	p_chip->plane_num_die = parameter->plane_die;
	p_chip->block_num_plane = parameter->block_plane;
	p_chip->page_num_block = parameter->page_block;
	p_chip->subpage_num_page = parameter->subpage_page;
	p_chip->ers_limit = parameter->ers_limit;
	p_chip->ac_timing = parameter->time_characteristics;		
	p_chip->chip_read_count = 0;
	p_chip->chip_program_count = 0;
	p_chip->chip_erase_count = 0;

	p_chip->die_head = (struct die_info *)malloc(parameter->die_chip * sizeof(struct die_info));
	alloc_assert(p_chip->die_head,"p_chip->die_head");
	memset(p_chip->die_head,0,parameter->die_chip * sizeof(struct die_info));

	for (i = 0; i<parameter->die_chip; i++)
	{
		p_die = &(p_chip->die_head[i]);
		initialize_die( p_die,parameter,current_time );	
	}

	return p_chip;
}

struct ssd_info * initialize_channels(struct ssd_info * ssd )
{
	unsigned int i,j;
	struct channel_info * p_channel;
	struct chip_info * p_chip;

	// set the parameter of each channel
	for (i = 0; i< ssd->parameter->channel_number; i++)
	{
		ssd->channel_head[i].channel_busy_flag = 0;
		ssd->channel_head[i].channel_read_count = 0;
		ssd->channel_head[i].channel_program_count = 0;
		ssd->channel_head[i].channel_erase_count = 0;
		p_channel = &(ssd->channel_head[i]);
		p_channel->chip = ssd->parameter->chip_channel[i];
		p_channel->current_state = CHANNEL_IDLE;
		p_channel->next_state = CHANNEL_IDLE;
		
		p_channel->chip_head = (struct chip_info *)malloc(ssd->parameter->chip_channel[i]* sizeof(struct chip_info));
		alloc_assert(p_channel->chip_head,"p_channel->chip_head");
		memset(p_channel->chip_head,0,ssd->parameter->chip_channel[i]* sizeof(struct chip_info));

		for (j = 0; j< ssd->parameter->chip_channel[i]; j++)
		{
			p_chip = &(p_channel->chip_head[j]);
			initialize_chip(p_chip,ssd->parameter,ssd->current_time);
		}
	}

	return ssd;
}


struct parameter_value *load_parameters(char parameter_file[30])
{
	FILE * fp;
	errno_t ferr;
	struct parameter_value *p;
	char buf[BUFSIZE];
	int i;
	int pre_eql,next_eql;
	int res_eql;
	char *ptr;

	p = (struct parameter_value *)malloc(sizeof(struct parameter_value));
	alloc_assert(p,"parameter_value");
	memset(p,0,sizeof(struct parameter_value));
	memset(buf,0,BUFSIZE);
		
	if((ferr = fopen_s(&fp,parameter_file,"r"))!= 0)
	{	
		printf("the file parameter_file error!\n");	
		return p;
	}


	while(fgets(buf,250,fp)){
		if(buf[0] =='#' || buf[0] == ' ') continue;
		ptr=strchr(buf,'=');
		if(!ptr) continue; 
		
		pre_eql = ptr - buf;
		next_eql = pre_eql + 1;

		while(buf[pre_eql-1] == ' ') pre_eql--;
		buf[pre_eql] = 0;
		if((res_eql=strcmp(buf,"chip number")) ==0){			
			sscanf(buf + next_eql,"%d",&p->chip_num);           //The number of chips
		}else if((res_eql=strcmp(buf,"data dram capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->data_dram_capacity);  //The size of the write cache, the unit is byte
		}else if((res_eql=strcmp(buf,"read dram capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->read_dram_capacity);  //The size of the read cache, the unit is byte
		}else if((res_eql = strcmp(buf, "mapping dram capacity")) == 0) {
			sscanf(buf + next_eql, "%d", &p->mapping_dram_capacity);
		}else if((res_eql=strcmp(buf,"channel number")) ==0){
			sscanf(buf + next_eql,"%d",&p->channel_number);		//The number of channels
		}else if((res_eql=strcmp(buf,"die number")) ==0){
			sscanf(buf + next_eql,"%d",&p->die_chip);			//The number of die
		}else if((res_eql=strcmp(buf,"plane number")) ==0){
			sscanf(buf + next_eql,"%d",&p->plane_die);			//The number of planes
		}else if((res_eql=strcmp(buf,"block number")) ==0){
			sscanf(buf + next_eql,"%d",&p->block_plane);		//The number of blocks
		}else if((res_eql=strcmp(buf,"page number")) ==0){
			sscanf(buf + next_eql,"%d",&p->page_block);			//The number of pages
		}else if((res_eql=strcmp(buf,"subpage page")) ==0){
			sscanf(buf + next_eql,"%d",&p->subpage_page);		//Page contains subpage (number of sectors)
		}else if((res_eql=strcmp(buf,"page capacity")) ==0){   
			sscanf(buf + next_eql,"%d",&p->page_capacity);		//The size of a page
		}else if((res_eql=strcmp(buf,"subpage capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->subpage_capacity);   //The size of a subpage (sector)
		}else if ((res_eql = strcmp(buf, "mapping entry size")) == 0) {
			sscanf(buf + next_eql, "%d", &p->mapping_entry_size);   //The size of a subpage (sector)
		}else if((res_eql=strcmp(buf,"t_PROG")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tPROG); //Write time to write flash
		}else if((res_eql=strcmp(buf,"t_DBSY")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDBSY);  //data busy time
		}else if((res_eql=strcmp(buf,"t_BERS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tBERS); // erases the time of a block
		}else if((res_eql=strcmp(buf,"t_PROGO"))== 0){
			sscanf(buf + next_eql, "%d", &p->time_characteristics.tPROGO);  //one shot program time
		}else if ((res_eql = strcmp(buf, "t_ERSL")) == 0){
			sscanf(buf + next_eql, "%d", &p->time_characteristics.tERSL);  //the trans time of suspend/resume operation
		}else if ((res_eql = strcmp(buf, "t_R")) == 0){
			sscanf(buf + next_eql, "%d", &p->time_characteristics.tR); //The time to read flash
		}else if ((res_eql = strcmp(buf, "t_WC")) == 0){
			sscanf(buf + next_eql, "%d", &p->time_characteristics.tWC); //Transfer address One byte of time
		}else if ((res_eql = strcmp(buf, "t_RC")) == 0){
			sscanf(buf + next_eql, "%d", &p->time_characteristics.tRC); //The time it takes to transfer data one byte
		}else if((res_eql=strcmp(buf,"t_CLS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLS); 
		}else if((res_eql=strcmp(buf,"t_CLH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLH); 
		}else if((res_eql=strcmp(buf,"t_CS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCS); 
		}else if((res_eql=strcmp(buf,"t_CH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCH); 
		}else if((res_eql=strcmp(buf,"t_WP")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWP); 
		}else if((res_eql=strcmp(buf,"t_ALS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tALS); 
		}else if((res_eql=strcmp(buf,"t_ALH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tALH); 
		}else if((res_eql=strcmp(buf,"t_DS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDS); 
		}else if((res_eql=strcmp(buf,"t_DH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDH); 
		}else if((res_eql=strcmp(buf,"t_WH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWH); 
		}else if((res_eql=strcmp(buf,"t_ADL")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tADL); 
		}else if((res_eql=strcmp(buf,"t_AR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tAR); 
		}else if((res_eql=strcmp(buf,"t_CLR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLR); 
		}else if((res_eql=strcmp(buf,"t_RR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRR); 
		}else if((res_eql=strcmp(buf,"t_RP")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRP); 
		}else if((res_eql=strcmp(buf,"t_WB")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWB); 
		}else if((res_eql=strcmp(buf,"t_REA")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tREA); 
		}else if((res_eql=strcmp(buf,"t_CEA")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCEA); 
		}else if((res_eql=strcmp(buf,"t_RHZ")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHZ); 
		}else if((res_eql=strcmp(buf,"t_CHZ")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCHZ); 
		}else if((res_eql=strcmp(buf,"t_RHOH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHOH); 
		}else if((res_eql=strcmp(buf,"t_RLOH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRLOH); 
		}else if((res_eql=strcmp(buf,"t_COH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCOH); 
		}else if((res_eql=strcmp(buf,"t_REH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tREH); 
		}else if((res_eql=strcmp(buf,"t_IR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tIR); 
		}else if((res_eql=strcmp(buf,"t_RHW")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHW); 
		}else if((res_eql=strcmp(buf,"t_WHR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWHR); 
		}else if((res_eql=strcmp(buf,"t_RST")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRST); 
		}else if((res_eql=strcmp(buf,"erase limit")) ==0){
			sscanf(buf + next_eql,"%d",&p->ers_limit);					//The number of times each block can be erased
		}else if((res_eql=strcmp(buf,"address mapping")) ==0){
			sscanf(buf + next_eql,"%d",&p->address_mapping);			//Address type (1: page; 2: block; 3: fast)
		}else if((res_eql=strcmp(buf,"wear leveling")) ==0){
			sscanf(buf + next_eql,"%d",&p->wear_leveling);				//Supports WL mode
		}else if((res_eql=strcmp(buf,"gc")) ==0){
			sscanf(buf + next_eql,"%d",&p->gc);							//Gc strategy, the general gc strategy, using the gc_threshold as a threshold, the active write strategy, that can be interrupted gc, need to use gc_hard_threshold hard threshold
		}else if((res_eql=strcmp(buf,"overprovide")) ==0){ 
			sscanf(buf + next_eql,"%f",&p->overprovide);                //The size of the op space
		}else if((res_eql=strcmp(buf,"buffer management")) ==0){
			sscanf(buf + next_eql,"%d",&p->buffer_management);          //Whether to support data cache
		}else if((res_eql=strcmp(buf,"scheduling algorithm")) ==0){
			sscanf(buf + next_eql,"%d",&p->scheduling_algorithm);       //Scheduling algorithm :FCFS
		}else if((res_eql=strcmp(buf,"gc hard threshold")) ==0){
			sscanf(buf + next_eql,"%f",&p->gc_hard_threshold);          //Gc hard threshold setting for the active write gc strategy to determine the threshold
		}else if ((res_eql = strcmp(buf, "gc soft threshold")) == 0) {         
			sscanf(buf + next_eql, "%f", &p->gc_soft_threshold);		 //Gc soft threshold setting for the active write gc strategy to determine the threshold(excute the gc_request in the gc_linklist)
		}else if((res_eql=strcmp(buf,"allocation")) ==0){
			sscanf(buf + next_eql,"%d",&p->allocation_scheme);		    //Determine the allocation method, 0 that dynamic allocation, that is, dynamic allocation of each channel, the static allocation that according to address allocation
		}else if ((res_eql=strcmp(buf, "static_allocation")) == 0){
			sscanf(buf + next_eql, "%d", &p->static_allocation);        //record the static allocation in ssd
		}else if((res_eql=strcmp(buf, "dynamic_allocation")) == 0){
			sscanf(buf + next_eql, "%d", &p->dynamic_allocation);	 //Indicates the priority of the ssd allocation mode, 0 means channel> chip> die> plane, and 1 represents plane> channel> chip> die
		}else if((res_eql=strcmp(buf,"advanced command")) ==0){
			sscanf(buf + next_eql,"%d",&p->advanced_commands);         //Whether to use the advanced command, 0 means not to use. (00001), copyback (00010), two-plane-program (00100), interleave (01000), and two-plane-read (10000) are used respectively, and all use is 11111, both 31       
		}else if((res_eql=strcmp(buf,"greed MPW command")) ==0){
			sscanf(buf + next_eql,"%d",&p->greed_MPW_ad);               //Indicates whether greedy use of multi-plane write advanced command, 0 for no, 1 for greedy use
		}else if((res_eql=strcmp(buf,"aged")) ==0){
			sscanf(buf + next_eql,"%d",&p->aged);                       //1 indicates that the SSD needs to be aged, 0 means that the SSD needs to be kept non-aged
		}else if((res_eql=strcmp(buf,"aged ratio")) ==0){
			sscanf(buf + next_eql,"%f",&p->aged_ratio);                 //Indicates that the SSD needs to be set to invaild in advance for SSD to become aged
		}else if ((res_eql = strcmp(buf, "flash mode")) == 0){
			sscanf(buf + next_eql, "%d", &p->flash_mode);
		}else if((res_eql=strcmp(buf,"requset queue depth")) ==0){
			sscanf(buf + next_eql,"%d",&p->queue_length);               //Request the queue depth
		}else if ((res_eql = strcmp(buf, "warm flash")) == 0){
			sscanf(buf + next_eql, "%d", &p->warm_flash);
		}else if((res_eql=strncmp(buf,"chip number",11)) ==0)
		{
			sscanf(buf+12,"%d",&i);
			sscanf(buf + next_eql,"%d",&p->chip_channel[i]);            //The number of chips on a channel
		}else{
			printf("don't match\t %s\n",buf);
		}
		
		memset(buf,0,BUFSIZE);
		
	}
	fclose(fp);

	return p;
}


int Get_Read_Request_Cnt(struct ssd_info *ssd, unsigned int chan,unsigned int chip,unsigned int die)
{
	int cnt = ssd->channel_head[chan].chip_head[chip].die_head[die].read_cnt;
	return cnt;
}

Status Read_cnt_4_Debug(struct ssd_info *ssd)
{
	unsigned int read_cnt;
	unsigned int que_read_cnt;

	unsigned int chan, chip, die;
	struct sub_request *sub;

	read_cnt = 0;
	que_read_cnt = 0;

	for (chan = 0; chan < ssd->parameter->channel_number; chan++)
	{
		
		for (chip = 0; chip < ssd->parameter->chip_channel[chan]; chip++)
		{
			for (die = 0; die < ssd->parameter->die_chip; die++)
			{
				read_cnt+=ssd->channel_head[chan].chip_head[chip].die_head[die].read_cnt;
			}
		}

		fprintf(ssd->read_req, "Channel %d 读请求如下\n", chan);
		sub = ssd->channel_head[chan].subs_r_head;
		while (sub)
		{
			fprintf(ssd->read_req,"OX%p ->", sub);
			if (sub->next_state != SR_COMPLETE)
				que_read_cnt++;
			sub = sub->next_node;
		}
		fprintf(ssd->read_req, "\n");
	}
	fflush(ssd->read_req);
	return (read_cnt == que_read_cnt);
}

Status Write_cnt(struct ssd_info* ssd, unsigned int chan)
{
	unsigned int w_cnt;
	struct sub_request* sub;

	w_cnt = 0;

	fprintf(ssd->write_req, "Channel %d  sub reqeusts are as follow:\n", chan);
	sub = ssd->channel_head[chan].subs_w_head;
	while (sub)
	{
		fprintf(ssd->write_req, "OX%p ->", sub);
		w_cnt++;
		sub = sub->next_node;
	}
	fprintf(ssd->write_req, "\n");
	
	fflush(ssd->write_req);
	return w_cnt;
}

Status Read_cnt(struct ssd_info* ssd, unsigned int chan)
{
	unsigned int r_cnt;
	struct sub_request* sub;

	r_cnt = 0;
	fprintf(ssd->read_req, "Channel %d  sub reqeusts are as follow:\n", chan);
	sub = ssd->channel_head[chan].subs_r_head;
	while (sub)
	{
		fprintf(ssd->read_req, "OX%p ->", sub);
		r_cnt++;
		sub = sub->next_node;
		if (r_cnt > 50)
		{
			break;
		}
	}
	fprintf(ssd->read_req, "\n");
	fflush(ssd->read_req);
	return r_cnt;
}

Status Debug_loc_allocation(struct ssd_info* ssd, unsigned int pun, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page, unsigned int unit)
{
	struct  local* loc;

	loc = find_location_pun(ssd, pun);

	if (loc->channel != channel || loc->chip != chip || loc->die != die || loc->plane != plane || loc->block != block || loc->page != page || loc->sub_page != unit)
	{
		printf("look here 11 \n");
		getchar();
	}

	free(loc);
	loc = NULL;
	return SUCCESS;
}

Status Debug_Invalid_Count(struct ssd_info * ssd)
{
	unsigned int channel, chip, die, plane, block;
	unsigned int count=0, invalid_subpage_num;
	
	for (channel = 0; channel < ssd->parameter->channel_number; channel++)
	{
		for (chip = 0; chip < ssd->parameter->chip_channel[channel]; chip++)
		{
			for (die = 0; die < ssd->parameter->die_chip; die++)
			{
				for (plane = 0; plane < ssd->parameter->plane_die; plane++)
				{
					for (block = 0; block < ssd->parameter->block_plane; block++)
					{
						if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page == ssd->parameter->page_block-1)
						{
							count = Get_invalid_data(ssd, channel, chip, die, plane, block);
							invalid_subpage_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_subpage_num;
							if (count != invalid_subpage_num)
							{
								printf("Look Here 10\n");
								printf("the count is %d, the record of block is %d\n", count, invalid_subpage_num);
								getchar();
								return FAILURE;
							}
						}
					}
				}
			}
		}
	}
	return SUCCESS;
}

int Get_invalid_data(struct ssd_info * ssd,unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block)
{
	unsigned int page, cnt = 0,i;
	unsigned int blk_type;

	blk_type = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].block_type;
	for (page = 0; page <= ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page; page++)
	{
		if (blk_type == MAPPING_BLOCK)
		{
			switch (FTL)
			{
			case DFTL:
				for (i = 0; i < ssd->parameter->subpage_page; i++)
				{
					if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_state[i] == 0)
						cnt++;
				}
				break;
			case LSM_TREE_FTL:
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state == 0)
					cnt += ssd->parameter->subpage_page;
				break;
			default:
				break;
			}
		}
		else
		{
			for (i = 0; i < ssd->parameter->subpage_page; i++)
			{
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_state[i] == 0)
					cnt++;
			}
		}
	}
	return cnt;
}