# This test should be run with root privileges.
set -x
cd ../..
ulimit -c unlimited

Port="10001"
Addr="127.0.0.1"
echo "Server parameters: $Addr:$Port"

(./stob/ut/server -d/tmp/ -p$Port &)
sleep 1
(./net/st/connection 127.0.0.1 $Port)
killall lt-server
echo ======================done=====================
