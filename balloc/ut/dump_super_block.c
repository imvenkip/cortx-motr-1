/* -*- C -*- */

#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* free */
#include <errno.h>   /* errno */
#include <string.h>  /* memset */
#include <err.h>

#include "balloc/balloc.h"

extern void c2_balloc_debug_dump_sb(const char *tag, struct c2_balloc_ctxt *ctxt);

static int c2_balloc_dump_super_block(struct c2_balloc_ctxt *ctxt) 
{
	c2_balloc_debug_dump_sb(__func__, ctxt);
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
