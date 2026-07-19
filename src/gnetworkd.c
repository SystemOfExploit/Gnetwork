#include "common.h"

static int server_fd = -1;
static volatile int running = 1;

static void execute_command(const char *cmd, char *out, size_t out_len) {
    if (out) out[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    if (out && out_len > 0) {
        size_t len = 0;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            size_t n = strlen(buffer);
            if (len + n < out_len - 1) {
                strcpy(out + len, buffer);
                len += n;
            }
        }
        out[len] = '\0';
    }
    pclose(fp);
}

static void update_resolv_conf(const char *dns1, const char *dns2) {
    FILE *fp = fopen(RESOLV_CONF, "w");
    if (!fp) return;
    if (dns1 && strlen(dns1) > 0) {
        fprintf(fp, "nameserver %s\n", dns1);
    } else {
        fprintf(fp, "nameserver 1.1.1.1\n");
    }
    if (dns2 && strlen(dns2) > 0) {
        fprintf(fp, "nameserver %s\n", dns2);
    } else {
        fprintf(fp, "nameserver 8.8.8.8\n");
    }
    fclose(fp);
}

static void auto_dhcp(const char *ifname) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ip link set %.32s up 2>/dev/null", ifname);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "dhcpcd %.32s 2>/dev/null || udhcpc -i %.32s -n 2>/dev/null || dhclient %.32s 2>/dev/null", ifname, ifname, ifname);
    system(cmd);
}

static void handle_status(struct msg_res *res) {
    char buffer[3500];
    buffer[0] = '\0';
    execute_command("ip -4 addr show", buffer, sizeof(buffer));
    snprintf(res->output, sizeof(res->output), "=== Gnetwork Active Interfaces ===\n%s", buffer);
    res->status = 0;
}

static void handle_scan(const char *ifname, struct msg_res *res) {
    char target_if[32];
    if (strlen(ifname) > 0) {
        strncpy(target_if, ifname, sizeof(target_if) - 1);
        target_if[sizeof(target_if) - 1] = '\0';
    } else {
        strcpy(target_if, "wlan0");
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link set %s up 2>/dev/null", target_if);
    system(cmd);

    char buffer[3500];
    buffer[0] = '\0';
    snprintf(cmd, sizeof(cmd), "iw dev %s scan 2>/dev/null | grep -E 'SSID:|signal:' | head -n 30", target_if);
    execute_command(cmd, buffer, sizeof(buffer));

    if (strlen(buffer) == 0) {
        snprintf(cmd, sizeof(cmd), "nmcli dev wifi list 2>/dev/null || wpa_cli scan_results 2>/dev/null");
        execute_command(cmd, buffer, sizeof(buffer));
    }

    if (strlen(buffer) == 0) {
        snprintf(res->output, sizeof(res->output), "No Wi-Fi networks found on interface %s\n", target_if);
    } else {
        snprintf(res->output, sizeof(res->output), "=== Wi-Fi Scan Results (%s) ===\n%s", target_if, buffer);
    }
    res->status = 0;
}

static void handle_connect(const char *ifname, const char *ssid, const char *pass, struct msg_res *res) {
    char target_if[32];
    if (strlen(ifname) > 0) {
        strncpy(target_if, ifname, sizeof(target_if) - 1);
        target_if[sizeof(target_if) - 1] = '\0';
    } else {
        strcpy(target_if, "wlan0");
    }

    char wpa_conf[256];
    snprintf(wpa_conf, sizeof(wpa_conf), "/etc/gnetwork/wpa_%s.conf", target_if);
    mkdir(CONFIG_DIR, 0755);

    FILE *fp = fopen(wpa_conf, "w");
    if (fp) {
        fprintf(fp, "ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\nnetwork={\n\tssid=\"%s\"\n", ssid);
        if (pass && strlen(pass) > 0) {
            fprintf(fp, "\tpsk=\"%s\"\n", pass);
        } else {
            fprintf(fp, "\tkey_mgmt=NONE\n");
        }
        fprintf(fp, "}\n");
        fclose(fp);
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pkill -f 'wpa_supplicant.*%s' 2>/dev/null", target_if);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_supplicant -B -i %s -c %s 2>/dev/null", target_if, wpa_conf);
    system(cmd);

    auto_dhcp(target_if);
    update_resolv_conf("1.1.1.1", "8.8.8.8");

    snprintf(res->output, sizeof(res->output), "Connected to SSID '%s' on %s\n", ssid, target_if);
    res->status = 0;
}

static void handle_set_static(const struct msg_req *req, struct msg_res *res) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %s", req->ifname);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ip link set %s up", req->ifname);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ip addr add %s/%s dev %s", req->ip, req->netmask[0] ? req->netmask : "24", req->ifname);
    system(cmd);
    if (strlen(req->gateway) > 0) {
        snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %s 2>/dev/null", req->gateway, req->ifname);
        system(cmd);
    }
    if (strlen(req->dns1) > 0) { // github.com/SystemOfExploit
        update_resolv_conf(req->dns1, req->dns2);
    }
    snprintf(res->output, sizeof(res->output), "Interface %s configured with static IP %s\n", req->ifname, req->ip);
    res->status = 0;
}

