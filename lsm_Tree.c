/**
 * @file control.c
 * @author Zijie Han (zijiehan@qq.com)
 * @brief
 * @version 0.1
 * @date 2019-07-11
 *
 * @copyright Copyright (c) 2019
 *
 */
#define _CRTDBG_MAP_ALLOC

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include< crtdbg.h>
#include <math.h>
#include "buffer.h"
#include "lsm_Tree.h"
#include "ftl.h"

extern int secno_num_per_page, secno_num_sub_page;

long new_ppn = 0;   //write count
int free_count = 0; //free SMT count
long read_request_size = 0;
/**
 * @brief 为类型为lsmTreeNode* 的ssd树根节点进行初始化，给节点分配存储空间
 *
 * @param T 模拟SSD树的根节点
 * @param levels 树的层数
 * @param SmtNumOfL0 第一层Smt的个数
 * @param SmtSize 每一层Smt的大小，数组，若为NULL，则每一层大小都为一个闪存页大小
 * @param pageSize_KB 闪存页的大小，以KB为单位
 * @param timesOfSmt 每一层smt增加的倍数
 * @param AddrSize_B if AddrSize_B = 0,it will be set as 5 Byte.每个地址所占的空间大小，若指定为0，则默认为4
 */
void initialize_lsm(struct ssd_info* ssd)
{
	unsigned int max_para;
	long log_capacity;
	unsigned int layer_count = 0, SmtNumOfL0;
	long capacity_threshold[MAX_LSM_TREE_LAYER];
	unsigned int i, basicNum;

	struct lsmTreeNode* T = (struct lsmTreeNode*)malloc(sizeof(struct lsmTreeNode));
	//检查T是否为NULL，是NULL则报错
	if (T == NULL)
	{
		printf("NULL POINTER EXCEPTION IN FUNCTION initialize_lsm \n");
		exit(EXIT_FAILURE);
	}
	T->buffer_array = NULL;
	T->buffer_length = 0;
	ssd->smt_id = 0;
	//determine the layer count of lsm-Tree accordinng to the SSD capacity
	max_para = ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip * ssd->parameter->chip_channel[0] * ssd->parameter->channel_number;
	log_capacity = secno_num_per_page * max_para / 2; //unit is KB

	SmtNumOfL0 = L0_SMT_CNT;
	capacity_threshold[0] = SmtNumOfL0 * secno_num_per_page / 2;
	if (log_capacity < capacity_threshold[0])
		layer_count = 1;
	for (i = 1; i < MAX_LSM_TREE_LAYER; i++)
	{
		capacity_threshold[i] = capacity_threshold[i - 1] * LAYER_TIMES;
		if (log_capacity < capacity_threshold[i])
		{
			layer_count = i + 1;
			break;
		}
	}

	ssd->max_lsm_read_count = layer_count + L0_SMT_CNT - 1;
	T->levelNum = layer_count;                                                           //lsm-Tree layer count
	T->level = (struct lsmTreeLevel*)malloc(sizeof(struct lsmTreeLevel) * layer_count); //initialize smt counts per layer
	T->smtNums = (int*)malloc(sizeof(int) * layer_count);
	T->dataTableItems = ssd->parameter->page_capacity / ssd->parameter->mapping_entry_size;

	//initialize lsm-tree statistics
	T->read_count = 0;
	T->req_read_count = 0;
	T->com_count = 0;
	T->write_count = 0;
	T->req_write_count = 0;

	//initailize metadata of smts per layer
	basicNum = SmtNumOfL0;
	for (i = 0; i < layer_count; i++)
	{
		*(T->smtNums + i) = basicNum;
		basicNum *= LAYER_TIMES;
		//对level中结构体内的变量进行赋值
		T->level[i].manifest[0] = 0; //min lpn in smt
		T->level[i].manifest[1] = 0; //max lpn in smt
		//statistics
		T->level[i].read_count = 0;
		T->level[i].com_count = 0;
		T->level[i].write_count = 0;
		T->level[i].fullFlag = 0;
		T->level[i].smtArray = (struct SMT**)malloc(sizeof(struct SMT*) * T->smtNums[i]);
		T->level[i].usedSmt = 0;
	}
	ssd->lsmTree = T;
}

/**
 * @brief 将数据插入L0层
 *
 * @param T 树根节点
 * @param s 被插入的smt对象
 * @return int
 */
