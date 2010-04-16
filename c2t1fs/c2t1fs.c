#include <linux/module.h>
#include <linux/kernel.h>
 
int init_module(void) 
{
       printk(KERN_INFO "c2t1fs init_module() called\n");
       return 0;
}
        
void cleanup_module(void)
{
       printk(KERN_INFO "c2t1fs cleanup_module() called\n");
}
