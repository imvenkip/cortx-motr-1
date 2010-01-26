#ifdef ENABLE_USER_MDS

#include <colibri/colibri.h>

int main(char *argv[], int argc)
{
	return 0;
}

#else

#include <linux/module.h>
#include <linux/kernel.h>
 
int init_module(void) 
{
	printk(KERN_INFO "mds init_module() called\n");
	return 0;
}
	 
void cleanup_module(void)
{
	printk(KERN_INFO "mds cleanup_module() called\n");
}

#endif
