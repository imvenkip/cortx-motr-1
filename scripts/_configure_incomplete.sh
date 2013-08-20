#!/bin/sh
# XXX remove this file after m0_db -> m0_be conversion

make distclean
sh autogen.sh && ./configure --disable-system-libs --enable-debug --disable-rpm --disable-altogether-mode && make -j3
make -j3 all-user