static void handle_if_control(const char *ifname, int up, struct msg_res *res) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link set %s %s", ifname, up ? "up" : "down");
    int ret = system(cmd);
    if (ret == 0) {
        snprintf(res->output, sizeof(res->output), "Interface %s turned %s\n", ifname, up ? "UP" : "DOWN");
        res->status = 0;
    } else {
        snprintf(res->output, sizeof(res->output), "Failed to change state of interface %s\n", ifname);
        res->status = -1;
    }
}

static void process_client(int client_fd) {
    struct msg_req req;
    struct msg_res res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));

    ssize_t n = read(client_fd, &req, sizeof(req));
    if (n <= 0) {
        close(client_fd);
        return;
    }

    switch (req.cmd) {
        case CMD_STATUS:
            handle_status(&res);
            break;
        case CMD_SCAN:
            handle_scan(req.ifname, &res);
            break;
        case CMD_CONNECT:
            handle_connect(req.ifname, req.ssid, req.password, &res);
            break;
        case CMD_DISCONNECT:
            handle_if_control(req.ifname[0] ? req.ifname : "wlan0", 0, &res);
            break;
        case CMD_SET_STATIC:
            handle_set_static(&req, &res);
            break;
        case CMD_SET_DHCP:
            auto_dhcp(req.ifname[0] ? req.ifname : "eth0");
            snprintf(res.output, sizeof(res.output), "DHCP requested for interface %s\n", req.ifname);
            res.status = 0;
            break;
        case CMD_SET_DNS:
            update_resolv_conf(req.dns1, req.dns2);
            snprintf(res.output, sizeof(res.output), "DNS updated to %s, %s\n", req.dns1, req.dns2[0] ? req.dns2 : "8.8.8.8");
            res.status = 0;
            break;
        case CMD_IF_UP:
            handle_if_control(req.ifname, 1, &res);
            break;
        case CMD_IF_DOWN:
            handle_if_control(req.ifname, 0, &res);
            break;
        default:
            res.status = -1;
            snprintf(res.output, sizeof(res.output), "Unknown command\n");
            break;
    }

    write(client_fd, &res, sizeof(res));
    close(client_fd);
}

static void auto_detect_and_bringup() {
    DIR *dir = opendir("/sys/class/net");
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 || strcmp(ent->d_name, "lo") == 0) continue;
        if (strncmp(ent->d_name, "eth", 3) == 0 || strncmp(ent->d_name, "en", 2) == 0) {
            auto_dhcp(ent->d_name);
        }
    }
    closedir(dir);
}

int main() {
    mkdir(CONFIG_DIR, 0755);
    unlink(SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    chmod(SOCKET_PATH, 0666);

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    pthread_t th;
    if (pthread_create(&th, NULL, (void *(*)(void *))auto_detect_and_bringup, NULL) == 0) {
        pthread_detach(th);
    }

    while (running) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            process_client(client_fd);
        }
    }

    close(server_fd);
    unlink(SOCKET_PATH);
    return 0;
}
