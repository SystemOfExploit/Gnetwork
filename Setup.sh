#!/bin/sh

echo "
   ____            _                      _    
  / ___|_ __   ___| |___      _____  _ __| | __
 | |  _| '_ \ / _ \ __\ \ /\ / / _ \| '__| |/ /
 | |_| | | | |  __/ |_ \ V  V / (_) | |  |   < 
  \____|_| |_|\___|\__| \_/\_/ \___/|_|  |_|\_\

            !Maded by SystemOfExploit!
"

set -e

make clean || true
make

mkdir -p /usr/local/bin /usr/bin /etc/gnetwork

cp -f gnetworkd /usr/local/bin/gnetworkd
cp -f gnetwork /usr/local/bin/gnetwork
cp -f src/nmcli_wrapper.sh /usr/local/bin/gnetwork-nmcli
chmod 755 /usr/local/bin/gnetworkd /usr/local/bin/gnetwork /usr/local/bin/gnetwork-nmcli

ln -sf /usr/local/bin/gnetworkd /usr/bin/gnetworkd
ln -sf /usr/local/bin/gnetwork /usr/bin/gnetwork

if ! command -v nmcli >/dev/null 2>&1; then
    ln -sf /usr/local/bin/gnetwork-nmcli /usr/local/bin/nmcli
    ln -sf /usr/local/bin/gnetwork-nmcli /usr/bin/nmcli
fi

if [ -d /etc/systemd/system ]; then
    cp -f init/systemd/gnetwork.service /etc/systemd/system/gnetwork.service
    cp -f init/systemd/gnetwork.service /etc/systemd/system/Gnetwork.service
    chmod 644 /etc/systemd/system/gnetwork.service /etc/systemd/system/Gnetwork.service
fi

if [ -d /etc/dinit.d ]; then
    cp -f init/dinit/gnetwork /etc/dinit.d/gnetwork
    cp -f init/dinit/gnetwork /etc/dinit.d/Gnetwork
    chmod 644 /etc/dinit.d/gnetwork /etc/dinit.d/Gnetwork
fi

mkdir -p /etc/granit/services 2>/dev/null || true
if [ -d /etc/granit/services ] || [ -f /usr/bin/granit ] || [ -f /usr/local/bin/granit ]; then
    cp -f init/granit/gnetwork /etc/granit/services/gnetwork
    cp -f init/granit/gnetwork /etc/granit/services/Gnetwork
    chmod 644 /etc/granit/services/gnetwork /etc/granit/services/Gnetwork
fi

if [ -d /etc/sv ]; then
    mkdir -p /etc/sv/gnetwork /etc/sv/Gnetwork
    cp -f init/runit/run /etc/sv/gnetwork/run
    cp -f init/runit/run /etc/sv/Gnetwork/run
    chmod 755 /etc/sv/gnetwork/run /etc/sv/Gnetwork/run
fi

if [ -d /etc/init.d ]; then
    cp -f init/openrc/gnetwork /etc/init.d/gnetwork
    cp -f init/openrc/gnetwork /etc/init.d/Gnetwork
    chmod 755 /etc/init.d/gnetwork /etc/init.d/Gnetwork
fi

if [ -d /etc/66/service ]; then
    cp -f init/suite66/gnetwork /etc/66/service/gnetwork
    cp -f init/suite66/gnetwork /etc/66/service/Gnetwork
    chmod 644 /etc/66/service/gnetwork /etc/66/service/Gnetwork
fi

if ! command -v systemctl >/dev/null 2>&1; then
    cat << 'EOF' > /usr/local/bin/systemctl
#!/bin/sh
ACTION="$1"
SERVICE="$2"
if [ "$ACTION" = "enable" ] || [ "$ACTION" = "start" ]; then
    if [ -x /etc/init.d/gnetwork ]; then
        /etc/init.d/gnetwork start 2>/dev/null || true
        if command -v rc-update >/dev/null 2>&1; then
            rc-update add gnetwork default 2>/dev/null || true
        fi
    elif [ -d /etc/sv/gnetwork ]; then
        ln -sf /etc/sv/gnetwork /var/service/ 2>/dev/null || ln -sf /etc/sv/gnetwork /run/runit/service/ 2>/dev/null || true
    elif command -v dinitctl >/dev/null 2>&1; then
        dinitctl enable gnetwork 2>/dev/null || dinitctl start gnetwork 2>/dev/null || true
    elif command -v 66-enable >/dev/null 2>&1; then
        66-enable gnetwork 2>/dev/null || 66-start gnetwork 2>/dev/null || true
    else
        pkill -f gnetworkd 2>/dev/null || true
        /usr/local/bin/gnetworkd &
    fi
    echo "Gnetwork enabled and started successfully."
elif [ "$ACTION" = "disable" ] || [ "$ACTION" = "stop" ]; then
    pkill -f gnetworkd 2>/dev/null || true
    echo "Gnetwork stopped."
elif [ "$ACTION" = "status" ]; then
    /usr/local/bin/gnetwork status
fi
EOF
    chmod 755 /usr/local/bin/systemctl
fi

pkill -f NetworkManager 2>/dev/null || true
pkill -f gnetworkd 2>/dev/null || true

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
    systemctl daemon-reload 2>/dev/null || true
    systemctl enable --now gnetwork 2>/dev/null || systemctl start gnetwork 2>/dev/null || true
elif command -v rc-service >/dev/null 2>&1; then
    rc-update add gnetwork default 2>/dev/null || true
    rc-service gnetwork restart 2>/dev/null || rc-service gnetwork start 2>/dev/null || true
elif command -v dinitctl >/dev/null 2>&1; then
    dinitctl enable gnetwork 2>/dev/null || true
    dinitctl restart gnetwork 2>/dev/null || dinitctl start gnetwork 2>/dev/null || true
elif command -v granit >/dev/null 2>&1; then
    granit service enable Gnetwork 2>/dev/null || true
    granit service start Gnetwork 2>/dev/null || true
elif [ -d /etc/sv/gnetwork ]; then
    ln -sf /etc/sv/gnetwork /var/service/ 2>/dev/null || ln -sf /etc/sv/gnetwork /run/runit/service/ 2>/dev/null || true
elif command -v 66-enable >/dev/null 2>&1; then
    66-enable gnetwork 2>/dev/null || true
    66-start gnetwork 2>/dev/null || true
    /usr/local/bin/gnetworkd &
fi

if ! pgrep -x gnetworkd >/dev/null 2>&1; then
    /usr/local/bin/gnetworkd &
fi

sleep 1
/usr/local/bin/gnetwork status || true

echo "Gnetwork installation & auto-configuration completed successfully! Network is online."
