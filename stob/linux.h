/* -*- C -*- */

#ifndef __COLIBRI_STOB_LINUX_H__
#define __COLIBRI_STOB_LINUX_H__

#include "stob/stob.h"
/**
   @defgroup stoblinux Storage object based on Linux specific file system
   interfaces.

   @see stob
   @{
 */

#include "stob.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif


/**
  Stob domain for Linux type.
*/
struct c2_stob_domain_linux {
	struct c2_stob_domain sdl_base;
	/**
	   parent directory to hold the mapping db and objects.
	   Mapping db will be stored in map.db, and all objects will be stored
	   in Objects/LOXXXXXX
	*/
	char    sdl_path[MAXPATHLEN];

        DB_ENV        *sdl_dbenv;
        DB            *sdl_mapping;
        u_int32_t      sdl_dbenv_flags;
        u_int32_t      sdl_db_flags;
        u_int32_t      sdl_txn_flags;
        u_int32_t      sdl_cache_size;
        u_int32_t      sdl_nr_thread;
        u_int32_t      sdl_recsize;
        int            sdl_direct_db;
};

/**
   stob based on Linux file system
*/
struct c2_stob_linux {
	struct c2_stob sl_stob;

	/** fd from returned open(2) */
	int            sl_fd;
	/** 
	   storage object of Linux type is a plain file.
	   if this filename is empty string, it is a new and in-memory structure,
	   or it has not been mapped.
	*/
	char   sl_filename[MAXPATHLEN];
};

/** @} end group stoblinux */

/* __COLIBRI_STOB_LINUX_H__ */
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
