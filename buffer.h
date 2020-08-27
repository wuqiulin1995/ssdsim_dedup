#pragma once

#include "initialize.h"

struct ssd_info *handle_new_request(struct ssd_info *ssd);
Status handle_read_request(struct ssd_info *ssd, struct request *req);
Status handle_write_request(struct ssd_info *ssd, struct request *req);