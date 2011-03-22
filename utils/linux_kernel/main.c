#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "lib/assert.h"
#include "lib/bitmap.h"
#include "lib/memory.h"
#include "lib/thread.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Unit Test Module");
MODULE_LICENSE("proprietary");

enum {
	UT_BITMAP_SIZE = 120
};

static void c2_kernel_bitmap_ut(void)
{
	struct c2_bitmap bm;
	size_t idx;

	/*
	  Assuming full UT performed at user level, and given
	  that bitmap code is shared, just do a sanity test.
	*/

	if (c2_bitmap_init(&bm, UT_BITMAP_SIZE) != 0) {
	    C2_IMPOSSIBLE("bitmap failed to initialize\n");
	}

	for (idx = 0; idx < UT_BITMAP_SIZE; ++idx) {
		C2_ASSERT(c2_bitmap_get(&bm, idx) == false);
	}

	c2_bitmap_set(&bm, 1, true);
	C2_ASSERT(c2_bitmap_get(&bm, 1) == true);
	C2_ASSERT(c2_bitmap_get(&bm, 0) == false);
	C2_ASSERT(c2_bitmap_get(&bm, 64) == false);
	c2_bitmap_set(&bm, 1, false);
	C2_ASSERT(c2_bitmap_get(&bm, 1) == false);

	c2_bitmap_fini(&bm);
	C2_ASSERT(bm.b_nr == 0);
	C2_ASSERT(bm.b_words == NULL);
        printk(KERN_INFO "bitmap: passed\n");
}

static void c2_kernel_thread_ut(void)
{
	struct c2_thread *t;
	struct c2_bitmap cpus;
	int result;

	/*
	  c2_thread_init does not exist, allocate a c2_thread for this thread,
	  initialize it enough for UT (blackbox UT), and use it to call
	  c2_thread_confine.
	*/
	C2_ALLOC_PTR(t);
	t->t_state = TS_RUNNING;
	t->t_h.h_id = current->pid;

	/* set affinity (confine) to CPU 0 */
	c2_bitmap_init(&cpus, 3);
	c2_bitmap_set(&cpus, 0, true);

	result = c2_thread_confine(t, &cpus);
	c2_bitmap_fini(&cpus);
	if (result != -ENOSYS) {
	    printk(KERN_INFO "c2_thread_confine: failed, result == %d\n", result);
	} else {
	    printk(KERN_INFO "thread: passed\n");
	}

	c2_free(t);
}

/* lib/ut/chan.c */
extern void test_chan(void);

static void c2_run_kernel_ut(void)
{
	c2_kernel_bitmap_ut();
	c2_kernel_thread_ut();
	test_chan();

        printk(KERN_INFO "Colibri Kernel UT: all passed\n");
}

int init_module(void)
{
        printk(KERN_INFO "Colibri Kernel Unit Test\n");

	c2_run_kernel_ut();

	return 0;
}

void cleanup_module(void)
{
}
