/**
   Why these small macros are not in lib-c2???
   XXX: just for now.
 */
#ifndef __COLIBRI_RPC_RPCDBG_H__
# define __COLIBRI_RPC_RPCDBG_H__
# ifdef __KERNEL__
#   define DBG(fmt, args...) printk("%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
# else
#   include <stdio.h>
#   include <stdlib.h>
#   define DBG(fmt, args...) printf("%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
# endif
#endif /* __COLIBRI_RPC_RPCDBG_H__  */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