int insertToL0(struct ssd_info* ssd, struct SMT* s, struct request* req)
{
	struct SMT* temp1, * temp2;
	struct lsmTreeNode* T;
	T = ssd->lsmTree;

	//printTree(ssd, T);
	s->layer_zero_flag = 1;
	s->ppn = -1;
	if (T->level[0].fullFlag == 1)
	{
		//L0层已满
		//调用dumpL0()操作
		//先检测L0层是否有该数据，没有再进行dump操作
		T->level[0].usedSmt -= 1;
		compaction(ssd, T->level[0].smtArray[T->smtNums[0] - 1], 1, req);
	}
	//将整个smt的表向后移动一位
	// SMT *temp1 = T->level[0].smtArray[0];
	// SMT *temp2 = T->level[0].smtArray[1];
	// for (int i = 0; i < T->smtNums[0] - 1; i++)
	// {
	//     temp2 = T->level[0].smtArray[i + 1];
	//     T->level[0].smtArray[i + 1] = temp1;
	//     temp1 = temp2;
	// }
	SMT_array_shift(T->level[0].smtArray, 1, T->smtNums[0], 0, 1);

	T->level[0].smtArray[0] = s;
	T->level[0].usedSmt += 1;
	if (T->level->usedSmt == T->smtNums[0])
	{
		T->level[0].fullFlag = 1;
	}

	//modify the manifest of L0
	T->level[0].manifest[0] = T->level[0].smtArray[0]->manifest[0];
	T->level[0].manifest[1] = T->level[0].smtArray[0]->manifest[1];
	for (int i = 1; i <= T->level[0].usedSmt - 1; i++)
	{
		if (T->level[0].smtArray[i]->manifest[0] < T->level[0].manifest[0])
		{
			T->level[0].manifest[0] = T->level[0].smtArray[i]->manifest[0];
		}
		if (T->level[0].smtArray[i]->manifest[1] > T->level[0].manifest[1])
		{
			T->level[0].manifest[1] = T->level[0].smtArray[i]->manifest[1];
		}
	}

	//printTree(ssd, T);
	return 1;
}

/**
 * @brief 对传入smt做compaction操作
 *
 * @param T 树的根节点
 * @param s SMT
 * @param dstLevel 目标level
 * @return int 最终完成的时候compaction的层
 */
int compaction(struct ssd_info* ssd, struct SMT* s, int dstLevel, struct request* req)
{
	struct lsmTreeNode* T;
	T = ssd->lsmTree;
	//检查是否有overlap
	int overlap_length = 0;
	int overlap_bgn_offset = is_overlap(T, s, dstLevel, &overlap_length);

	fprintf(ssd->smt_info, "\n manifest:%u ~ %u \n", s->manifest[0], s->manifest[1]);
	fprintf(ssd->smt_info, "Compaction length = %d \n", overlap_length);

	//处理没有overlap的情况
	if (overlap_length == 0)
	{
		return handle_no_overlap(ssd, s, dstLevel, req);
	}
	//先生成新的smt
	T->level[dstLevel].com_count++;
	int new_smt_num;
	struct SMT** subSmt = NULL;
	subSmt = merge_smt(ssd, s, dstLevel, overlap_bgn_offset, overlap_length, &new_smt_num, req);
	T->buffer_array = subSmt;
	T->buffer_length = new_smt_num;

	for (int i = 0; i < overlap_length; i++)
	{
		free_smt(ssd, T->level[dstLevel].smtArray[overlap_bgn_offset + i]);
	}

	free_smt(ssd, s);

	if (new_smt_num == overlap_length)
	{ //smt数目不变
		for (int i = 0; i < overlap_length; i++)
		{
			insert_lsmTree(ssd, subSmt[i], dstLevel, overlap_bgn_offset + i, req);
		}
		free(subSmt);
		return maintain_manifest_of_level(T, dstLevel);
	}
	else if (new_smt_num < overlap_length)
	{ //smt数目减少
		int shift_num = overlap_length - new_smt_num;
		for (int i = overlap_bgn_offset + overlap_length; i < T->level[dstLevel].usedSmt; i++)
		{
			T->level[dstLevel].smtArray[i - shift_num] = T->level[dstLevel].smtArray[i];
		}
		for (int i = 0; i < shift_num; i++)
		{
			T->level[dstLevel].smtArray[T->level[dstLevel].usedSmt - 1 - i] = NULL;
		}
		T->level[dstLevel].usedSmt -= shift_num;
		T->level[dstLevel].fullFlag = 0;
		int j = 0;
		for (int i = 0; i < new_smt_num; i++)
		{
			insert_lsmTree(ssd, subSmt[j], dstLevel, overlap_bgn_offset + i, req);
			j++;
		}
		free(subSmt);
		return maintain_manifest_of_level(T, dstLevel);
	}
	else
	{ //数目增多的情况
		if (T->level[dstLevel].fullFlag == 1)
		{
			unsigned int level_overlap;
			if (dstLevel != T->levelNum - 1 && !(T->level[dstLevel + 1].manifest[0] == 0 && T->level[dstLevel + 1].manifest[1] == 0)) {
				level_overlap = T->level[dstLevel].manifest[1] - T->level[dstLevel + 1].manifest[0];
			}
			else {
				level_overlap = 0;
			}
			float overlap_rate = (float)level_overlap / (T->level[dstLevel].manifest[1] - T->level[dstLevel].manifest[0]);
			if (overlap_rate < INNER_COMPACETION_THRESHOLD) {
				//不触发inner compaction
				int minRangeSmt = 0;
				int minRange = subSmt[0]->manifest[1] - subSmt[0]->manifest[0];
				for (int i = 1; i < overlap_length; i++)
				{
					if (subSmt[i]->manifest[1] - subSmt[i]->manifest[0] < minRange)
					{
						minRangeSmt = i;
					}
				}
				int j = 0;
				for (int i = 0; i < overlap_length; i++)
				{
					if (minRangeSmt == j)
					{
						j++;
					}
					insert_lsmTree(ssd, subSmt[j], dstLevel, overlap_bgn_offset + i, req);
					j++;
				}
				maintain_manifest_of_level(T, dstLevel);
				int rtn = compaction(ssd, subSmt[minRangeSmt], dstLevel + 1, req);
				free(subSmt);
				return rtn;
			}
			else {
				//触发
				if (overlap_bgn_offset + overlap_length == T->smtNums[dstLevel]) {
					for (int i = 0; i < overlap_length; i++)
					{
						insert_lsmTree(ssd, subSmt[i], dstLevel, overlap_bgn_offset + i, req);
					}
					maintain_manifest_of_level(T, dstLevel);
					int rtn = compaction(ssd, subSmt[new_smt_num - 1], dstLevel + 1, req);
					free(subSmt);
					return rtn;
				}
				else {
					compaction(ssd, T->level[dstLevel].smtArray[T->smtNums[dstLevel] - 1], dstLevel + 1, req);
					SMT_array_shift(T->level[dstLevel].smtArray, 1, T->level[dstLevel].usedSmt, overlap_bgn_offset, 1);
					for (int i = 0; i < overlap_length + 1; i++)
					{
						insert_lsmTree(ssd, subSmt[i], dstLevel, overlap_bgn_offset + i, req);
					}
					return maintain_manifest_of_level(T, dstLevel);
				}
			}
		}
		else
		{
			SMT_array_shift(T->level[dstLevel].smtArray, 1, T->level[dstLevel].usedSmt + 1, overlap_bgn_offset + overlap_length, 1);
			int j = 0;
			for (int i = overlap_bgn_offset; i < overlap_bgn_offset + overlap_length + 1; i++)
			{
				insert_lsmTree(ssd, subSmt[j++], dstLevel, i, req);
			}
			T->level[dstLevel].usedSmt++;
			free(subSmt);
			return maintain_manifest_of_level(T, dstLevel);
		}
	}

}

