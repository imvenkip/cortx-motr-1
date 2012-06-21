/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
	int	    result;
	int	    argc;
	int	    argc_scaled;
	int	    num;
	bool	    e;
	c2_bcount_t bcount;
	static char *argv[] = {
		"getopts-ut",
		"-e",
		"-n", "010",
		NULL
	};
	static char *argv_scaled[] = {
		"-a", "2b",
		"-b", "30k",
		"-c", "400m",
		"-d", "5000g",
		"-x", "70K",
		"-y", "800M",
		"-z", "9000G",
		"-j", "123456789012345",
		NULL
	};
	struct c2_ut_redirect redir;

	argc		    = ARRAY_SIZE(argv) - 1;
	argc_scaled	    = ARRAY_SIZE(argv_scaled) - 1;

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
	C2_UT_ASSERT(result == 0);

	/* test for valid "scaled"-type options */
	result = C2_GETOPTS("getopts-ut", argc_scaled, argv_scaled,
			    C2_SCALEDARG('a', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount == 2 * 512);})),
			    C2_SCALEDARG('b', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount == 30 * 1024);})),
			    C2_SCALEDARG('c', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount == 400 * 1024 * 1024);})),
			    C2_SCALEDARG('d', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount ==
					5000ULL * 1024 * 1024 * 1024);})),
			    C2_SCALEDARG('x', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount == 70 * 1000);})),
			    C2_SCALEDARG('y', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount == 800 * 1000000);})),
			    C2_SCALEDARG('z', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount ==
					9000 * 1000000000ULL);})),
			    C2_SCALEDARG('j', "scaled",
			LAMBDA(void, (c2_bcount_t bcount){
				C2_UT_ASSERT(bcount ==
					123456789012345ULL);})));
	C2_UT_ASSERT(result == 0);

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

	/* c2_get_bcount() */
	result = c2_get_bcount("123456789012345G", &bcount);
	C2_UT_ASSERT(result == -EOVERFLOW);
	result = c2_get_bcount("1asdf", &bcount);
	C2_UT_ASSERT(result == -EINVAL);
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
