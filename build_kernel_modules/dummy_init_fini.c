#include <linux/module.h>

/*
  This file contains dummy init() and fini() routines for modules, that are
  not yet ported to kernel.

  Once the module compiles successfully for kernel mode, dummy routines from
  this file should be removed.
 */

#define DUMMY_IMPLEMENTATION \
	printk("dummy implementation of %s called\n", __FUNCTION__)
int c2_trace_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_trace_fini(void)
{

}

int c2_memory_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_memory_fini(void)
{

}

int c2_threads_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_threads_fini(void)
{

}

int c2_db_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_db_fini(void)
{

}

int c2_linux_stobs_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_linux_stobs_fini(void)
{

}

int c2_ad_stobs_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_ad_stobs_fini(void)
{

}

int sim_global_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void sim_global_fini(void)
{

}

int c2_reqhs_init(void)
{
	DUMMY_IMPLEMENTATION;
	return 0;
}

void c2_reqhs_fini(void)
{

}
