/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/18/2010
 */

#pragma once

#ifndef __COLIBRI_STOB_LINUX_GETEVENTS_H__
#define __COLIBRI_STOB_LINUX_GETEVENTS_H__

/**
   @addtogroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @{
 */

/*
 * XXX the following macro are copied from libaio-devel source,
 *     to solve a bug in libaio code.
 *     Now we are going to do system call directly.
 *     They should be definitely removed when that bug fixed. After that,
 *     io_getevents() from libaio should be called directly.
 */

#if defined(__i386__)

#define __NR_io_getevents       247
#define io_syscall5(type,fname,sname,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
          type5,arg5)                                                   \
type fname (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5)     \
{                                                                       \
long __res;                                                             \
long tmp;                                                               \
__asm__ volatile ("movl %%ebx,%7\n"                                     \
                  "movl %2,%%ebx\n"                                     \
                  "int $0x80\n"                                         \
                  "movl %7,%%ebx"                                       \
        : "=a" (__res)                                                  \
        : "0" (__NR_##sname),"rm" ((long)(arg1)),"c" ((long)(arg2)),    \
          "d" ((long)(arg3)),"S" ((long)(arg4)),"D" ((long)(arg5)), \
          "m" (tmp));                                                   \
return __res;                                                           \
}


#elif defined(__x86_64__)

#define __NR_io_getevents	208
#define __syscall_clobber "r11","rcx","memory"
#define __syscall "syscall"
#define io_syscall5(type,fname,sname,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5)							\
type fname (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5)	\
{									\
long __res;								\
__asm__ volatile ("movq %5,%%r10 ; movq %6,%%r8 ; " __syscall		\
	: "=a" (__res)							\
	: "0" (__NR_##sname),"D" ((long)(arg1)),"S" ((long)(arg2)),	\
	  "d" ((long)(arg3)),"g" ((long)(arg4)),"g" ((long)(arg5)) :	\
	__syscall_clobber,"r8","r10" );					\
return __res;								\
}

#endif

io_syscall5(int, raw_io_getevents, io_getevents, io_context_t, ctx, long, min_nr, long, nr, struct io_event *, events, struct timespec *, timeout)


/** @} end group stoblinux */

/* __COLIBRI_STOB_LINUX_GETEVENTS_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
