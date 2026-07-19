#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#define SOCKET_PATH "/var/run/gnetwork.sock"
#define CONFIG_DIR "/etc/gnetwork"
#define CONFIG_FILE "/etc/gnetwork/gnetwork.conf"
#define RESOLV_CONF "/etc/resolv.conf"

#define CMD_STATUS 1
#define CMD_SCAN 2
#define CMD_CONNECT 3
#define CMD_DISCONNECT 4
#define CMD_SET_STATIC 5
#define CMD_SET_DHCP 6
#define CMD_SET_DNS 7
#define CMD_IF_UP 8
#define CMD_IF_DOWN 9

struct msg_req {
    int cmd;
    char ifname[32];
    char ssid[64];
    char password[64];
    char ip[32];
    char netmask[32];
    char gateway[32];
    char dns1[32];
    char dns2[32];
};

struct msg_res {
    int status;
    char output[4096];
};

#endif