/**
 * @brief 在一个层中删除smt,free some memory，需要底层接口，未完成
 *
 * @param T 树的根节点
 * @param level level号
 * @param smtPointer smt号
 * @return int return 1 if success, 0 otherwise.
 */
void free_smt(struct ssd_info* ssd, struct SMT* s)
{
	struct lsmTreeNode* T = ssd->lsmTree;
	unsigned int ppn;
	unsigned int channel, chip, die, plane, block, page;
	struct local* loc;
	unsigned int i, page_type,j;
	unsigned int invalid_data;
	//invalidate old ppn 

	ppn = s->ppn;
	if (ppn == -1)  // in command buffer or in sub request queue 
	{
		s->delay_free_flag = 1;
		for (i = 0; i < ssd->dram->lsm_tree_mapping_command_buf->smt_count; i++)
		{
			if (ssd->dram->lsm_tree_mapping_command_buf->smts[i]->smt_id == s->smt_id) // in command buffer 
			{
				T->write_count--;
				for (j = i + 1; j < ssd->dram->lsm_tree_mapping_command_buf->smt_count; j++)
				{
					ssd->dram->lsm_tree_mapping_command_buf->smts[j - 1] = ssd->dram->lsm_tree_mapping_command_buf->smts[j];
				}
				ssd->dram->lsm_tree_mapping_command_buf->smts[j] = NULL;
				ssd->dram->lsm_tree_mapping_command_buf->smt_count--;
				s->delay_free_flag = 0;
				break;
			}
		}
		if (s->layer_zero_flag == 1)
			s->delay_free_flag = 0;
	}
	else {  // in flash 
		s->delay_free_flag  = 0;
		loc = find_location(ssd, ppn);
		channel = loc->channel;
		chip = loc->chip;
		die = loc->die;
		plane = loc->plane;
		block = loc->block;
		page = loc->page;
		page_type = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].type;
		
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state = 0;
		for (i = 0; i < ssd->parameter->subpage_page; i++)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lun_state[i] = 0;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].luns[i] = -1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_subpage_num++;
		}
		invalid_data = Get_invalid_data(ssd, channel, chip, die, plane, block);

		if (invalid_data != ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_subpage_num)
			printf("Error: Invalid data maintain error\n");
	}

	if (s->dataBlockTable->table != NULL)
	{
		//free table
		for (int i = 0; i < T->dataTableItems; i++)
		{
			free(s->dataBlockTable->table[i]);
			s->dataBlockTable->table[i] = NULL;
		}
		free(s->dataBlockTable->table);
		s->dataBlockTable->table = NULL;
	}

	if (s->delay_free_flag != 1)
	{
		//free table structure
		free(s->dataBlockTable);
		s->dataBlockTable = NULL;
		//free smt
		free(s);
		s = NULL;
		ssd->smt_invalid_count++;

	}
}

/**
 * @brief 找到最后的ppn
 *
 * @param T
 * @param lpn
 * @return Addr ppn
 */
