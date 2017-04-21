#!/bin/bash

# Script for starting or stopping Clovis system tests

. `dirname $0`/clovis_st_inc.sh
# enable core dumps
ulimit -c unlimited

# Print out usage
usage()
{
	cat <<.
Usage:

$ sudo clovis_sys_test [start|stop|list|run] [local|remote] [-i Index-service]\
[-t tests] [-k] [-u]

Where:

start: starts only clovis system tests.
stop : stops clovis system tests.
list : Lists all the available clovis system tests.
run  : Starts Mero services, executes clovis system tests and then\
stops mero services.

-i: Select Index service:
    CASS : Cassandra
    KVS  : Mero KVS
    MOCK : Mock index service

-k: run Clovis system tests in kernel mode

-u: run Clovis system tests in user space mode

-t TESTS: Only run selected tests

-r: run tests in a suite in random order
.
}
OPTIONS_STRING="ikurt:"

# Get options
cmd=$1
conf=$2
shift 2

umod=1
random_mode=0
while getopts "$OPTIONS_STRING" OPTION; do
	case "$OPTION" in
		i)
			index=$2
			shift 1
			;;
		k)
			umod=0
			echo "Running Clovis ST in Kernel mode"
			;;
		u)
			umod=1
			echo "Running Clovis ST in User mode"
			;;
		t)
			tests="$OPTARG"
			;;
		r)
			random_mode=1
			;;
		*)
			usage
			exit 1
			;;
	esac
done

case "$conf" in
	local)
		. `dirname $0`/clovis_local_conf.sh
		;;
	remote)
		. `dirname $0`/clovis_remote_conf.sh
		;;
	*)
		usage
		exit
esac

case "$cmd" in
	start)
		if [ $umod -eq 1 ]; then
			clovis_st_start_u $index
		else
			clovis_st_start_k $index
		fi

		rc=$?
		report_and_exit clovis_sys_test $rc
		;;
	run)
		( exec `dirname $0`/mero_services.sh start )
		if [ $umod -eq 1 ]; then
			clovis_st_start_u $index
		else
			clovis_st_start_k $index
		fi

		rc=$?

		( exec `dirname $0`/mero_services.sh stop )
		report_and_exit clovis_sys_test $rc
		;;
	stop)
		if [ $umod -eq 1 ]; then
			clovis_st_stop_u
		else
			clovis_st_stop_k
		fi
		;;
	list)
		clovis_st_list_tests
		;;
	*)
		usage
		exit
esac

# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
