bin_PROGRAMS            = ploss_server ploss_client
ploss_server_SOURCES    = pl_svc.c pl_server.c pl_xdr.c helper.c
ploss_server_CFLAGS     = -D_REENTRANT
ploss_server_LDADD     = -lnsl -lpthread
ploss_client_SOURCES    = pl_clnt.c pl_client.c pl_xdr.c helper.c
ploss_client_CFLAGS     = -D_REENTRANT
ploss_client_LDADD     = -lnsl -lpthread
