#!/bin/sh

set -e

BROOT=${PWD%/*}

N=(
	2
	3
	4
	5
	6
	7
	8
	10
	11
	12
	13
	14
	15
	16
	18
	20
)

K=1

P=(
	4
	5
	6
	7
	8
	9
	10
	15
	20
	25
	30
	40
	50
	60
	70
	80
)
	
U=(
	4096
	8192
	16384
	32768
	65536
	131072
	262144
	131072
	262144
	524288
	1024000
	1024000
	1024000
	1024000
	1024000
	1024000
)

cleanup()
{
	umount /mnt/m0

	pkill -INT -f m0d

	while ps ax | grep -v grep | grep m0d; do
		echo -n .; sleep 2;
	done
	rmmod m0mero galois
}

main()
{
	for ((i = 0; i < ${#P[*]}; i++)); do
		cmd="$BROOT/core/scripts/m0mount.sh -a -L -n 1 -d ${N[$i]} -p ${P[$i]} -u ${U[$i]} -q"
		if ! $cmd
		then
			echo "Cannot start mero service"
			return 1
		fi

		cmd="dd if=/dev/zero of=/mnt/m0/dat bs=30M count=10000"
		if ! $cmd
		then
			echo "write failed"
			cleanup
			return 1
		fi

		cmd="$BROOT/core/sns/cm/st/m0repair -O 2 -U ${U[$i]} -F ${U[$i]} -n 1
			-s 300000000000 -N ${N[$i]} -K 1 -P ${P[$i]} -C 172.18.50.45@o2ib:12345:41:102
			-S 172.18.50.45@o2ib:12345:41:101"
		if ! $cmd 
		then
			echo "SNS Repair failed"
			cleanup
			return 1
		else
			echo "SNS Repair done."
		fi

		cleanup
	done

	return 0
}

#trap unprepare EXIT

#set -x

main
