/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_FORMAT_DEF_H__
#define __COLIBRI_FOP_FOP_FORMAT_DEF_H__

/**
   @addtogroup fop 

   @{
*/

/**
   @file fop_format_def.h

   Helper macros included before .ff files.
 */

#define DEF C2_FOP_FORMAT
#define _  C2_FOP_FIELD
#define _case C2_FOP_FIELD_TAG

#define U32 C2_FOP_TYPE_FORMAT_U32
#define U64 C2_FOP_TYPE_FORMAT_U64
#define BYTE C2_FOP_TYPE_FORMAT_BYTE
#define VOID C2_FOP_TYPE_FORMAT_VOID

#define RECORD FFA_RECORD
#define UNION FFA_UNION
#define SEQUENCE FFA_SEQUENCE
#define TYPEDEF FFA_TYPEDEF

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_FORMAT_DEF_H__ */
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