int search_lpn(struct ssd_info* ssd, struct sub_request* sub, unsigned int lpn,unsigned int flag)
{
	struct lsmTreeNode* T;
	T = ssd->lsmTree;
	int status = SUCCESS;
	struct sub_request* sub_read;
	struct request* req;
	unsigned int max_read_count,i;
	unsigned int read_status = 0;
	if (T->buffer_array != NULL && T->buffer_length != 0) {
		for (int i = 0; i < T->buffer_length; i++)
		{
			if (lpn >= T->buffer_array[i]->manifest[0] && lpn <= T->buffer_array[i]->manifest[1]) {
				int ans = biSearch(T, T->buffer_array[i], lpn);
				fprintf(ssd->read_req, "i = %-3d , lpn  %-10u , manifest %-10u , %-10u \n",i,lpn , T->buffer_array[i]->manifest[0],T->buffer_array[i]->manifest[1]);
				fflush(ssd->read_req);
				if (ans != -1)
				{
					return ans;
				}
			}
		}
	}

	if (sub == NULL)
	{
		max_read_count = T->levelNum + L0_SMT_CNT - 1;

		for (int i = 0; i < T->levelNum; i++)
		{
			if (lpn >= T->level[i].manifest[0] && lpn <= T->level[i].manifest[1])
			{
				for (int j = 0; j < T->level[i].usedSmt; j++)
				{
					if (lpn >= T->level[i].smtArray[j]->manifest[0] && lpn <= T->level[i].smtArray[j]->manifest[1])
					{
						//create a read sub request
						if (T->level[i].smtArray[j]->ppn != -1) // i
						{
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

							switch (flag)
							{
							case READ:     //for getting the new address
								sub_read->read_flag = LSM_TREE_READ;
								sub->tran_read[sub->tran_read_count++] = sub;
								break;
							case WRITE:   //for invalidating old ppn
								sub_read->read_flag = INVALIDATE_READ;

								break;
							default:
								break;
							}
							sub_read->lpn = T->level[i].smtArray[j]->smt_id;
							sub_read->ppn = T->level[i].smtArray[j]->ppn;
							sub_read->location = find_location(ssd, sub_read->ppn);
							creat_one_read_sub_req(ssd, sub_read); //read the corresponding mapping entry and find corresponding pun of old lun
						}

						T->level[i].read_count += 1;
						T->read_count += 1;
						int ans = biSearch(T, T->level[i].smtArray[j], lpn);
						if (ans != -1)
						{
							return ans;
						}
					}
				}
			}
		}
	}
	else
	{
		T = ssd->lsmTree;
		max_read_count = T->levelNum + L0_SMT_CNT - 1;
		sub->tran_read = (struct sub_reuqest**)malloc(sizeof(struct sub_request*) * max_read_count);
		sub->tran_read_count = 0;
		for (i = 0; i < max_read_count; i++)
		{
			sub->tran_read[i] = NULL;
		}

		for (int i = 0; i < T->levelNum; i++)
		{
			if (lpn >= T->level[i].manifest[0] && lpn <= T->level[i].manifest[1])
			{
				for (int j = 0; j < T->level[i].usedSmt; j++)
				{
					if (lpn >= T->level[i].smtArray[j]->manifest[0] && lpn <= T->level[i].smtArray[j]->manifest[1])
					{
						//create a read sub request
						if (T->level[i].smtArray[j]->ppn != -1)
						{
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

							switch (flag)
							{
							case READ:     //for getting the new address
								sub_read->read_flag = LSM_TREE_READ;
								sub->tran_read[sub->tran_read_count++] = sub_read;
								break;
							case WRITE:   //for invalidating old ppn
								sub_read->read_flag = INVALIDATE_READ;
								break;
							default:
								break;
							}
							sub_read->lpn = T->level[i].smtArray[j]->smt_id;
							sub_read->ppn = T->level[i].smtArray[j]->ppn;
							sub_read->location = find_location(ssd, sub_read->ppn);
							creat_one_read_sub_req(ssd, sub_read); //read the corresponding mapping entry and find corresponding pun of old lun
						}

						T->level[i].read_count += 1;
						T->read_count += 1;
						int ans = biSearch(T, T->level[i].smtArray[j], lpn);
						if (ans != -1)
						{
							if (sub->tran_read == NULL)
								free(sub->tran_read);
							return ans;
						}
					}
				}
			}
		}
	}

	printf("Error! Lsm-Tree LPN not Found\n");
	getchar();
	return FAILURE;
}


/**
 * @brief 二分查找到lpn
 *
 * @param s
 * @param lpn
 * @return Addr ppn if success, -1 otherwise
 */
Addr biSearch(struct lsmTreeNode* T, struct SMT* s, Addr lpn)
{
	//********** read SMT s  create a read request

	//s->read_count += 1;
	if (s->dataBlockTable == NULL || s->dataBlockTable->table == NULL)
		return -1;
	int bgn = 0;
	int end = T->dataTableItems - 1;
	int mid = (bgn + end) / 2;
	while (bgn <= end)
	{
		mid = (bgn + end) / 2;
		if (s->dataBlockTable->table[mid][0] == -1)
		{
			end = mid - 1;
			continue;
		}
		if (s->dataBlockTable->table[mid][0] > lpn)
		{
			end = mid - 1;
			continue;
		}
		if (s->dataBlockTable->table[mid][0] < lpn)
		{
			bgn = mid + 1;
			continue;
		}
		if (s->dataBlockTable->table[mid][0] == lpn)
		{
			s->hit_times += 1;
			return s->dataBlockTable->table[mid][1];
		}
	}
	return -1;
}

