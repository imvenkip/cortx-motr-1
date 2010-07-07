/* -*- C -*- */

#ifndef __COLIBRI_DBA_DBA_H__
#define __COLIBRI_DBA_DBA_H__

#include <db.h>
#include <lib/list.h>
#include <lib/mutex.h>

#define MAXPATHLEN 1024

typedef u_int64_t c2_blockno_t;
typedef u_int32_t c2_blockcount_t;
typedef u_int64_t c2_groupno_t;


struct c2_dba_ctxt {
	DB_ENV         *dc_dbenv;
	char	       *dc_home;

        u_int32_t       dc_dbenv_flags;
        u_int32_t  	dc_db_flags;
        u_int32_t  	dc_txn_flags;
        u_int32_t  	dc_cache_size;
        u_int32_t  	dc_nr_thread;

	DB             *dc_group_extent;
	DB             *dc_group_info;
};

/**
   On-disk and in-momory data structure for extent

   When stored in db, {key, value} = {start, len}
 */
struct c2_dba_extent {
	c2_blockno_t    ext_start;
	c2_blockcount_t ext_len;
};


/**
   On-disk data structure for group, stored in db.

   When stored in db, {key, value} = {group_nr, c2_dba_group_desc}.
   Every group description will stay in a separate db.
 */
struct c2_dba_group_desc {
        c2_blockcount_t  dgd_freeblocks; /* total free blocks */
        c2_blockcount_t  dgd_fragments;  /* nr of freespace fragments */
        c2_blockcount_t  dgd_maxchunk;   /* max bytes of freespace chunk */
};

/**
   In-memory data structure for group
 */
struct c2_dba_group_info {
        unsigned long       dgi_state;
        c2_blockcount_t     dgi_freeblocks; /* total free blocks */
        c2_blockcount_t     dgi_fragments;  /* nr of freespace fragments */
        c2_blockcount_t     dgi_maxchunk;   /* max bytes of freespace chunk */
        struct c2_list_link dgi_prealloc_list;
        struct c2_mutex     dgi_mutex;
        c2_blockcount_t     dgi_counters[]; /* Nr of free power-of-two-block
                                             * regions, index is order.
                                             * bb_counters[3] = 5 means
                                             * 5 free 8-block regions. */
};

/**
   On-disk and in-memory super block stored in db
 */
struct c2_dba_super_block {
	uint64_t	   dsb_magic;
	uint64_t	   dsb_state;
	uint64_t	   dsb_version;
	uint8_t	   	   dsb_uuid[128];

        c2_blockno_t       dsb_totalsize;  /* total size in bytes */
        c2_blockno_t       dsb_freeblocks; /* nr of free blocks */
        c2_blockcount_t    dsb_blocksize;  /* block size in bytes */
        c2_blockcount_t    dsb_groupsize;  /* group size in blocks */
        c2_blockcount_t    dsb_reserved_groups;  /* nr of reserved groups */
        c2_blockcount_t    dsb_prealloc_count;   /* nr of pre-alloc blocks */

	uint64_t	   dsb_format_time;
	uint64_t	   dsb_write_time;
	uint64_t	   dsb_mnt_time;
	uint64_t	   dsb_last_check_time;

	uint64_t	   dsb_mnt_count;
	uint64_t	   dsb_max_mnt_count;

        c2_blockcount_t    dsb_stripe_size;   /* stripe size in blocks */
};

#define C2_DBA_SB_MAGIC   0xC011B21AC08EC08EULL
#define C2_DBA_SB_CLEAN   (1 << 0)
#define C2_DBA_SB_VERSION 1ULL


struct c2_dba_format_req {
	c2_blockno_t 	dfr_totalsize;	      /* total size in bytes */
	u_int32_t	dfr_blocksize;        /* block size in bytes */
	u_int32_t	dfr_groupsize;        /* block size in blocks */
	u_int32_t	dfr_reserved_groups;  /* # of resvered groups */

	char 	       *dfr_db_home;          /* database home dir */
};

struct c2_dba_allocate_req {
	c2_blockno_t	dar_logical;
	c2_blockcount_t	dar_lcount;
	c2_blockno_t	dar_goal;
	u_int32_t	dar_flags;

	c2_blockno_t	dar_physical;  /* result allocated blocks */

	u_int32_t	dar_err;
	c2_blockno_t	dar_max_avail; /* max avail blocks */
};

struct c2_dba_free_req {
	c2_blockno_t	dfr_logical;
	c2_blockcount_t	dfr_lcount;
	c2_blockno_t	dfr_physical;
	u_int32_t	dfr_flags;
};

static inline void cpube(void *place, uint64_t val)
{
	*(uint64_t *)place = val;

	char *area = place;
	int i;

	for (i = 0; i < 8; ++i)
		area[i] = val >> (64 - (i + 1)*8);
}

static inline uint64_t becpu(void *place)
{
	char *area = place;
	int i;
	uint64_t out;

	for (out = 0, i = 0; i < 8; ++i)
		out = (out << 8) | area[i];
	return out;
}


#define DBA_DEBUG

#ifdef DBA_DEBUG
#define ENTER printf("===>>> %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define LEAVE printf("<<<=== %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define GOTHERE printf("!!! %s:%d:%s\n", __FILE__, __LINE__, __func__)
#else
#define ENTER
#define LEAVE
#define GOTHERE
#endif

#endif /*__COLIBRI_DBA_DBA_H__*/
