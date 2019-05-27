#!/bin/bash

processes=$(mktemp /tmp/beak_monitor_psXXXXXXXX)
files=$(mktemp /tmp/beak_monitor_filesXXXXXXXX)

while :
do
    if [ -d /dev/shm/beak-$USER ]
    then
        ps -e -o pid | sort -n > $processes
        ls /dev/shm/beak-$USER/ | sort -n > $files

        PIDS=$(grep -f $processes $files)

        tput clear
        for x in $PIDS
        do
            cat /dev/shm/beak-$USER/$x
        done

    fi
    # script is executed every second
    sleep 1
done
