/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pl.h"

void usage(const char *prog)
{
        fprintf(stderr,
"Usage: %s -[ch] [-i interval] [-s verbose] [-p propability] [-d delay] server_host\n"
"There are two modes available for this program: ping and config mode. Ping mode is used to\n"
"send requests to server side, and config mode control how the server should handle these ping\n"
"requests.\n"
"-c:            Running in config mode; otherwise, it will be running in ping mode\n"
"-i interval:   Only applied to ping mode, it specifies the interval of sending ping requests\n"
"-s verbose:    Show requests received. If verbose equals 0, it disables to showing the requests\n"
"-p prop:       The probability of server discarding the ping requests, or client discarding\n"
        "\t\treplied request from server. The prop must be within [0, 100].\n"
"-d delay:      Delay time before server starting to handle the ping requests.\n"
"-h:            Show this message.\n",
                prog);
        exit(-1);
}

static int ping_seqno    = 0;
static int ping_interval = 1;
static int ping_verbose  = 0;
static int ping_delay    = 0;

void plprog_ping_1(CLIENT *clnt)
{
	enum clnt_stat retval;
	struct c2_pl_ping_res result;
	struct c2_pl_ping ping;

        while (1) {
                ping.seqno = ++ping_seqno;
                show_msg(ping_verbose, "Going to send ping request with seqno %d at %d\n",
                         ping.seqno, time(NULL));
        	retval = ping_1(&ping, &result, clnt);
	        if (retval != RPC_SUCCESS) {
		        clnt_perror (clnt, "call failed");
                        break;
	        }

                show_msg(ping_verbose, "Received server echo\n");
                if (result.seqno != ping.seqno) {
                        fprintf(stderr, "Server returned mismatch seqno %d, expect %d\n",
                                result.seqno, ping.seqno);
                        continue;
                }

                fprintf(stdout, "Server handled this ping request at %d\n", result.time);
                if (ping_interval)
                        sleep(ping_interval);
        }
}


int
main (int argc, char *argv[])
{
        const char *prog = argv[0];
        char *endptr;
	char *host;
        CLIENT *clnt;

        int have_config   = 0;
        int have_verbose  = 0;
        int have_prop     = 0;
        int have_delay    = 0;
        int have_interval = 0;

        int interval;
        int prop;
        int delay;
        int verbose;
        int c;

	if (argc < 2)
                usage(prog);

        while (((c = getopt(argc, argv, "chs:p:d:i:"))) != -1) {
                switch(c) {
                case 'c':
                        have_config = 1;
                        break;
                case 'i':
                        have_interval = 1;
                        interval = strtol(optarg, &endptr, 10);
                        if (*endptr) {
                                fprintf(stderr, "Wrong option %s for interval\n", optarg);
                                exit(1);
                        }
                        break;
                case 's':
                        have_verbose = 1;
                        verbose = strtol(optarg, &endptr, 10);
                        if (*endptr || verbose < 0 || verbose > 5) {
                                fprintf(stderr, "Wrong option %s for verbose\n", optarg);
                                exit(1);
                        }
                        break;
                case 'p':
                        have_prop = 1;
                        prop = strtol(optarg, &endptr, 10);
                        if (*endptr || prop < 0 || prop > 100) {
                                fprintf(stderr, "Wrong option %s for prop\n", optarg);
                                exit(1);
                        }
                        break;
                case 'd':
                        have_delay = 1;
                        delay = strtol(optarg, &endptr, 10);
                        if (*endptr || delay < 0) {
                                fprintf(stderr, "Wrong option %s for delay\n", optarg);
                                exit(1);
                        }
                        break;
                case 'h':
                default:
                        usage(prog);
                }
        }

        host = argv[optind];
        if (host == NULL) {
                fprintf(stderr, "No host specified\n");
                usage(prog);
        }

	clnt = clnt_create (host, PLPROG, PLVER, "udp");
	if (clnt == NULL) {
		clnt_pcreateerror (host);
		exit (1);
        }


        if (have_config) {      /* config mode */
                enum clnt_stat retval;
                struct c2_pl_config config;
                struct c2_pl_config_res config_res;
                c2_pl_config_reply *reply = &config_res.body;

                if (have_interval)
                        fprintf(stderr, "in config mode, interval will be ignored\n");

                if (have_prop) {
                        config.op = PL_PROPABILITY;
                        config.value = prop;

                        retval = setconfig_1(&config, &config_res, clnt);
	                if (retval != RPC_SUCCESS) {
		                clnt_perror (clnt, "call failed");
                                exit(1);
	                }
                        if (config_res.op != PL_PROPABILITY) {
                                fprintf(stderr, "fatal error: server unsderstands something wrong!\n");
                                exit(1);
                        }
                        if (reply->res)
                                fprintf(stderr, "Set prop error due to %s\n", strerror(reply->res));
                        else
                                fprintf(stdout, "The original config value for propability is %d\n",
                                        reply->c2_pl_config_reply_u.config_value);
                }

                if (have_delay) {
                        config.op = PL_DELAY;
                        config.value = delay;

                        retval = setconfig_1(&config, &config_res, clnt);
	                if (retval != RPC_SUCCESS) {
		                clnt_perror (clnt, "call failed");
                                exit(1);
	                }

                        if (config_res.op != PL_DELAY) {
                                fprintf(stderr, "fatal error: server unsderstands something wrong!\n");
                                exit(1);
                        }
                        if (reply->res)
                                fprintf(stderr, "Set delay error due to %s\n", strerror(reply->res));
                        else
                                fprintf(stdout, "The original config value for delay is %d\n",
                                        reply->c2_pl_config_reply_u.config_value);
                }

                if (have_verbose) {
                        config.op = PL_VERBOSE;
                        config.value = verbose;

                        retval = setconfig_1(&config, &config_res, clnt);
	                if (retval != RPC_SUCCESS) {
		                clnt_perror (clnt, "call failed");
                                exit(1);
	                }
                        if (config_res.op != PL_VERBOSE) {
                                fprintf(stderr, "fatal error: server unsderstands something wrong!\n");
                                exit(1);
                        }
                        if (reply->res)
                                fprintf(stderr, "Set verbose error due to %s\n", strerror(reply->res));
                        else
                                fprintf(stdout, "The original config value for verbose is %d\n",
                                        reply->c2_pl_config_reply_u.config_value);
                }
        } else {        /* ping mode */
                if (have_verbose)
                        ping_verbose = verbose;
                if (have_interval)
                        ping_interval = interval;
                if (have_delay)
                        ping_delay = delay;

                plprog_ping_1(clnt);
        }
	clnt_destroy (clnt);

        return 0;
}
