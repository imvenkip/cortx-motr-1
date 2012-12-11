enum M0_PL_CONFIG_TYPE {
	PL_PROPABILITY = 1,
	PL_DELAY       = 2,
        PL_VERBOSE     = 3
};
typedef enum M0_PL_CONFIG_TYPE m0_pl_config_type;

struct m0_pl_config {
	m0_pl_config_type op;
	int	          value;
};

union m0_pl_config_reply switch (int res) {
	case 0:
		int config_value;
	default:
		void;
};

struct m0_pl_config_res {
	m0_pl_config_type op;
        m0_pl_config_reply body;
};

struct m0_pl_ping {
	int seqno;
};

struct m0_pl_ping_res {
	int seqno;
	unsigned hyper time;
};

program PLPROG {
	version PLVER {
                struct m0_pl_ping_res   PING(struct m0_pl_ping)  = 1;
                struct m0_pl_config_res SETCONFIG(struct m0_pl_config) = 2;
                struct m0_pl_config_res GETCONFIG(struct m0_pl_config) = 3;
	} = 1;
} = 0x20000076;
