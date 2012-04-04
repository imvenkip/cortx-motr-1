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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 03/28/2012
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */

#define NLX_SCOPE
#include "net/lnet/lnet_core.h"
#include "net/lnet/lnet_ioctl.h"
#include "net/lnet/ut/lnet_ut.h"

const char lnet_xprt_dev[] = "/dev/" C2_LNET_DEV;
const char lnet_ut_proc[]  = "/proc/c2_lnet_ut";

enum {
	MAX_PROC_TRIES = 120,
	PROC_DELAY_SEC = 2,
};

/** @todo hack */
void *nlx_core_mem_alloc(size_t size, unsigned shift)
{
	return c2_alloc_aligned(size, shift);
}

/** @todo hack */
void nlx_core_mem_free(void *data, size_t size, unsigned shift)
{
	c2_free_aligned(data, size, shift);
}

int test_dev_exists()
{
	struct stat st;
	int rc = stat(lnet_xprt_dev, &st);

	if (rc == 0) {
		if (!S_ISCHR(st.st_mode))
			rc = -1;
		if (st.st_uid != 0 || st.st_gid != 0)
			rc = -1;
		if ((st.st_mode & ALLPERMS) != (S_IRUSR|S_IWUSR))
			rc = -1;
	}
	return rc;
}

int test_dev_open_close()
{
	struct c2_lnet_dev_dom_init_params p;
	struct nlx_core_domain *dom;
	int f;
	int rc;

	C2_SET0(&p);
	NLX_ALLOC_PTR(dom);
	C2_ASSERT(dom != NULL);
	p.ddi_cd = dom;

	f = open(lnet_xprt_dev, O_RDWR|O_CLOEXEC);
	if (f < 0) {
		perror(lnet_xprt_dev);
		return 1;
	}

	rc = ioctl(f, C2_LNET_DOM_INIT, &p);
	if (rc == 0) {
		printf("max values are: bufsize=%ld segsize=%ld segs=%d\n",
		       p.ddi_max_buffer_size,
		       p.ddi_max_buffer_segment_size,
		       p.ddi_max_buffer_segments);
	}

	close(f);
	NLX_FREE_PTR(dom);
	return rc;
}

int test_tm()
{
	struct nlx_core_transfer_mc *tm;

	NLX_ALLOC_PTR(tm);
	C2_ASSERT(tm != NULL);
	tm->ctm_magic = C2_NET_LNET_CORE_TM_MAGIC;
	tm->ctm_user_space_xo = true;
	tm->_debug_ = 15;
	NLX_FREE_PTR(tm);
	return 0;
}

int main(int argc, char *argv[])
{
	int procf;
	int i;
	int rc;
	char cmd[1];
	off_t off = 0;
	c2_time_t delay;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	c2_time_set(&delay, PROC_DELAY_SEC, 0);
	for (i = 0; i < MAX_PROC_TRIES; ++i) {
		procf = open(lnet_ut_proc, O_RDWR);
		if (procf >= 0)
			break;
		C2_ASSERT(errno == ENOENT);
		c2_nanosleep(delay, 0);
	}
	if (procf < 0) {
		fprintf(stderr,
			"%s: kernel UT did not create %s after %d sec\n",
			__FILE__, lnet_ut_proc,
			MAX_PROC_TRIES * PROC_DELAY_SEC);
		c2_fini();
		exit(1);
	}

	cmd[0] = UT_USER_READY;
	rc = write(procf, cmd, 1);
	C2_ASSERT(rc == 1);
	while (1) {
		off = lseek(procf, off, SEEK_SET);
		if (off != 0)
			break;
		rc = read(procf, cmd, 1);
		if (rc <= 0)
			break;
		switch (*cmd) {
		case UT_TEST_DEV:
			rc = test_dev_exists();
			break;
		case UT_TEST_OPEN:
			rc = test_dev_open_close();
			break;
		case UT_TEST_TM:
			rc = test_tm();
			break;
		case UT_TEST_DONE:
			printf("done testing\n");
			goto done;
		default:
			printf("**** UNKNOWN TEST %d\n", (int) *cmd);
			break;
		}
		cmd[0] = (rc == 0) ? UT_USER_SUCCESS : UT_USER_FAIL;
		rc = write(procf, cmd, 1);
		C2_ASSERT(rc == 1);
	}
done:
	close(procf);
	c2_fini();
	return 0;
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
