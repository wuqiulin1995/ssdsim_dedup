#pragma once

#include "initialize.h"

Status erase_operation(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block);
int NAND_program(struct ssd_info *ssd, struct sub_request * req);
int NAND_read(struct ssd_info* ssd, struct sub_request* req);
int NAND_multi_plane_program(struct ssd_info* ssd, struct sub_request* req0, struct sub_request* req1);
int NAND_multi_plane_read(struct ssd_info* ssd, struct sub_request* req0, struct sub_request* req1);