/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/19/2010
 */

#include <stdio.h>    /* setbuf */
#include <stdlib.h>   /* system */
#include <sys/stat.h> /* mkdir */
#include <unistd.h>   /* chdir */
#include <errno.h>
#include <stdbool.h>  /* bool */

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/list.h"
#include "lib/ut.h"
#include "lib/finject.h"

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

void unit_end(const char *sandbox, bool keep_sandbox)
{
	int rc;

	c2_fini();

	rc = chdir("..");
	C2_ASSERT(rc == 0);

        if (!keep_sandbox)
                reset_sandbox(sandbox);
}

int parse_test_list(char *str, struct c2_list *list)
{
	char *token;
	char *subtoken;
	char *saveptr = NULL;
	struct c2_test_suite_entry *ts_entry;

	while (true) {
		token = strtok_r(str, ",", &saveptr);
		if (token == NULL)
			break;

		subtoken = strchr(token, ':');
		if (subtoken != NULL)
			*subtoken++ = '\0';

		C2_ALLOC_PTR(ts_entry);
		if (ts_entry == NULL)
			return -ENOMEM;

		ts_entry->tse_suite_name = token;
		/* subtoken can be NULL if no test was specified */
		ts_entry->tse_test_name = subtoken;

		c2_list_link_init(&ts_entry->tse_linkage);
		c2_list_add_tail(list, &ts_entry->tse_linkage);

		/* str should be NULL for subsequent strtok_r(3) calls */
		str = NULL;
	}

	return 0;
}

void free_test_list(struct c2_list *list)
{
	struct c2_test_suite_entry *entry;
	struct c2_test_suite_entry *n;
	c2_list_for_each_entry_safe(list, entry, n,
			struct c2_test_suite_entry, tse_linkage)
	{
		c2_list_del(&entry->tse_linkage);
		c2_free(entry);
	}
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
