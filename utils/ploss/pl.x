enum C2_PL_CONFIG_TYPE {
	PL_PROPABILITY = 1,
	PL_DELAY       = 2,
        PL_VERBOSE     = 3
};
typedef enum C2_PL_CONFIG_TYPE c2_pl_config_type;

struct c2_pl_config {
	c2_pl_config_type op;
	int	          value;
};

union c2_pl_config_reply switch (int res) {
	case 0:
		int config_value;
	default:
		void;
};

struct c2_pl_config_res {
	c2_pl_config_type op;
        c2_pl_config_reply body;
};

struct c2_pl_ping {
	int seqno;
};

struct c2_pl_ping_res {
	int seqno;
	unsigned hyper time;
};

program PLPROG {
	version PLVER {
                struct c2_pl_ping_res   PING(struct c2_pl_ping)  = 1;
                struct c2_pl_config_res SETCONFIG(struct c2_pl_config) = 2;
                struct c2_pl_config_res GETCONFIG(struct c2_pl_config) = 3;
	} = 1;
} = 0x20000076;
