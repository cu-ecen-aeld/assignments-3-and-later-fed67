PID=-1

do_start() {
    /usr/bin/aesdsocket -d
    PID=$(pgrep aesdsocket)
}

do_stop() {
    if [ PID -ne -1 ];
    then
        kill $PID
    fi;
}

case "$1" in
    start)
        echo "Starting asedsocket"
        do_start
        ;;
    stop)
        echo "Stopping asedsocket"
        do_stop
        ;;
    *)
        echo "Usage: "$1" {start|stop}"
        exit 1
esac

exit 0