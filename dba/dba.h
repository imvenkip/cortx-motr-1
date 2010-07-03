/* -*- C -*- */

#ifndef __COLIBRI_DBA_DBA_H__
#define __COLIBRI_DBA_DBA_H__

#include <db.h>

struct c2_dba_ctxt {
	DB_ENV         *dc_dbenv;
	char	       *dc_home;

        u_int32_t       dc_dbenv_flags;
        u_int32_t  	dc_db_flags;
        u_int32_t  	dc_txn_flags;
        u_int32_t  	dc_cache_size;
        u_int32_t  	dc_nr_thread;

	DB            *dc_group_extent;
	DB            *dc_group_info;
};

#define MAXPATHLEN 1024

typedef u_int64_t c2_blockno_t;
typedef u_int32_t c2_blockcount_t;


struct c2_dba_allocate_req {
	c2_blockno_t	dar_logical;
	c2_blockcount_t	dar_lcount;
	c2_blockno_t	dar_goal;
	u_int32_t		dar_flags;

	c2_blockno_t		dar_physical;  /* result allocated blocks */

	u_int32_t		dar_err;
	c2_blockno_t		dar_max_avail; /* max avail blocks */
};

void cpube(void *place, uint64_t val)
{
	*(uint64_t *)place = val;

	char *area = place;
	int i;

	for (i = 0; i < 8; ++i)
		area[i] = val >> (64 - (i + 1)*8);
}

uint64_t becpu(void *place)
{
	char *area = place;
	int i;
	uint64_t out;

	for (out = 0, i = 0; i < 8; ++i)
		out = (out << 8) | area[i];
	return out;
}



#endif /*__COLIBRI_DBA_DBA_H__*/
