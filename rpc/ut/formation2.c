#include "lib/ut.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"

static int frm_ut_init(void);
static int frm_ut_fini(void);
static void frm_init_test(void);
static void frm_fini_test(void);

const struct c2_test_suite frm_ut = {
	.ts_name = "formation-ut",
	.ts_init = frm_ut_init,
	.ts_fini = frm_ut_fini,
	.ts_tests = {
		{ "frm-init", frm_init_test},
		{ "frm-fini", frm_fini_test},
		{ NULL,       NULL         }
	}
};

static int frm_ut_init(void)
{
	return 0;
}
static int frm_ut_fini(void)
{
	return 0;
}

static struct c2_rpc_frm frm;
static struct c2_rpc_frm_constraints constraints;
static struct c2_rpc_machine rmachine;

static void frm_init_test(void)
{
	c2_rpc_frm_init(&frm, &rmachine, constraints);
	C2_UT_ASSERT(frm.f_state == FRM_IDLE);
}
static void frm_fini_test(void)
{
	c2_rpc_frm_fini(&frm);
}
