#include <linux/module.h>
#include <linux/kernel.h>
 
int init_module(void) 
{
       printk(KERN_INFO "colibri FS init_module() called\n");
       return 0;
}
        
void cleanup_module(void)
{
       printk(KERN_INFO "colibri FS cleanup_module() called\n");
}
