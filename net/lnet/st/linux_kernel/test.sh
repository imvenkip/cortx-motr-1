#! /bin/sh

# Small wrapper to run kernel lnetping ST

if [ "$(id -u)" -ne 0 ]; then
    echo "Must be run as root"
    exit 1
fi

usage() {
    echo "Usage: $0 {-s | -c | -s -c}"
    echo "   [-b #Bufs] [-l #Loops] [-n #Threads] [-o MessageTimeout]"
    echo "   [-O PassiveTimeout] [-d PassiveSize] [-D ActiveDelay] [-q]"
    echo "   [-i ClientNetwork] [-p ClientPortal] [-t ClientTMID]"
    echo "   [-I ServerNetwork] [-P ServerPortal] [-T ServerTMID]"
    echo "   [-x ClientDebug] [-X ServerDebug]"
    echo "Flags:"
    echo "-D\tServer active bulk delay"
    echo "-I\tServer network interface (ip@intf)"
    echo "-O\tBulk timeout in seconds"
    echo "-P\tServer portal"
    echo "-T\tServer TMID"
    echo "-X\tServer debug"
    echo "-b\tNumber of buffers"
    echo "-c\tRun client only"
    echo "-d\tPassive data size"
    echo "-i\tClient network interface (ip@intf)"
    echo "-l\tLoops to run"
    echo "-n\tNumber of client threads"
    echo "-o\tMessage send timeout in seconds"
    echo "-p\tClient portal"
    echo "-q\tNot verbose"
    echo "-s\tRun server only"
    echo "-t\tClient base TMID - default is dynamic"
    echo "-x\tClient debug"
    echo "By default the client and server are configued to use the first LNet"
    echo "network interface returned by lctl."
}

d="`git rev-parse --show-cdup`"
if [ -n "$d" ]; then
    cd "$d"
fi

modprobe lnet
if [ $? -ne 0 ]; then
    echo "The lnet module is not loaded"
    exit 1
fi
lctl network up
if [ $? -ne 0 ] ; then
    echo "LNet network not enabled"
    exit 1
fi

# use the first NID configured
NID=`lctl list_nids | head -n 1`
if [ -z "$NID" ] ; then
    echo "No networks available"
    exit 1
fi

Pverbose=verbose
Pserver_only=
Pclient_only=
Pnr_bufs=
Ploops=
Ppassive_size="passive_size=30720"
Pbulk_timeout="bulk_timeout=20"
Pmsg_timeout="msg_timeout=5"
Pactive_bulk_delay=
Pnr_clients=
Pclient_network="client_network=$NID"
Pclient_portal=
Pclient_tmid=
Pclient_debug=
Pserver_network="server_network=$NID"
Pserver_portal=
Pserver_tmid=
Pserver_debug=

while [ $# -gt 0 ]; do
    FLAG=$1; shift
    has_sarg=0
    has_narg=0
    case $FLAG in
	(-c) Pclient_only="client_only";;
	(-s) Pserver_only="server_only";;
	(-q) Pverbose="" ;;
	(-D|-O|-P|-T|-X|-b|-d|-l|-n|-o|-p|-t|-x) has_narg=1;;
	(-I|-i) has_sarg=1;;
	(*) usage; exit 1;;
    esac
    if [ $has_sarg -eq 0 -a $has_narg -eq 0 ]; then
	continue;
    fi
    if [ $# -eq 0 ] ; then
	echo "$FLAG needs an argument"
	exit 1;
    fi
    if [ $has_narg -eq 1 ] ; then
	case $1 in
	    ([0-9]*) ;;
	    (*) echo "$FLAG needs a numeric argument"
	        exit 1
		;;
	esac
    fi
    case $FLAG in
	(-D) Pactive_bulk_delay="active_bulk_delay $1";;
	(-I) Pserver_network="server_network=$1";;
	(-O) Pbulk_timeout="bulk_timeout=$1";;
	(-P) Pserver_portal="server_portal=$1";;
	(-T) Pserver_tmid="server_tmid=$1";;
	(-X) Pserver_debug="server_debug=$1";;
	(-b) Pnr_bufs="nr_bufs=$1";;
	(-d) Ppassive_size="passive_size=$1";;
	(-i) Pclient_network="client_network=$1";;
	(-l) Ploops="loops=$1";;
	(-n) Pnr_clients="nr_clients=$1";;
	(-o) Pmsg_timeout="msg_timeout=$1";;
	(-p) Pclient_portal="client_portal=$1";;
	(-t) Pclient_tmid="client_tmid=$1";;
	(-x) Pclient_debug="client_debug=$1";;
    esac
    shift
done

if [ -z "$Pserver_only" -a -z "$Pclient_only" ] ; then
    echo "Error: Specify if server, client or both roles to be run locally"
    usage
    exit 1
fi

# Server parameters
SPARM="$Pserver_only $Pserver_network $Pserver_portal $Pserver_tmid \
$Pactive_bulk_delay $Pserver_debug"

# Client parameters
CPARM="$Pclient_only $Pclient_network $Pclient_portal $Pclient_tmid \
$Pnr_clients $Ploops $Ppassive_size $Pclient_debug"

# Other parameters
OPARM="$Pverbose $Pnr_bufs $Pmsg_timeout $Pbulk_timeout"

echo $OPARM
echo $SPARM
echo $CPARM

. c2t1fs/linux_kernel/st/common.sh

MODLIST="build_kernel_modules/kcolibri.ko"

log='/var/log/kern'
if [ ! -e "$log" ]; then
    log='/var/log/messages'
fi
tailseek=$(( $(stat -c %s "$log") + 1 ))

# insert ST module separately to pass parameters
STMOD=klnetst
unload_all() {
    echo "Aborted! Unloading kernel modules..."
    rmmod $STMOD
    modunload
    modunload_galois
}
trap unload_all EXIT

modload_galois || exit $?
modload || exit $?

insmod net/lnet/st/linux_kernel/$STMOD.ko $OPARM $SPARM $CPARM
if [ $? -eq 0 ] ; then
    if [ -z "$Pclient_only" ] ; then
	msg="Enter EOF to stop the server"
	echo $msg
	while read LINE ; do
	    echo $msg
	done
    fi
    rmmod $STMOD
fi

modunload
modunload_galois

trap "" EXIT

sleep 1
tail -c+$tailseek "$log" | grep ' kernel: '
