#pragma once

struct ssd_info *warm_flash(struct ssd_info *ssd);
struct ssd_info *simulate(struct ssd_info *ssd);
void tracefile_sim(struct ssd_info *ssd);
struct ssd_info *warm_flash(struct ssd_info *ssd);
void reset(struct ssd_info *ssd);
void trace_output(struct ssd_info *ssd);
void statistic_output(struct ssd_info *ssd);
void free_all_node(struct ssd_info *ssd);
void alloc_assert(void *p, char *s);