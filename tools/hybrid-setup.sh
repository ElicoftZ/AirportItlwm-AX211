#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
backup="$dir/network-backup.plist"
if [[ "${1:-}" != "--test-only" ]]; then
    [[ ! -e "$backup" ]] || { echo "A network backup already exists. Use --test-only, or RESTORE-NETWORK.sh before another setup."; exit 1; }
    [[ -f "$dir/hybrid-network" ]] || { echo "hybrid-network helper is missing."; exit 1; }
    # Copy off FAT32 so executable mount flags do not affect the helper.
    temp="$(mktemp -d /private/tmp/airport-hybrid.XXXXXX)"
    trap 'rm -f "$temp/hybrid-network"; rmdir "$temp"' EXIT
    cp "$dir/hybrid-network" "$temp/hybrid-network"
    chmod 755 "$temp/hybrid-network"
    sudo "$temp/hybrid-network" --apply "$backup"
fi
data="$(ioreg -r -c AirportItlwm -d 2 | awk -F '"' '/"HybridDataBSDName" = / { print $(NF-1); exit }')"
[[ "$data" =~ ^(en|itl)[0-9]+$ ]] || { echo "The hybrid data interface was not found. Run RESTORE-NETWORK.sh to undo setup."; exit 1; }
report="$dir/REPORT-$(date +%Y%m%d-%H%M%S)"
mkdir "$report"
sw_vers > "$report/macos.txt"
kmutil showloaded 2>&1 | awk '/AirportItlwm/' > "$report/loaded-driver.txt" || true
ioreg -r -c AirportItlwm -d 2 > "$report/driver-before.txt"
ifconfig "$data" > "$report/data-interface-before.txt"
route -n get default > "$report/default-route-before.txt" 2>&1 || true
echo "Join your network using the normal Wi-Fi menu before this test. Ethernet data interface: $data"
# Allow DHCP to settle, and save an offline report as well as successful tests.
sleep 3
set +e
networkQuality -I "$data" -v 2>&1 | tee "$report/networkQuality.txt"
quality_status=${PIPESTATUS[0]}
set -e
ioreg -r -c AirportItlwm -d 2 > "$report/driver-after.txt"
ifconfig "$data" > "$report/data-interface-after.txt"
route -n get default > "$report/default-route-after.txt" 2>&1 || true
printf 'networkQuality exit status: %s\nData interface: %s\n' "$quality_status" "$data" > "$report/result.txt"
echo "Report saved: $report"
echo "If connectivity is worse, run: bash \"$dir/RESTORE-NETWORK.sh\""
exit "$quality_status"
