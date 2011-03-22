#include "io_fops_def.h"

/*
 * Since the .ff file which bears all FOP definitions has no
 * inclusion protection like header files, care has to be taken
 * in order to include the .ff file only once.
 * Hence this file has been created which includes its header file
 * which in turn includes the .ff file.
 * Since all the FOP definitions need to be a part of ioservice library,
 * this C file is used as a target in the Makefile.am
 */
