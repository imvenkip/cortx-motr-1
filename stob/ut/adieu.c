#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>     /* fopen, fgetc, ... */
#include <string.h>    /* memset */
#include <unistd.h>    /* unlink */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <errno.h>

#include "lib/assert.h"

#include "stob/stob.h"
#include "stob/linux.h"

/**
   @addtogroup stob
   @{
 */

enum {
	NR    = 16,
	COUNT = 4096
};

/**
   Adieu unit-test. 
 */
int main(int argc, char **argv)
{
	int result;
	struct c2_stob_domain *dom;
	const struct c2_stob_id id = {
		.si_seq = 1,
		.si_id = 2
	};
	struct c2_stob *obj;
	struct c2_stob *obj1;
	const char path[] = "./__s/o/0000000000000001.0000000000000002";
	struct c2_stob_io io;
	c2_bcount_t user_vec[NR];
	char user_buf[NR][COUNT];
	char read_buf[NR][COUNT];
	char *user_bufs[NR];
	char *read_bufs[NR];
	c2_bindex_t stob_vec[NR];
	struct c2_clink clink;
	FILE *f;
	int ch;
	int i;
	int j;

#ifdef LINUX
	result = linux_stob_module_init();
#endif	

	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	unlink(path);

#ifdef LINUX
	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  "./__s", &dom);
#else
        /* Others than Linux are not supported so far. */
        result = -ENOSYS;
        dom = NULL;
#endif
	C2_ASSERT(result == 0);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_locate(obj);
	C2_ASSERT(result == -ENOENT);
	C2_ASSERT(obj->so_state == CSS_NOENT);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj1);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj == obj1);

	c2_stob_put(obj);
	c2_stob_put(obj1);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_create(obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_EXISTS);
	c2_stob_put(obj);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_create(obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_EXISTS);
	c2_stob_put(obj);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_locate(obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_EXISTS);

	for (i = 0; i < NR; ++i) {
		user_bufs[i] = user_buf[i];
		read_bufs[i] = read_buf[i];
		user_vec[i] = COUNT;
		stob_vec[i] = COUNT * (2 * i + 1);
		memset(user_buf[i], 'a' + i, sizeof user_buf[i]);
	}

	for (i = 1; i < NR; ++i) {
		c2_stob_io_init(&io);

		io.si_opcode = SIO_WRITE;
		io.si_flags  = 0;
		io.si_user.div_vec.ov_vec.v_nr = i;
		io.si_user.div_vec.ov_vec.v_count = user_vec;
		io.si_user.div_vec.ov_buf = (void **)user_bufs;

		io.si_stob.ov_vec.v_nr = i;
		io.si_stob.ov_vec.v_count = user_vec;
		io.si_stob.ov_index = stob_vec;

		c2_clink_init(&clink, NULL);
		c2_clink_add(&io.si_wait, &clink);

		result = c2_stob_io_launch(&io, obj, NULL, NULL);
		C2_ASSERT(result == 0);

		c2_chan_wait(&clink);

		C2_ASSERT(io.si_rc == 0);
		C2_ASSERT(io.si_count == COUNT * i);

		c2_clink_del(&clink);
		c2_clink_fini(&clink);

		c2_stob_io_fini(&io);

		f = fopen(path, "r");
		for (j = 0; j < i; ++j) {
			int k;

			for (k = 0; k < COUNT; ++k) {
				ch = fgetc(f);
				C2_ASSERT(ch == '\0');
				C2_ASSERT(!feof(f));
			}
			for (k = 0; k < COUNT; ++k) {
				ch = fgetc(f);
				C2_ASSERT(ch != '\0');
				C2_ASSERT(!feof(f));
			}
		}
		ch = fgetc(f);
		C2_ASSERT(ch == EOF);
		fclose(f);
	}

	for (i = 1; i < NR; ++i) {
		c2_stob_io_init(&io);

		io.si_opcode = SIO_READ;
		io.si_flags  = 0;
		io.si_user.div_vec.ov_vec.v_nr = i;
		io.si_user.div_vec.ov_vec.v_count = user_vec;
		io.si_user.div_vec.ov_buf = (void **)read_bufs;

		io.si_stob.ov_vec.v_nr = i;
		io.si_stob.ov_vec.v_count = user_vec;
		io.si_stob.ov_index = stob_vec;

		c2_clink_init(&clink, NULL);
		c2_clink_add(&io.si_wait, &clink);

		result = c2_stob_io_launch(&io, obj, NULL, NULL);
		C2_ASSERT(result == 0);

		c2_chan_wait(&clink);

		C2_ASSERT(io.si_rc == 0);
		C2_ASSERT(io.si_count == COUNT * i);

		c2_clink_del(&clink);
		c2_clink_fini(&clink);

		c2_stob_io_fini(&io);
		C2_ASSERT(memcmp(user_buf, read_buf, COUNT * i) == 0);
	}

	c2_stob_put(obj);
	dom->sd_ops->sdo_fini(dom);
#ifdef LINUX
	linux_stob_module_fini();
#endif

	return 0;
}

/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
