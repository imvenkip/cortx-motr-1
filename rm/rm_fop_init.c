#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "fop/fop_iterator.h"

#ifdef __KERNEL__
# include "rm_k.h"
# define loan_reply_handler NULL
# define send_out_handler NULL
#else
int loan_reply_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int send_out_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
# include "rm_u.h"
#endif

#include "rm_fop.h"

#include "fop/fop_format_def.h"
#include "rm.ff"

/**
   @addtogroup rm
   @{
 */

static struct c2_fop_type_ops loan_reply_ops = {
        .fto_execute = loan_reply_handler,
};

static struct c2_fop_type_ops send_out_ops = {
        .fto_execute = send_out_handler,
};

C2_FOP_TYPE_DECLARE(c2_rm_loan_reply, "reply", 16, &loan_reply_ops);
C2_FOP_TYPE_DECLARE(c2_rm_send_out, "send", 17, &send_out_ops);

static struct c2_fop_type *fops[] = {
        &c2_rm_loan_reply_fopt,
        &c2_rm_send_out_fopt
};

static struct c2_fop_type_format *fmts[] = {
	&c2_rm_fop_sid_tfmt,
        &c2_rm_loan_reply_tfmt,
        &c2_rm_send_out_tfmt
};


void rm_fop_fini(void)
{
        c2_fop_object_fini();
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
        c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

int rm_fop_init(void)
{
        int result;

        result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
        if (result == 0) {
                result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
                if (result == 0)
                        c2_fop_object_init(&c2_rm_fop_sid_tfmt);
        }
        if (result != 0)
                rm_fop_fini();
        return result;
}

/** @} end group rm */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

