#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Library");
MODULE_LICENSE("proprietary");

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}
