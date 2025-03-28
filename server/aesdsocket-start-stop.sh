#!/bin/sh

### BEGIN INIT INFO
# Provides		:	aesdsocket
# Required-Start	:	$remote_fs $syslog
# Required-Stop		:	$remote_fs $syslog
# Default-Start		:	2 3 4 5
# Default-Stop		:	0 1 6
# Short-Description	:	Start the aesdsocket daemon
# Description		:	Uses start-stop-daemon to launch and stop the aesdsocket daemon.
### END INIT INFO

DAEMON=/usr/bin/aesdsocket
DAEMON_OPTS="-d"
PIDFILE=/var/run/aesdsocket.pid

case "$1" in 
	start)
		echo "Starting aesdsocket daemon..."
		start-stop-daemon --start --quiet --background --make-pidfile --pidfile $PIDFILE --exec $DAEMON -- $DAEMON_OPTS
		;;
	stop)
		echo "Stoping aesdsocket daemon..."
		start-stop-daemon --stop --quiet --pidfile $PIDFILE --retry SIGTERM
		;;
	restart)
		$0 stop
		$0 start 
		;;
	*)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
		;;
	esac
exit 0


