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

#include <sys/ioctl.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

struct dhcp_msg {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t cookie;
    uint8_t options[308];
} __attribute__((packed));

static uint16_t dhcp_checksum(uint16_t *buf, int nwords) {
    uint32_t sum = 0;
    for (; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

static int builtin_dhcp(const char *ifname) {
    int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) return -1;

    int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
    if (sock < 0) return -1;

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int ctl_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctl_sock < 0) { close(sock); return -1; }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(ctl_sock, SIOCGIFHWADDR, &ifr) < 0) {
        close(ctl_sock);
        close(sock);
        return -1;
    }
    close(ctl_sock);

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_IP);
    sll.sll_ifindex = ifindex;
    sll.sll_hatype = ARPHRD_ETHER;
    sll.sll_pkttype = PACKET_BROADCAST;
    sll.sll_halen = 6;
    memset(sll.sll_addr, 0xff, 6);

    uint32_t xid = (uint32_t)rand();

    uint8_t buffer[600];
    memset(buffer, 0, sizeof(buffer));

    struct iphdr *iph = (struct iphdr *)buffer;
    struct udphdr *udph = (struct udphdr *)(buffer + sizeof(struct iphdr));
    struct dhcp_msg *dhcp = (struct dhcp_msg *)(buffer + sizeof(struct iphdr) + sizeof(struct udphdr));

    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = xid;
    dhcp->flags = htons(0x8000);
    memcpy(dhcp->chaddr, ifr.ifr_hwaddr.sa_data, 6);
    dhcp->cookie = htonl(0x63825363);

    int opt_idx = 0;
    dhcp->options[opt_idx++] = 53;
    dhcp->options[opt_idx++] = 1;
    dhcp->options[opt_idx++] = 1;

    dhcp->options[opt_idx++] = 55;
    dhcp->options[opt_idx++] = 3;
    dhcp->options[opt_idx++] = 1;
    dhcp->options[opt_idx++] = 3;
    dhcp->options[opt_idx++] = 6;
    dhcp->options[opt_idx++] = 255;

    size_t dhcp_len = sizeof(struct dhcp_msg);
    size_t udp_len = sizeof(struct udphdr) + dhcp_len;
    size_t ip_len = sizeof(struct iphdr) + udp_len;

    udph->source = htons(68);
    udph->dest = htons(67);
    udph->len = htons(udp_len);
    udph->check = 0;

    iph->version = 4;
    iph->ihl = 5;
    iph->tos = 0;
    iph->tot_len = htons(ip_len);
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = IPPROTO_UDP;
    iph->saddr = 0;
    iph->daddr = INADDR_BROADCAST;
    iph->check = dhcp_checksum((uint16_t *)iph, sizeof(struct iphdr) / 2);

    if (sendto(sock, buffer, ip_len, 0, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        close(sock);
        return -1;
    }

    uint8_t recv_buf[1024];
    uint32_t offered_ip = 0, subnet_mask = 0, gateway_ip = 0, dns_ip = 0;
    int offer_received = 0;

    for (int retry = 0; retry < 5; retry++) {
        ssize_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < (ssize_t)(sizeof(struct iphdr) + sizeof(struct udphdr) + 240)) continue;

        struct iphdr *riph = (struct iphdr *)recv_buf;
        if (riph->protocol != IPPROTO_UDP) continue;

        int ip_hdr_len = riph->ihl * 4;
        struct udphdr *rudph = (struct udphdr *)(recv_buf + ip_hdr_len);
        if (ntohs(rudph->dest) != 68) continue;

        struct dhcp_msg *rdhcp = (struct dhcp_msg *)(recv_buf + ip_hdr_len + sizeof(struct udphdr));
        if (rdhcp->xid == xid && rdhcp->op == 2) {
            offered_ip = rdhcp->yiaddr;
            int i = 0;
            while (i < 300 && rdhcp->options[i] != 255) {
                uint8_t tag = rdhcp->options[i++];
                if (tag == 0) continue;
                uint8_t len = rdhcp->options[i++];
                if (tag == 1 && len >= 4) memcpy(&subnet_mask, &rdhcp->options[i], 4);
                else if (tag == 3 && len >= 4) memcpy(&gateway_ip, &rdhcp->options[i], 4);
                else if (tag == 6 && len >= 4) memcpy(&dns_ip, &rdhcp->options[i], 4);
                i += len;
            }
            offer_received = 1;
            break;
        }
    }

    if (!offer_received || offered_ip == 0) {
        close(sock);
        return -1;
    }

    memset(buffer, 0, sizeof(buffer));
    iph = (struct iphdr *)buffer;
    udph = (struct udphdr *)(buffer + sizeof(struct iphdr));
    dhcp = (struct dhcp_msg *)(buffer + sizeof(struct iphdr) + sizeof(struct udphdr));

    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = xid;
    dhcp->flags = htons(0x8000);
    memcpy(dhcp->chaddr, ifr.ifr_hwaddr.sa_data, 6);
    dhcp->cookie = htonl(0x63825363);

    opt_idx = 0;
    dhcp->options[opt_idx++] = 53;
    dhcp->options[opt_idx++] = 1;
    dhcp->options[opt_idx++] = 3;

    dhcp->options[opt_idx++] = 50;
    dhcp->options[opt_idx++] = 4;
    memcpy(&dhcp->options[opt_idx], &offered_ip, 4);
    opt_idx += 4;

    dhcp->options[opt_idx++] = 255;

    udph->source = htons(68);
    udph->dest = htons(67);
    udph->len = htons(udp_len);
    udph->check = 0;

    iph->version = 4;
    iph->ihl = 5;
    iph->tos = 0;
    iph->tot_len = htons(ip_len);
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = IPPROTO_UDP;
    iph->saddr = 0;
    iph->daddr = INADDR_BROADCAST;
    iph->check = dhcp_checksum((uint16_t *)iph, sizeof(struct iphdr) / 2);

    sendto(sock, buffer, ip_len, 0, (struct sockaddr *)&sll, sizeof(sll));
    recvfrom(sock, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
    close(sock);

    char ip_str[INET_ADDRSTRLEN], gw_str[INET_ADDRSTRLEN], dns_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &offered_ip, ip_str, sizeof(ip_str));

    int cidr = 24;
    if (subnet_mask != 0) {
        uint32_t mask = ntohl(subnet_mask);
        cidr = 0;
        while (mask) { if (mask & 1) cidr++; mask >>= 1; }
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ip addr flush dev %.32s 2>/dev/null", ifname);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ip addr add %s/%d dev %.32s 2>/dev/null", ip_str, cidr, ifname);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ip link set %.32s up 2>/dev/null", ifname);
    system(cmd);

    if (gateway_ip != 0) {
        inet_ntop(AF_INET, &gateway_ip, gw_str, sizeof(gw_str));
        snprintf(cmd, sizeof(cmd), "ip route add default via %s dev %.32s 2>/dev/null", gw_str, ifname);
        system(cmd);
    }

    if (dns_ip != 0) {
        inet_ntop(AF_INET, &dns_ip, dns_str, sizeof(dns_str));
        update_resolv_conf(dns_str, "8.8.8.8");
    } else {
        update_resolv_conf("1.1.1.1", "8.8.8.8");
    }

    return 0;
}

static void auto_dhcp(const char *ifname) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ip link set %.32s up 2>/dev/null", ifname);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "dhcpcd %.32s 2>/dev/null || udhcpc -i %.32s -n 2>/dev/null || dhclient %.32s 2>/dev/null || busybox udhcpc -i %.32s -n 2>/dev/null", ifname, ifname, ifname, ifname);
    int res = system(cmd);

    if (res != 0) {
        builtin_dhcp(ifname);
    }
}

static void handle_status(struct msg_res *res) {
    char buffer[3500];
    buffer[0] = '\0';
    execute_command("ip addr show", buffer, sizeof(buffer));
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
        if (strncmp(ent->d_name, "eth", 3) == 0 || strncmp(ent->d_name, "en", 2) == 0 || strncmp(ent->d_name, "wl", 2) == 0) {
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
