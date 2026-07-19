#include "common.h"

static void print_usage(const char *prog) {
    printf("Gnetwork CLI Control Tool\n");
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  status                       Show status of network interfaces\n");
    printf("  scan [interface]             Scan for Wi-Fi networks (default: wlan0)\n");
    printf("  connect <ssid> [pass] [if]   Connect to a Wi-Fi network\n");
    printf("  disconnect [interface]       Disconnect Wi-Fi interface\n");
    printf("  set-ip <if> <ip/prefix> [gw] Set static IP address\n");
    printf("  set-dhcp <interface>         Request IP via DHCP\n");
    printf("  set-dns <dns1> [dns2]        Set system DNS resolvers\n");
    printf("  up <interface>               Bring network interface UP\n");
    printf("  down <interface>             Bring network interface DOWN\n");
}

static int send_request(const struct msg_req *req, struct msg_res *res) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error: Unable to connect to gnetworkd daemon. Is Gnetwork running?\n");
        close(fd);
        return -1;
    }

    write(fd, req, sizeof(*req));
    memset(res, 0, sizeof(*res));
    read(fd, res, sizeof(*res));
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    struct msg_req req;
    struct msg_res res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    const char *cmd = argv[1];

    if (strcmp(cmd, "status") == 0) {
        req.cmd = CMD_STATUS;
    } else if (strcmp(cmd, "scan") == 0) {
        req.cmd = CMD_SCAN;
        if (argc >= 3) strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
    } else if (strcmp(cmd, "connect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s connect <ssid> [password] [interface]\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_CONNECT;
        strncpy(req.ssid, argv[2], sizeof(req.ssid) - 1);
        if (argc >= 4) strncpy(req.password, argv[3], sizeof(req.password) - 1);
        if (argc >= 5) strncpy(req.ifname, argv[4], sizeof(req.ifname) - 1);
    } else if (strcmp(cmd, "disconnect") == 0) {
        req.cmd = CMD_DISCONNECT;
        if (argc >= 3) strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
    } else if (strcmp(cmd, "set-ip") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s set-ip <interface> <ip/prefix> [gateway]\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_SET_STATIC;
        strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
        char *slash = strchr(argv[3], '/');
        if (slash) {
            *slash = '\0';
            strncpy(req.ip, argv[3], sizeof(req.ip) - 1);
            strncpy(req.netmask, slash + 1, sizeof(req.netmask) - 1);
        } else {
            strncpy(req.ip, argv[3], sizeof(req.ip) - 1);
            strcpy(req.netmask, "24");
        }
        if (argc >= 5) strncpy(req.gateway, argv[4], sizeof(req.gateway) - 1);
    } else if (strcmp(cmd, "set-dhcp") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s set-dhcp <interface>\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_SET_DHCP;
        strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
    } else if (strcmp(cmd, "set-dns") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s set-dns <primary_dns> [secondary_dns]\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_SET_DNS;
        strncpy(req.dns1, argv[2], sizeof(req.dns1) - 1);
        if (argc >= 4) strncpy(req.dns2, argv[3], sizeof(req.dns2) - 1);
    } else if (strcmp(cmd, "up") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s up <interface>\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_IF_UP;
        strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
    } else if (strcmp(cmd, "down") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s down <interface>\n", argv[0]);
            return 1;
        }
        req.cmd = CMD_IF_DOWN;
        strncpy(req.ifname, argv[2], sizeof(req.ifname) - 1);
    } else {
        print_usage(argv[0]);
        return 1;
    }

    if (send_request(&req, &res) == 0) {
        printf("%s", res.output);
        return res.status;
    }
    return 1;
}
