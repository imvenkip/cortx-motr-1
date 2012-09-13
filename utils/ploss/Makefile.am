sbin_PROGRAMS           = ploss_server ploss_client

ploss_server_SOURCES    = pl_svc.c pl_server.c pl_xdr.c helper.c helper.h pl.h
ploss_server_CFLAGS     = -D_REENTRANT
ploss_server_LDFLAGS    = -pthread

ploss_client_SOURCES    = pl_clnt.c pl_client.c pl_xdr.c helper.c helper.h pl.h
ploss_client_CFLAGS     = -D_REENTRANT
ploss_client_LDFLAGS    = -pthread

EXTRA_DIST              = pl.x
