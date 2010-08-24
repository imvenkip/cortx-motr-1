/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* free */
#include <errno.h>   /* errno */
#include <string.h>  /* memset */
#include <err.h>

#include "balloc/balloc.h"

static int c2_balloc_dump_free_extent(struct c2_balloc_ctxt *ctxt, 
				      c2_bindex_t gn)
{
	int  result;
	DBT  nkeyt;
	DBT  nrect;
	DBC *cursor;
	DB  *db;
	c2_bindex_t	*bn;
	c2_bcount_t	*count;
	int sum = 0;

	if (gn > ctxt->bc_sb.bsb_groupcount) {
		printf("Invalid group number: %llu\n", (unsigned long long)gn);
		return -EINVAL;
	}

	db = ctxt->bc_group_info[gn].bgi_db_group_extent;

	result = db->cursor(db, NULL, &cursor, 0);
	if (result == 0) {
		while (1) {
			memset(&nkeyt, 0, sizeof nkeyt);
			memset(&nrect, 0, sizeof nrect);
			nrect.flags = DB_DBT_MALLOC;
			nkeyt.flags = DB_DBT_MALLOC;

			result = cursor->get(cursor, &nkeyt,
				     &nrect, DB_NEXT);
			if ( result != 0) {
				if (result == DB_NOTFOUND)
					result = 0;
				break;
			}

			bn = nkeyt.data;
			count = nrect.data;

			printf("...[%08llx, %lx]\n", (unsigned long long) *bn, 
			       (unsigned long)*count);
			free(bn);
			free(count);
			sum++;
		}

		cursor->close(cursor);
	}

	if (result)
		return result;
	else
		return sum;
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

	rc = c2_balloc_dump_free_extent(&ctxt, gn);
	if (rc >= 0) {
		printf("%d free extents dump succeeded\n", rc);
	} else {
		printf("Dump free extents failed: rc = %d\n", rc);
	}
	
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
