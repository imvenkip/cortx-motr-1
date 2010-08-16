/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* free */
#include <errno.h>   /* errno */
#include <string.h>  /* memset */
#include <err.h>

#include "balloc/balloc.h"

int c2_balloc_get_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			 void *key, size_t keysize,
			 void **rec, size_t *recsize);

static int c2_balloc_dump_group_desc(struct c2_balloc_ctxt *ctxt, 
				     c2_bindex_t gn)
{
	DB  *db;
	struct c2_balloc_group_desc *desc;
	size_t size;
	int  result;

	if (gn > ctxt->bc_sb.bsb_groupcount) {
		printf("Invalid group number: %llu\n", (unsigned long long)gn);
		return -EINVAL;
	}

	db = ctxt->bc_group_info[gn].bgi_db_group_desc;

	result = c2_balloc_get_record(ctxt->bc_dbenv, ctxt->bc_tx, db,
				      &gn, sizeof gn,
				      (void**)&desc, &size);
	if (result == 0) {
		printf("groupno=%llu,%llx\n", (unsigned long long)desc->bgd_groupno, (unsigned long long)desc->bgd_groupno);    /*< group number */
        	printf("freeblocks=%llu,%llx\n", (unsigned long long)desc->bgd_freeblocks, (unsigned long long)desc->bgd_freeblocks); /*< total free blocks */
        	printf("fragments=%llu,%llx\n", (unsigned long long)desc->bgd_fragments, (unsigned long long)desc->bgd_fragments);  /*< nr of freespace fragments */
        	printf("maxchunk=%llu,%llx\n", (unsigned long long)desc->bgd_maxchunk, (unsigned long long)desc->bgd_maxchunk);   /*< max bytes of freespace chunk */
		free(desc);
	}

	return result;
}

int main(int argc, char **argv)
{
	struct c2_balloc_ctxt         ctxt = {
		.bc_nr_thread = 1,
	};

	int rc;
	char *path;
	int gn;

	if (argc != 3)
		errx(1, "Usage: %s path-to-db-dir group_number", argv[0]);

	path = argv[1];
	gn = atol(argv[2]);

	ctxt.bc_home = path;
	rc = c2_balloc_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_balloc_init error: %d\n", rc);
		return rc;
	}

	rc = c2_balloc_dump_group_desc(&ctxt, gn);
	
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
