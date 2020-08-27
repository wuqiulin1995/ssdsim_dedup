#pragma once

#include "initialize.h"

struct ssd_info* buffer_management(struct ssd_info *);
struct ssd_info* no_buffer_distribute(struct ssd_info *);
struct ssd_info* check_w_buff(struct ssd_info *ssd, unsigned int lpn, int state, struct request *req);
struct ssd_info* insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req);
struct ssd_info* insert2_read_buffer(struct ssd_info* ssd, unsigned int lpn, int state);
struct ssd_info* insert2_command_buffer(struct ssd_info* ssd, struct buffer_info* command_buffer, unsigned int lpn, unsigned int state, struct request* req);

Status create_sub_w_req(struct ssd_info* ssd, struct sub_request* sub, struct request* req);

unsigned int size(unsigned int);

struct ssd_info *handle_write_buffer(struct ssd_info *ssd, struct request *req);

Status creat_one_read_sub_req(struct ssd_info* ssd, struct sub_request* sub);
Status read_reqeust(struct ssd_info* ssd, unsigned int lpn, struct request* req, unsigned int state);

struct ssd_info* create_new_mapping_buffer(struct ssd_info* ssd, unsigned int lpn, struct request* req, unsigned int flag);

unsigned int translate(struct ssd_info* ssd, unsigned int lpn, struct sub_request* sub);
int get_cached_map_entry_cnt(struct ssd_info* ssd);

struct ssd_info *handle_new_request(struct ssd_info *ssd);
Status handle_read_request(struct ssd_info *ssd, struct request *req);
Status handle_write_request(struct ssd_info *ssd, struct request *req);