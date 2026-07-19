# Gnetwork - Lightweight Network Manager

Gnetwork is a lightweight, high-speed C-based network management suite designed for Linux. It supports automatic carrier detection, Ethernet auto-configuration, Wi-Fi scanning and connections, DNS configuration, and static IP management.

Gnetwork supports all major init systems out of the box:
- **systemd**
- **dinit**
- **Granit-Init** (https://github.com/SystemOfExploit/Granit-Init)
- **runit**
- **OpenRC**
- **suite66 (66)**

---

## Installation

Run `Setup.sh` as root:

```bash
chmod +x Setup.sh
sudo ./Setup.sh
```

This compiles `gnetworkd` and `gnetwork`, installs binaries to `/usr/local/bin`, prepares `/etc/gnetwork`, installs unit/service definitions for all supported init systems, and configures `systemctl` compatibility.

---

## Enabling & Starting Service

On systemd:
```bash
sudo systemctl enable --now Gnetwork
```

On OpenRC:
```bash
sudo rc-update add gnetwork default
sudo rc-service gnetwork start
```

On dinit:
```bash
sudo dinitctl enable gnetwork
sudo dinitctl start gnetwork
```

On Granit-Init:
```bash
sudo granit service enable Gnetwork
sudo granit service start Gnetwork
```

On runit:
```bash
sudo ln -s /etc/sv/gnetwork /var/service/
```

On suite66:
```bash
sudo 66-enable gnetwork
sudo 66-start gnetwork
```

*Note: On non-systemd systems, running `systemctl enable Gnetwork` or `systemctl start Gnetwork` is also supported via the built-in systemctl wrapper!*

---

## CLI Usage (`gnetwork`)

### 1. Show Network Status
```bash
gnetwork status
```

### 2. Scan Wi-Fi Networks
```bash
gnetwork scan
gnetwork scan wlan0
```

### 3. Connect to Wi-Fi
```bash
gnetwork connect "MyWiFiNetwork" "WiFiPassword" wlan0
```

### 4. Configure Static IP
```bash
gnetwork set-ip eth0 192.168.1.100/24 192.168.1.1
```

### 5. Request DHCP
```bash
gnetwork set-dhcp eth0
```

### 6. Set Custom DNS Resolvers
```bash
gnetwork set-dns 1.1.1.1 8.8.8.8
```

### 7. Interface Control
```bash
gnetwork up eth0
gnetwork down eth0
```
