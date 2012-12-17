/*
 * XXX Temporary kludge for "conf-net" demo.
 *
 * This file should be deleted as soon as `confd' service can receive
 * parameter via mero_setup CLI option.
 */
#pragma once
#ifndef __MERO_CONF_CONFD_HACK_H__
#define __MERO_CONF_CONFD_HACK_H__

/* Variants of configuration string. */
enum m0_conf_mode {
	M0_CM_UT, /* for unit testing */
	M0_CM_ST  /* for system testing */
};

/*
 * Specifies configuration string to be used by confd.
 *
 * This variable allows external entity (e.g., conf/ut/confc.c)
 * to tell confd, which configuration string to use.
 *
 * Default value is M0_CM_ST.
 */
M0_EXTERN enum m0_conf_mode m0_confd_hack_mode;

/*
 * Gets the configuration string corresponding to a given mode of
 * operation.
 *
 * CAUTION:
 * - Don't free *out.
 * - Don't call m0_conf_str() concurrently.
 */
M0_INTERNAL int m0_conf_str(enum m0_conf_mode mode, const char **out);

#endif /* __MERO_CONF_CONFD_HACK_H__ */
