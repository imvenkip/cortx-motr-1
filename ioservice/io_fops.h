/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */
#ifndef __COLIBRI_IOSERVICE_IO_FOPS_H__
#define __COLIBRI_IOSERVICE_IO_FOPS_H__

#include "fop/fop_base.h"
#include "fop/fop_format.h"
#include "lib/list.h"
#include "fop/fop.h"

/**
   @addtogroup io_fops
   In-memory definition of generic io fop and generic io segment.
 */
struct page;
struct c2_io_ioseg;

/**
   @page DLD-fspec DLD Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref DLD-fspec-ds
   - @ref DLD-fspec-sub
   - @ref DLD-fspec-cli
   - @ref DLD-fspec-usecases
   - @ref DLDDFS "Detailed Functional Specification" <!-- Note link -->

   The Functional Specification section of the DLD shall be placed in a
   separate Doxygen page, identified as a @@subpage of the main specification
   document through the table of contents in the main document page.  The
   purpose of this separation is to co-locate the Functional Specification in
   the same source file as the Detailed Functional Specification.

   A table of contents should be created for the major sections in this page,
   as illustrated above.  It should also contain references to other
   <b>external</b> Detailed Functional Specification sections, which even
   though may be present in the same source file, would not be visibly linked
   in the Doxygen output.

   @section DLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   Simple lists can also suffice:
   - dld_sample_ds1
   - dld_bad_example

   The section could also describe what use it makes of data structures
   described elsewhere.

   Note that data structures are defined in the
   @ref DLDDFS "Detailed Functional Specification"
   so <b>do not duplicate the definitions</b>!
   Do not describe internal data structures here either - they can be described
   in the @ref DLD-lspec "Logical Specification" if necessary.

   @section DLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Externally visible interfaces should be enumerated and categorized by
   function.  <b>Do not provide details.</b> They will be fully documented in
   the @ref DLDDFS "Detailed Functional Specification".
   Do not describe internal interfaces - they can be described in the
   @ref DLD-lspec "Logical Specification" if necessary.

   @subsection DLD-fspec-sub-cons Constructors and Destructors

   @subsection DLD-fspec-sub-acc Accessors and Invariants

   @subsection DLD-fspec-sub-opi Operational Interfaces
   - dld_sample_sub1()

   @section DLD-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section DLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   Note the following references to the Detailed Functional Specification
   sections at the end of these Functional Specifications, created using the
   Doxygen @@see command:

   @see @ref DLDDFS "Sample Detailed Functional Specification"
 */

/**
   @defgroup DLDDFS Colibri Sample Module
   @brief Detailed functional specification template.

   This page is part of the DLD style template.  Detailed functional
   specifications go into a module described by the Doxygen @@defgroup command.
   Note that you cannot use a hyphen (-) in the tag of a @@defgroup.

   Module documentation may spread across multiple source files.  Make sure
   that the @@addtogroup Doxygen command is used in the other files to merge
   their documentation into the main group.  When doing so, it is important to
   ensure that the material flows logically when read through Doxygen.

   You are not constrained to have only one module in the design.  If multiple
   modules are present you may use multiple @@defgroup commands to create
   individual documentation pages for each such module, though it is good idea
   to use separate header files for the additional modules.  In particular, it
   is a good idea to separate the internal detailed documentation from the
   external documentation in this header file.  Please make sure that the DLD
   and the modules cross-reference each other, as shown below.

   @see The @ref DLD "Colibri Sample DLD" its
   @ref DLD-fspec "Functional Specification"
   and its @ref DLD-lspec-thread

   @{
*/

/**
   Data structure to do something.

 */
struct dld_sample_ds1 {
	/** The z field */
	int dsd_z_field;
	/** Flux density */
	int dsd_flux_density;
};

/* Documented elsewhere to illustrate bad documentation of an external symbol.
   Doxygen cannot automatically reference it from elsewhere, as can be seen in
   the Doxygen output for the reference in the Functional Specification above.
 */
extern unsigned int dld_bad_example;

