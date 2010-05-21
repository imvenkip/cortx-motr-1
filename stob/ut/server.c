#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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

/**
   Simple server for unit-test purposes. 
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

	result = linux_stob_module_init();
	
	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	unlink(path);

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  "./__s", &dom);
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

	dom->sd_ops->sdo_fini(dom);
	linux_stob_module_fini();

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
