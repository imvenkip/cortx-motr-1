/* -*- C -*- */

#include <stdio.h>    /* setbuf */
#include <stdlib.h>   /* system */
#include <sys/stat.h> /* mkdir */
#include <unistd.h>   /* chdir */
#include <errno.h>

#include "colibri/init.h"
#include "lib/assert.h"

int reset_sandbox(const char *sandbox)
{
	char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr '%s'", sandbox);
	C2_ASSERT(rc > 0);

	rc = system(cmd);
	if (rc != 0) {
		/* cleanup might fail for innocent reasons, e.g., unreliable rm
		   on an NFS mount. */
		fprintf(stderr, "sandbox cleanup at \"%s\" failed: %i\n",
			sandbox, rc);
	}

	free(cmd);
	return rc;
}

int unit_start(const char *sandbox)
{
	int result;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	result = c2_init();
	if (result == 0) {
		result = reset_sandbox(sandbox);
		if (result == 0) {
			result = mkdir(sandbox, 0700);
			if (result == 0)
				result = chdir(sandbox);
			if (result != 0)
				result = -errno;
		}
	}
	return result;
}

void unit_end(const char *sandbox)
{
	int rc;

	c2_fini();

	rc = chdir("..");
	C2_ASSERT(rc == 0);

	reset_sandbox(sandbox);
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
