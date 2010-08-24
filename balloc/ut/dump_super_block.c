/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* free */
#include <errno.h>   /* errno */
#include <string.h>  /* memset */
#include <err.h>

#include "balloc/balloc.h"


static int c2_balloc_dump_super_block(struct c2_balloc_ctxt *ctxt) 
{
	struct c2_balloc_super_block *sb = &ctxt->bc_sb;

	printf("dumping sb@%p:%p\n"
		"|---------magic=%llx, state=%llu, version=%llu\n"
		"|---------total=%llu, free=%llu, bs=%llu@%lx\n"
		"|---------gs=%llu:@%lx, gc=%llu, rsvd=%llu, prealloc=%llu\n"
		"|---------time format=%llu,\n"
		"|---------write=%llu,\n"
		"|---------mnt=%llu,\n"
		"|---------last_check=%llu\n"
		"|---------mount=%llu, max_mnt=%llu, stripe_size=%llu\n",
		ctxt, sb,
		(unsigned long long) sb->bsb_magic,
		(unsigned long long) sb->bsb_state,
		(unsigned long long) sb->bsb_version,
		(unsigned long long) sb->bsb_totalsize,
		(unsigned long long) sb->bsb_freeblocks,
		(unsigned long long) sb->bsb_blocksize,
		(unsigned long     ) sb->bsb_bsbits,
		(unsigned long long) sb->bsb_groupsize,
		(unsigned long     ) sb->bsb_gsbits,
		(unsigned long long) sb->bsb_groupcount,
		(unsigned long long) sb->bsb_reserved_groups,
		(unsigned long long) sb->bsb_prealloc_count,
		(unsigned long long) sb->bsb_format_time,
		(unsigned long long) sb->bsb_write_time,
		(unsigned long long) sb->bsb_mnt_time,
		(unsigned long long) sb->bsb_last_check_time,
		(unsigned long long) sb->bsb_mnt_count,
		(unsigned long long) sb->bsb_max_mnt_count,
		(unsigned long long) sb->bsb_stripe_size
		);
	return 0;
}

int main(int argc, char **argv)
{
	struct c2_balloc_ctxt         ctxt = {
		.bc_nr_thread = 1,
	};

	int rc;
	char *path;

	if (argc != 2)
		errx(1, "Usage: %s path-to-db-dir", argv[0]);

	path = argv[1];

	ctxt.bc_home = path;
	rc = c2_balloc_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_balloc_init error: %d\n", rc);
		return rc;
	}

	rc = c2_balloc_dump_super_block(&ctxt);
	if (rc == 0)
		printf("Dump super block succeeded.\n");

	c2_balloc_fini(&ctxt);
	return rc;
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