/**
   Subroutine1 opens a foo for access.

   Some particulars:
   - Proper grammar, punctuation and spelling is required
   in all documentation.
   This requirement is not relaxed in the detailed functional specification.
   - Function documentation should be in the 3rd person, singular, present
   tense, indicative mood, active voice.  For example, "creates",
   "initializes", "finds", etc.
   - Functional parameters should not trivialize the
   documentation by repeating what is already clear from the function
   prototype.  For example it would be wrong to say, <tt>"@param read_only
   A boolean parameter."</tt>.
   - The default return convention (0 for success and <tt>-errno</tt>
   on failure) should not be repeated.
   - The @@pre and @@post conditions are preferably expressed in code.

   @param param1 Parameter 1 must be locked before use.
   @param read_only This controls the modifiability of the foo object.
   Set to @c true to prevent modification.
   @retval return value
   @pre Pre-condition, preferably expressed in code.
   @post Post-condition, preferably expressed in code.
*/
int dld_sample_sub1(struct dld_sample_ds1 *param1, bool read_only);

/**
   @} DLDDFS end group
*/
/**
 * The opcode from which IO service FOPS start.
 */
enum c2_io_service_opcodes {
	C2_IOSERVICE_READV_OPCODE = 15,
	C2_IOSERVICE_WRITEV_OPCODE = 16,
	C2_IOSERVICE_READV_REP_OPCODE = 17,
	C2_IOSERVICE_WRITEV_REP_OPCODE = 18,
};

/**
   Returns the number of fops registered by ioservice.
 */
int c2_ioservice_fops_nr(void);

/**
   Init and fini of ioservice fops code.
 */
int c2_ioservice_fop_init(void);
void c2_ioservice_fop_fini(void);

/**
   This data structure is used to associate an io fop with its
   rpc bulk data. It abstracts the c2_net_buffer and net layer APIs.
   Client side implementations use this structure to represent
   io fops and the associated rpc bulk structures.

   @todo Not complete yet. Need to build ops around c2_io_fop.
 */
struct c2_io_fop {
	/** Inline fop for a generic IO fop. */
	struct c2_fop		if_fop;
	/** Bulk structure containing zero vector for io fop. */
	struct c2_rpc_bulk	if_bulk;
};

/**
   Generic io segment that represents a contiguous stream of bytes
   along with io extent. This structure is typically used by io coalescing
   code from ioservice.
 */
struct io_zeroseg {
	/* Offset of target object to start io from. */
	c2_bindex_t		 is_off;
	/* Number of bytes in io segment. */
	c2_bcount_t		 is_count;
	/* Starting address of buffer. */
	void			*is_buf;
	/* Linkage to have such zero segments in a list. */
	struct c2_list_link	 is_linkage;
};

/**
   Allocate a zero segment.
   @retval Valid io_zeroseg object if success, NULL otherwise.
 */
struct io_zeroseg *io_zeroseg_alloc(void);

/**
   Deallocate a zero segment.
   @param zseg - Zero segment to be deallocated.
 */
void io_zeroseg_free(struct io_zeroseg *zseg);

/**
   Get the io segment indexed by index in array of io segments in zerovec.
   @note The incoming c2_0vec should be allocated and initialized.

   @param zvec The c2_0vec io vector from which io segment will be retrieved.
   @param index Index of io segments in array of io segments from zerovec.
   @param seg Out parameter to return io segment.
 */
void io_zerovec_seg_get(const struct c2_0vec *zvec, uint32_t index,
			struct io_zeroseg *seg);

/**
   Set the io segment referred by index into array of io segments from
   the zero vector.
   @note There is no data copy here. Just buffer pointers are copied since
   this API is supposed to be used in same address space.

   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec io vector whose io segment will be changed.
   @param seg Target segment for set.
 */
void io_zerovec_seg_set(struct c2_0vec *zvec, uint32_t index,
			const struct io_zeroseg *seg);

/**
   Allocate the io segments for the given c2_0vec structure.
   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec structure.
   @param segs_nr Number of io segments to be allocated.
 */
int io_zerovec_segs_alloc(struct c2_0vec *zvec, uint32_t segs_nr);

/**
 * FOP definitions and corresponding fop type formats
 * exported by ioservice.
 */
extern struct c2_fop_type_format c2_fop_cob_writev_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;
extern struct c2_fop_type_format c2_fop_file_fid_tfmt;
extern struct c2_fop_type_format c2_fop_io_buf_tfmt;
extern struct c2_fop_type_format c2_fop_io_seg_tfmt;
extern struct c2_fop_type_format c2_fop_io_vec_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_reply_tfmt;

extern struct c2_fop_type c2_fop_cob_readv_fopt;
extern struct c2_fop_type c2_fop_cob_writev_fopt;
extern struct c2_fop_type c2_fop_cob_readv_rep_fopt;
extern struct c2_fop_type c2_fop_cob_writev_rep_fopt;

/** @} end of io_fops group */

/* __COLIBRI_IOSERVICE_IO_FOPS_H__ */
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
