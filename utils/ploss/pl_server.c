/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "lib/errno.h"
#include "pl.h"

static int pl_verbose = 1;
static int pl_prop = 0;
static int pl_delay = 0;

static int check_discard(int prop)
{
        if (prop == 0)
                return 0;
        if (prop == 100)
                return 1;

        return ((random() % 100) < prop);
}

bool_t
ping_1_svc(struct c2_pl_ping *argp, struct c2_pl_ping_res *result, struct svc_req *rqstp)
{
        show_msg(pl_verbose, "Received ping request with seqno %d\n", argp->seqno);
        if (check_discard(pl_prop)) {
                show_msg(pl_verbose, "request with %d discarded\n", argp->seqno);
                return FALSE;
        }

        if (pl_delay) {
                show_msg(pl_verbose, "Delay %d seconds as configured\n", pl_delay);
                sleep(pl_delay);
        }

        result->seqno = argp->seqno;
        result->time  = time(NULL);
        return TRUE;
}

bool_t
setconfig_1_svc(struct c2_pl_config *argp, struct c2_pl_config_res *result, struct svc_req *rqstp)
{
        const char *msg = NULL;
        int *config_res = &result->body.res;
        uint32_t *config_vp = &result->body.c2_pl_config_reply_u.config_value;

        *config_res = 0;
        result->op = argp->op;
        switch(argp->op) {
        case PL_PROPABILITY:
                if (argp->value > 100 || argp->value < 0) {
                        *config_res = EINVAL;
                        msg = "Refuse propability due to invalid value\n";
                        break;
                }
                *config_vp = pl_prop;
                pl_prop = argp->value;
                break;
        case PL_DELAY:
                if (argp->value < 0) {
                        *config_res = EINVAL;
                        msg = "Refuse delay due to invalid value\n";
                        break;
                }
                *config_vp = pl_delay;
                pl_delay = argp->value;
                break;
        case PL_VERBOSE:
                if (argp->value < 0 || argp->value > 5) {
                        *config_res = EINVAL;
                        msg = "Refuse verbose config because it must be wihin [0, 5]\n";
                        break;
                }
                *config_vp = pl_verbose;
                pl_verbose = argp->value;
                break;
        default:
                *config_res = EINVAL;
                msg = "Unknow option\n";
        }

        if (*config_res && msg != NULL)
                show_msg(pl_verbose, msg);
        return TRUE;
}

bool_t
getconfig_1_svc(struct c2_pl_config *argp, struct c2_pl_config_res *result, struct svc_req *rqstp)
{
	bool_t retval = TRUE;

	/*
	 * insert server code here
	 */

	return retval;
}

int
plprog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);

	return 1;
}
