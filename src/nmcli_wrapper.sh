#!/bin/sh
if [ "$1" = "device" ] || [ "$1" = "dev" ]; then
    if [ "$2" = "wifi" ] || [ "$2" = "w" ]; then
        if [ "$3" = "list" ] || [ "$3" = "res" ] || [ -z "$3" ]; then
            /usr/local/bin/gnetwork scan
        elif [ "$3" = "connect" ] || [ "$3" = "c" ]; then
            /usr/local/bin/gnetwork connect "$4" "$6" "$8"
        fi
    elif [ "$3" = "status" ] || [ -z "$2" ]; then
        /usr/local/bin/gnetwork status
    fi
elif [ "$1" = "connection" ] || [ "$1" = "con" ] || [ "$1" = "c" ]; then
    if [ "$2" = "up" ]; then
        /usr/local/bin/gnetwork up "$3"
    elif [ "$2" = "down" ]; then
        /usr/local/bin/gnetwork down "$3"
    elif [ "$2" = "show" ] || [ -z "$2" ]; then
        /usr/local/bin/gnetwork status
    fi
elif [ "$1" = "radio" ] || [ "$1" = "r" ]; then
    echo "wifi enabled"
elif [ "$1" = "networking" ] || [ "$1" = "n" ]; then
    echo "enabled"
else
    /usr/local/bin/gnetwork "$@"
fi
