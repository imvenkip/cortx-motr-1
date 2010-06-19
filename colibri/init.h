/* -*- C -*- */

#ifndef __COLIBRI_COLIBRI_INIT_H__
#define __COLIBRI_COLIBRI_INIT_H__

/**
   @defgroup init Colibri initialisation calls.

   @{
 */

/**
   Performs all global initializations of C2 sub-systems. The nomenclature of
   sub-systems to be initialised depends on the build configuration.

   @see c2_fini().
 */
int  c2_init(void);

/**
   Finalizes all sub-systems initialised by c2_init().
 */
void c2_fini(void);

/** @} end of init group */

/* __COLIBRI_COLIBRI_LIST_H__ */
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
