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
 * Original creation date: 10/04/2010
 */

#include "lib/ut.h"
#include "lib/cdefs.h"         /* ARRAY_SIZE */
#include "lib/thread.h"        /* LAMBDA */
#include "lib/assert.h"
#include "lib/getopts.h"

void test_getopts(void)
{
	int  result;
	int  argc;
	int  num;
	bool e;
	static char *argv[] = {
		"getopts-ut",
		"-e",
		"-n", "010",
		NULL
	};
	struct c2_ut_redirect redir;

	argc = ARRAY_SIZE(argv) - 1;

	c2_stream_redirect(stderr, "/dev/null", &redir);
	result = C2_GETOPTS("getopts-ut", argc, argv);
	C2_UT_ASSERT(result == -EINVAL);
	c2_stream_restore(&redir);

	e = false;
	result = C2_GETOPTS("getopts-ut", argc, argv,
			    C2_FORMATARG('n', "Num", "%i", &num),
			    C2_FLAGARG('e', "E", &e));
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(e == true);
	C2_UT_ASSERT(num == 8);

	result = C2_GETOPTS("getopts-ut", argc, argv,
			    C2_FORMATARG('n', "Num", "%d", &num),
			    C2_FLAGARG('e', "E", &e));
	C2_UT_ASSERT(num == 10);
	result = C2_GETOPTS("getopts-ut", argc, argv,
			    C2_STRINGARG('n', "Num",
			 LAMBDA(void, (const char *s){
				 C2_UT_ASSERT(!strcmp(s, "010"));
			 })),
			    C2_FLAGARG('e', "E", &e));

	argv[--argc] = NULL;
	argv[--argc] = NULL;

	e = false;
	result = C2_GETOPTS("getopts-ut", argc, argv,
			    C2_FLAGARG('e', "E", &e));
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(e == true);
	argv[--argc] = NULL;

	result = C2_GETOPTS("getopts-ut", argc, argv);
	C2_UT_ASSERT(result == 0);

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
