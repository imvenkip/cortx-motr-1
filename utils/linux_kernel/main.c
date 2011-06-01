#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/assert.h"
#include "lib/bitmap.h"
#include "lib/rwlock.h"
#include "lib/memory.h"
#include "lib/ut.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Unit Test Module");
MODULE_LICENSE("proprietary");

/* lib/ut */
extern void test_bitmap(void);
extern void test_chan(void);
extern void test_rw(void);
extern void test_thread(void);

static const struct c2_test_suite klibc2_ut = {
	.ts_name = "klibc2-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bitmap",    test_bitmap    },
		{ "chan",      test_chan      },
		{ "rwlock",    test_rw        },
		{ "thread",    test_thread    },
		{ NULL,        NULL           }
	}
};

static void run_kernel_ut(void)
{
	c2_uts_init();
	c2_ut_add(&klibc2_ut);
	c2_ut_run(NULL);
	c2_uts_fini();
}

int init_module(void)
{
        printk(KERN_INFO "Colibri Kernel Unit Test\n");

	run_kernel_ut();

	return 0;
}

void cleanup_module(void)
{
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
