#pragma once
#include "initialize.h"

void initialize_lsm(struct ssd_info* ssd);
int insertToL0(struct ssd_info* ssd, struct SMT* s, struct request* req);
int compaction(struct ssd_info* ssd, struct SMT* s, int dstLevel, struct request* req);
int search_lpn(struct ssd_info* ssd, struct sub_request* sub, unsigned int lpn,unsigned int flag);
Addr biSearch(struct lsmTreeNode* T, struct SMT* s, Addr lpn);
void printTree(struct ssd_info* ssd, struct lsmTreeNode* T);
int request_new_ppn(struct ssd_info* ssd, struct SMT* s, struct request* req, unsigned int flag);
int is_overlap(struct lsmTreeNode* T, struct SMT* s, int dstLevel, int* length);
struct SMT** merge_smt(struct ssd_info* ssd, struct SMT* s, int dstLevel, int offset, int length, int* SMT_num, struct request* req);
int insert_lsmTree(struct ssd_info* ssd, struct SMT* s, int level, int offset, struct request* req);
int maintain_manifest_of_level(struct lsmTreeNode* T, int dstLevel);
int handle_no_overlap(struct ssd_info* ssd, struct SMT* s, int dstLevel, struct request* req);
int read_smt(struct ssd_info* ssd, struct SMT* s, struct request* req);
void free_smt(struct ssd_info *ssd, struct SMT *s);
void SMT_array_shift(struct SMT** array, int left_right, int array_size, int shift_offset, int shift_num);
int check_smt(struct ssd_info* ssd, struct SMT* s);