void printTree(struct ssd_info* ssd, struct lsmTreeNode* T)
{
	fprintf(ssd->smt_info, "\n----------------------------------Print Bgn----------------------------------\n");
	for (int i = 0; i < T->levelNum; i++)
	{
		if (T->level[i].usedSmt == 0)
			break;
		fprintf(ssd->smt_info, "\n|------------------Level %d--------------------|\n", i);
		fprintf(ssd->smt_info, "manifest:%u ~ %u", T->level[i].manifest[0], T->level[i].manifest[1]);
		for (int j = 0; j < T->smtNums[i]; j++)
		{
			if (j % 2 == 0)
			{
				fprintf(ssd->smt_info, "\n");
			}
			if (j == T->level[i].usedSmt)
				break;
			fprintf(ssd->smt_info, "smt(%-3d) : %-10d , %-10d, %-10d |", j, T->level[i].smtArray[j]->smt_id, T->level[i].smtArray[j]->allocated_ppn_flag, T->level[i].smtArray[j]->ppn);
		}
	}
	fprintf(ssd->smt_info, "\n");
	fprintf(ssd->smt_info, "----------------------------------Print End----------------------------------\n");
	fflush(ssd->smt_info);
}

int request_new_ppn(struct ssd_info* ssd, struct SMT* s, struct request* req, unsigned int flag)
{
	struct sub_request* sub;
	unsigned int i, j, offset;

	s->ppn = -1;
	if(flag == 0)
		s->smt_id = ssd->smt_id++;
	//s->read_count = 0;
	s->hit_times = 0;
	offset = ssd->dram->lsm_tree_mapping_command_buf->smt_count;
	ssd->dram->lsm_tree_mapping_command_buf->smts[offset++] = s;
	ssd->dram->lsm_tree_mapping_command_buf->smt_count = offset;

	if (offset == ssd->dram->lsm_tree_mapping_command_buf->max_smt_count)
	{
		for (j = 0; j < ssd->dram->lsm_tree_mapping_command_buf->max_smt_count; j++)
		{
			sub = (struct sub_request*)malloc(sizeof(struct sub_request));
			sub->smt = ssd->dram->lsm_tree_mapping_command_buf->smts[j];
			sub->lpn = ssd->dram->lsm_tree_mapping_command_buf->smts[j]->smt_id;
			if (sub->smt->smt_id <= 0 || sub->smt->smt_id > ssd->smt_id)
			{
				printf("ERROR! Invalid SMT ID\n");
			}
			if (sub->smt->ppn != -1)
			{
				printf("ERROR! Invalid SMT ID\n");
			}

			sub->size = secno_num_per_page;
			sub->state = 99999;

			sub->req_type = MAPPING_DATA;
			sub->tran_read = NULL;
			sub->gc_flag = 0;
			sub->lun_count = 0;
			sub->next_node = NULL;
			create_sub_w_req(ssd, sub, req, MAPPING_DATA);  //maintain Lsm-Tree mapping table in the way of user data 
			ssd->dram->lsm_tree_mapping_command_buf->smt_count--;
			ssd->dram->lsm_tree_mapping_command_buf->smts[j] = NULL;
		}
		if (ssd->dram->lsm_tree_mapping_command_buf->smt_count != 0)
		{
			printf("Look Here: Error!\n");
			getchar();
		}
	}

	if (ssd->open_sb[MAPPING_DATA] == NULL)  //mapping block
		find_active_superblock(ssd, req, MAPPING_DATA);

	if (ssd->open_sb[MAPPING_DATA]->next_wr_page == ssd->parameter->page_block) //no free superpage in the superblock
		find_active_superblock(ssd, req, MAPPING_DATA);

	return SUCCESS;
}

/**
 * @brief 判断是否有新的smt和level中的smt是否有overlap。
 *
 * @param T
 * @param level
 * @param s
 * @param length
 * @return int
 */
