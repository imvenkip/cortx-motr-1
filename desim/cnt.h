/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifndef CNT_H
#define CNT_H

#include "lib/list.h"

typedef unsigned long long cnt_t;
 
struct cnt {
	cnt_t               c_sum;
	cnt_t               c_min;
	cnt_t               c_max;
	cnt_t               c_nr;
	double              c_sq;
	char               *c_name;
	struct c2_list_link c_linkage;
	struct cnt         *c_parent;
};

void cnt_init(struct cnt *cnt, struct cnt *parent, const char *name, ...) 
              __attribute__((format(printf, 3, 4)));
void cnt_fini(struct cnt *cnt);
void cnt_dump(struct cnt *cnt);
void cnt_dump_all(void);

void cnt_mod(struct cnt *cnt, cnt_t val);

void cnt_global_init(void);
void cnt_global_fini(void);

#endif /* CNT_H */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
