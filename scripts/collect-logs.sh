#!/bin/bash
# Read-only capture after a full connection attempt. No password collection.
set -u
destination="${1:-$HOME/Desktop/MT7922-WiFi-logs-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$destination"
sw_vers > "$destination/system.txt"
uname -m >> "$destination/system.txt"
ioreg -r -c itlmt7922 -l -w0 | sed -E 's/("itl_psk[0-9]*" = ).*/\1"<redacted>"/' > "$destination/driver-ioreg.txt"
ioreg -r -c itlmt7922Eth -l -w0 > "$destination/ethernet-ioreg.txt"
kmutil showloaded > "$destination/loaded-kexts.txt" 2>&1
networksetup -listallhardwareports > "$destination/network-ports.txt" 2>&1
ifconfig -a > "$destination/interfaces.txt" 2>&1
# Driver logs are also mirrored into m0/m1/m2/m3-log and wifi-events in ioreg.
log show --last 10m --style compact --predicate 'eventMessage CONTAINS "itlmt7922"' \
    > "$destination/unified-log.txt" 2>&1
printf '日志已保存：%s\n' "$destination"
printf '日志含附近网络名称、MAC 和 IP；不包含 Wi-Fi 密码。\n'