int is_overlap(struct lsmTreeNode* T, struct SMT* s, int dstLevel, int* length)
{
	if (T->level[dstLevel].usedSmt == 0)
	{
		*length = 0;
		return -1;
	}
	int* SmtReadFlag = (int*)malloc(sizeof(int) * T->smtNums[dstLevel]); //标记需要被读取的smt
	for (int i = 0; i < T->smtNums[dstLevel]; i++)
	{
		SmtReadFlag[i] = 0;
	}

	//找到要被读取的smt
	for (int i = 0; i < T->level[dstLevel].usedSmt; i++)
	{
		if (T->level[dstLevel].smtArray[i]->manifest[0] >= s->manifest[0] &&
			T->level[dstLevel].smtArray[i]->manifest[0] <= s->manifest[1])
		{
			SmtReadFlag[i] = 1;
		}
		else if (T->level[dstLevel].smtArray[i]->manifest[1] >= s->manifest[0] &&
			T->level[dstLevel].smtArray[i]->manifest[1] <= s->manifest[1])
		{
			SmtReadFlag[i] = 1;
		}
		else if (T->level[dstLevel].smtArray[i]->manifest[1] >= s->manifest[1] &&
			T->level[dstLevel].smtArray[i]->manifest[0] <= s->manifest[0])
		{
			SmtReadFlag[i] = 1;
		}
		else if (T->level[dstLevel].smtArray[i]->manifest[1] <= s->manifest[1] &&
			T->level[dstLevel].smtArray[i]->manifest[0] >= s->manifest[0])
		{
			SmtReadFlag[i] = 1;
		}
	}

	int readSmtNum = 0; //需要读的smt的数量
	int bgn = T->smtNums[dstLevel];
	//将需要被读取的smt存到一个指针数组中
	//并删除该smt
	for (int i = 0; i < T->smtNums[dstLevel]; i++)
	{
		if (SmtReadFlag[i] >= 1)
		{
			readSmtNum++;
			if (bgn > i)
			{
				bgn = i;
			}
		}
	}
	*length = readSmtNum;
	free(SmtReadFlag);
	return bgn;
}

/**
 * @brief
 *
 * @param T
 * @param s
 * @param dstLevel
 * @param offset
 * @param length
 * @return SMT*
 */
struct SMT** merge_smt(struct ssd_info* ssd, struct SMT* s, int dstLevel, int offset, int length, int* SMT_num, struct request* req)
{
	Addr readingLpn = 0;
	int readSmt = 0;            //已经被读完的smt
	int readNewSmtTableNum = 0; //compaction来的smt中已经被读取的地址
	int newSmtNum = 0;
	int countSmtAddrs = 0; //记录一个SMT中，已经被copy的条目数
	int pointer = 0;       //记录新的表中拷贝的条目位置
	struct lsmTreeNode *T = ssd->lsmTree;

	//生成读请求
	for (int i = 0; i < length; i++) {
		read_smt(ssd, T->level[dstLevel].smtArray[i + offset], req);
	}

	//暂存所有映射条目的表，完成后，直接以在新的smt中以执政的形式索引到表中的内容，无需释放
	Addr** readTablelist = (Addr * *)malloc(sizeof(Addr*) * (T->dataTableItems) * (length + 1));
	//将所有的数据读到一个映射表中
	int minSmt = 0;
	while (readSmt < length) //
	{
		if (countSmtAddrs >= T->dataTableItems)
		{
			readSmt++;
			countSmtAddrs = 0;
		}
		else if (T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0] == -1)
		{
			readSmt++;
			countSmtAddrs = 0;
		}
		else if (readNewSmtTableNum >= T->dataTableItems)
		{
			readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2);
			readTablelist[pointer][0] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0];
			readTablelist[pointer][1] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][1];
			countSmtAddrs++;
			pointer++;
		}
		else if (s->dataBlockTable->table[readNewSmtTableNum][0] == -1)
		{
			//新的compaction 的 smt已经被读完的情况
			readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2);
			readTablelist[pointer][0] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0];
			readTablelist[pointer][1] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][1];
			countSmtAddrs++;
			pointer++;
		}
		//read exist table
		//新smt的lpn小于旧的smt的情况
		else if (s->dataBlockTable->table[readNewSmtTableNum][0] < T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0])
		{
			readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2); //前两个记录 lpn和ppn，后两个分别记录它的命中率和是否写到下一层
			readTablelist[pointer][0] = s->dataBlockTable->table[readNewSmtTableNum][0];
			readTablelist[pointer][1] = s->dataBlockTable->table[readNewSmtTableNum][1];
			pointer++;
			readNewSmtTableNum++;
		}
		//新smt的lpn等于旧的smt的情况
		else if (s->dataBlockTable->table[readNewSmtTableNum][0] == T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0])
		{
			readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2);
			readTablelist[pointer][0] = s->dataBlockTable->table[readNewSmtTableNum][0];
			readTablelist[pointer][1] = s->dataBlockTable->table[readNewSmtTableNum][1];
			pointer++;
			countSmtAddrs++;
			readNewSmtTableNum++;
		}
		//新smt的lpn大于旧的smt的情况
		else
		{
			readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2);
			readTablelist[pointer][0] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][0];
			readTablelist[pointer][1] = T->level[dstLevel].smtArray[readSmt + offset]->dataBlockTable->table[countSmtAddrs][1];
			countSmtAddrs++;
			pointer++;
		}
	}
	//处理新的smt中没有写完的情况
	while (readNewSmtTableNum < T->dataTableItems && s->dataBlockTable->table[readNewSmtTableNum][0] != -1)
	{
		readTablelist[pointer] = (Addr*)malloc(sizeof(Addr) * 2);
		readTablelist[pointer][0] = s->dataBlockTable->table[readNewSmtTableNum][0];
		readTablelist[pointer][1] = s->dataBlockTable->table[readNewSmtTableNum][1];
		pointer++;
		readNewSmtTableNum++;
	}
	int tableLength = pointer; //新表的总长度
	//不产生多的smt的情况，即读出来两个smt，compaction后仍然只剩下两个smt

	int newSmtPointer = 0;
	struct SMT** subSmt = (struct SMT * *)malloc(sizeof(struct SMT*) * (length + 1));
	//请求新地址
	subSmt[0] = (struct SMT*)malloc(sizeof(struct SMT));
	subSmt[0]->ppn = -1;
	subSmt[0]->dataBlockTable = (struct dataBlock*)malloc(sizeof(struct dataBlock));
	subSmt[0]->dataBlockTable->table = (Addr * *)malloc(sizeof(Addr*) * T->dataTableItems);
	subSmt[0]->smt_id = -1;
	subSmt[0]->allocated_ppn_flag = 0;
	readSmt = 0;
	//拷贝数据
	for (int i = 0; i < tableLength; i++)
	{
		if (newSmtPointer == T->dataTableItems)
		{
			subSmt[readSmt]->manifest[0] = subSmt[readSmt]->dataBlockTable->table[0][0];
			subSmt[readSmt]->manifest[1] = subSmt[readSmt]->dataBlockTable->table[T->dataTableItems - 1][0];
			newSmtPointer = 0;
			readSmt++;
			subSmt[readSmt] = (struct SMT*)malloc(sizeof(struct SMT));
			subSmt[readSmt]->dataBlockTable = (struct dataBlock*)malloc(sizeof(struct dataBlock));
			subSmt[readSmt]->dataBlockTable->table = (Addr * *)malloc(sizeof(Addr*) * T->dataTableItems);
			subSmt[readSmt]->ppn = -1;
			subSmt[readSmt]->smt_id = -1;
			subSmt[readSmt]->allocated_ppn_flag = 0;
		}
		subSmt[readSmt]->dataBlockTable->table[newSmtPointer++] = readTablelist[i];
	}

	//处理最后一个没写完的
	subSmt[readSmt]->manifest[0] = subSmt[readSmt]->dataBlockTable->table[0][0];
	subSmt[readSmt]->manifest[1] = subSmt[readSmt]->dataBlockTable->table[newSmtPointer - 1][0];
	for (int i = newSmtPointer; i < T->dataTableItems; i++)
	{
		subSmt[readSmt]->dataBlockTable->table[i] = (Addr*)malloc(sizeof(Addr) * 2);
		subSmt[readSmt]->dataBlockTable->table[i][0] = -1;
		subSmt[readSmt]->dataBlockTable->table[i][0] = -1;
	}
	*SMT_num = readSmt + 1;
	free(readTablelist);
	return subSmt;
}

