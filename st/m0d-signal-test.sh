#!/usr/bin/env bash

sudo scripts/install-mero-service -u
sudo scripts/install-mero-service -l
sudo utils/m0setup -v -P 12 -N 2 -K 1 -i 1 -d /var/mero/img -s 1 -c
sudo utils/m0setup -v -P 12 -N 2 -K 1 -i 1 -d /var/mero/img -s 1

test_for_signal()
{
	local sig=$1
	echo "------------------ Configuring Mero for $sig test ------------------"
	sudo systemctl start mero-mkfs
	sudo systemctl start mero &

	echo "Waiting for ios1 to become active"
	local pid=""
	while true; do
		pid=`sudo ps ax | grep "m0d -e" | grep "ios1.conf" | awk '{ print $1 }'`
		if [ -n "$pid" ]; then
			echo "Send $sig to ios1 ($pid)"
			sudo kill -s $sig $pid
			echo "Waiting for ios1 to stop"
			sleep 10
			break
		fi
		sleep 1
	done

	local result=0
	gotsignal=`sudo journalctl --no-pager --full -b -n 50 | grep "mero-server\[$pid\]" | grep "Got signal during Mero setup"`
	if [ -n "$gotsignal" ]; then
		echo "Successfully handled $sig during Mero setup"

		if test "x$sig" = "xSIGUSR1"; then
			restarting=`sudo journalctl --no-pager --full -b -n 50 | grep "mero-server\[$pid\]" | grep "Restarting"`
			if [ -n "$restarting" ]; then
				echo "Wait for Mero restart"
				status=`sudo systemctl status mero.service | grep Active: | sed -e 's/[[:space:]]*Active:[[:space:]]*//' -e 's/ (.*$//g'`
				echo "mero.service.status=$status"
				if test "x$status" != "xactive"; then
					echo "Mero service is not in active status"
					result=1
				else
					local SLEEP_MAX=60
					local SPEEP_COUNT=0
					while true; do
						started=`sudo journalctl --no-pager --full -b -n 50 | grep "mero-server\[$pid\]" | grep "Started"`
						if [ -n "x$started" ] ; then
							echo "Successfully restarted after $sig during Mero setup"
							result=0
							break
						else
							if test $SLEEP_COUNT -eq $SLEEP_MAX; then
								echo "Failed to wait for Mero to restart after signal"
								result=1
								break
							fi
						fi
						((SLEEP_COUNT++))
						sleep 1
					done
				fi
			else
				echo "Restarting Mero failed"
				result=1
			fi
		else
			result=0
		fi
	else
		echo "Failed to handle $sig during Mero setup"
		result=1
	fi

	echo "Stopping Mero"
	sudo systemctl stop mero
	return $result
}

# Test for stop
test_for_signal SIGTERM || exit $?

# Test for restart
test_for_signal SIGUSR1 || exit $?
exit 0