/**
 * @brief
 *
 * @param T
 * @param s
 * @param level
 * @param offset
 * @return int
 */
int insert_lsmTree(struct ssd_info* ssd, struct SMT* s, int level, int offset, struct request* req)
{
	struct lsmTreeNode* T;
	T = ssd->lsmTree;

	if (s->allocated_ppn_flag == 0)
	{
		//inser to command buffer  write request (SMT *s);
		s->allocated_ppn_flag = 1;
		s->layer_zero_flag = 0;
		request_new_ppn(ssd, s, req,0);
		T->write_count++;
	}
	// s->ppn = request_new_ppn(1);
	T->level[level].smtArray[offset] = s;
	return 1;
}

/**
 * @brief
 *
 * @param T
 * @param level
 * @return int
 */
int maintain_manifest_of_level(struct lsmTreeNode* T, int dstLevel)
{
	T->buffer_array = NULL;
	T->buffer_length = 0;
	if (T->level[dstLevel].usedSmt == T->smtNums[dstLevel])
	{
		T->level[dstLevel].fullFlag = 1;
	}
	T->level[dstLevel].manifest[0] = T->level[dstLevel].smtArray[0]->manifest[0];
	T->level[dstLevel].manifest[1] = T->level[dstLevel].smtArray[T->level[dstLevel].usedSmt - 1]->manifest[1];
	return dstLevel;
}

/**
 * @brief
 *
 * @param T
 * @param s
 * @param dstLevel
 * @return int
 */
int handle_no_overlap(struct ssd_info* ssd, struct SMT* s, int dstLevel, struct request* req)
{
	struct lsmTreeNode* T;
	T = ssd->lsmTree;

	//S的最小值大于level的最大值
	if (s->manifest[0] > T->level[dstLevel].manifest[1])
	{
		if (T->level[dstLevel].fullFlag == 0)
		{
			//T->level[dstLevel].smtArray[T->level[dstLevel].usedSmt++] = s;
			insert_lsmTree(ssd, s, dstLevel, T->level[dstLevel].usedSmt++, req);
			if (T->level[dstLevel].usedSmt == T->smtNums[dstLevel])
			{
				T->level[dstLevel].fullFlag = 1;
			}
			return maintain_manifest_of_level(T, dstLevel);
		}
		else
		{
			return compaction(ssd, s, dstLevel + 1, req);
		}
	}
	//s的最小值下雨level的最小值且level被写满
	else if (s->manifest[0] < T->level[dstLevel].manifest[0])
	{
		if (T->level[dstLevel].fullFlag == 1)
		{
			return compaction(ssd, s, dstLevel + 1, req);
		}
	}

	//level为空的
	if (T->level[dstLevel].usedSmt == 0)
	{
		//T->level[dstLevel].smtArray[0] = s;
		insert_lsmTree(ssd, s, dstLevel, 0, req);
		T->level[dstLevel].usedSmt += 1;
		return maintain_manifest_of_level(T, dstLevel);
	}

	for (int i = 0; i < T->smtNums[dstLevel]; i++)
	{
		//找到应该被插入的地方
		if (s->manifest[1] < T->level[dstLevel].smtArray[i]->manifest[0])
		{
		if (T->level[dstLevel].fullFlag == 1)
			{
				compaction(ssd, s, dstLevel + 1, req);
				//T->level[dstLevel].smtArray[i] = s;
			}
			else
			{
				// SMT *temp1 = T->level[dstLevel].smtArray[i];
				// SMT *temp2 = T->level[dstLevel].smtArray[i + 1];
				// for (int k = i; k < T->level[dstLevel].usedSmt; k++)
				// {
				//     temp2 = T->level[dstLevel].smtArray[k + 1];
				//     T->level[dstLevel].smtArray[k + 1] = temp1;
				//     temp1 = temp2;
				// }
				SMT_array_shift(T->level[dstLevel].smtArray, 1, T->level[dstLevel].usedSmt + 1, i, 1);
				//T->level[dstLevel].smtArray[i] = s;
				insert_lsmTree(ssd, s, dstLevel, i, req);
				T->level[dstLevel].usedSmt++;
			}
			// if (T->level[dstLevel].usedSmt == T->smtNums[dstLevel])
			// {
			//     T->level[dstLevel].fullFlag = 1;
			// }
			// T->level[dstLevel].manifest[0] = T->level[dstLevel].smtArray[0]->manifest[0];
			// T->level[dstLevel].manifest[1] = T->level[dstLevel].smtArray[T->level[dstLevel].usedSmt - 1]->manifest[1];
			return maintain_manifest_of_level(T, dstLevel);
		}
	}
	return 0;
}

/**
 * @brief
 *
 * @param array
 * @param left_right
 * @param array_size
 * @param shift_offset
 * @param shift_num
 */
void SMT_array_shift(struct SMT** array, int left_right, int array_size, int shift_offset, int shift_num)
{
	struct SMT** tempArray;
	if (left_right == -1) //left shift
	{
		tempArray = (struct SMT * *)malloc(sizeof(struct SMT*) * array_size);
		int j = 0;
		for (int i = 0; i < array_size; i++)
		{
			if (i == shift_offset)
			{
				j -= shift_num;
			}
			tempArray[j++] = array[i];
		}
		for (int i = 0; i < shift_num; i++)
		{
			tempArray[array_size - i] = NULL;
		}
		for (int i = 0; i < array_size; i++)
		{
			array[i] = tempArray[i];
		}
		free(tempArray);
	}
	else if (left_right == 1) //right shift
	{
		tempArray = (struct SMT * *)malloc(sizeof(struct SMT*) * (array_size + shift_num));
		int j = 0;
		for (int i = 0; i < array_size; i++)
		{
			if (i == shift_offset)
			{
				for (int k = 0; k < shift_num; k++)
				{
					tempArray[j + k] = NULL;
				}
				j += shift_num;
			}
			tempArray[j++] = array[i];
		}
		for (int i = 0; i < array_size; i++)
		{
			array[i] = tempArray[i];
		}
		free(tempArray);
	}
	else
	{
		printf("Error! Unrecongnized value of left_rigth. left shift: -1, right shift: 1!\n");
	}
}

/**
 * @brief
 *
 * @param ssd
 * @param s
 * @param req
 * @return int
 */
int read_smt(struct ssd_info* ssd, struct SMT* s, struct request* req)
{
	struct sub_request* sub_read;

	if (s->ppn == -1) //smt may be in the buffer or sub request queue 
	{
		return SUCCESS;
	}
	//create a read sub request
	sub_read = (struct sub_request*)malloc(sizeof(struct sub_request));
	read_request_size += sizeof(struct sub_request);
	if (sub_read == NULL)
	{
		printf("MALLOC ERROR\N");
		return FAILURE;
	}
	sub_read->next_node = NULL;
	sub_read->next_subs = NULL;
	sub_read->update_cnt = 0;
	sub_read->tran_read = NULL;
	sub_read->next_subs = req->subs;
	req->subs = sub_read;
	sub_read->total_request = req;
	sub_read->read_flag = LSM_TREE_READ;
	sub_read->lpn = s->smt_id;

	sub_read->ppn = s->ppn;
	sub_read->location = find_location(ssd, sub_read->ppn);
	creat_one_read_sub_req(ssd, sub_read);
	return SUCCESS;
}


int check_smt(struct ssd_info* ssd, struct SMT* s)
{
	struct lsmTreeNode* T;
	T = ssd->lsmTree;
	int i = 0;
	while (s->dataBlockTable->table[i][0] != -1 && i < T->dataTableItems - 1)
	{
		if (s->dataBlockTable->table[i][0] > s->dataBlockTable->table[i + 1][0])
		{
			return FAILURE;
		}
		i++;
	}
	return TRUE;
}