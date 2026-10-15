#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
hybrid_data_name() {
    # Consume the entire ioreg stream: exiting awk after the first match makes
    # ioreg die from SIGPIPE and aborts this script under pipefail (exit 141).
    ioreg -r -c AirportItlwm -d 2 | awk -F '"' '/"HybridDataBSDName" = / && !seen { print $(NF-1); seen=1 }'
}
if [[ "${1:-}" == "--self-test" ]]; then
    ioreg() { awk 'BEGIN { print "\"HybridDataBSDName\" = \"itl0\""; for (i=0;i<25000;i++) print "padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding" }'; }
    [[ "$(hybrid_data_name)" == "itl0" ]]
    echo "Large ioreg stream discovery passed under pipefail."
    exit 0
fi
dir="$(cd "$(dirname "$0")" && pwd)"
backup="$dir/network-backup.plist"
if [[ "${1:-}" != "--test-only" ]]; then
    [[ -f "$dir/hybrid-network" ]] || { echo "hybrid-network helper is missing."; exit 1; }
    # Copy off FAT32 so executable mount flags do not affect the helper.
    temp="$(mktemp -d /private/tmp/airport-hybrid.XXXXXX)"
    trap 'rm -f "$temp/hybrid-network"; rmdir "$temp"' EXIT
    cp "$dir/hybrid-network" "$temp/hybrid-network"
    chmod 755 "$temp/hybrid-network"
    if [[ -e "$backup" ]]; then
        sudo "$temp/hybrid-network" --repair "$backup"
    else
        sudo "$temp/hybrid-network" --apply "$backup"
    fi
fi
data="$(hybrid_data_name)"
control="$(ioreg -r -c AirportItlwm -d 2 | awk -F '"' '/"HybridControlBSDName" = / && !seen { print $(NF-1); seen=1 }')"
if [[ "${1:-}" != "--test-only" && "$control" =~ ^en[0-9]+$ ]]; then
    sudo networksetup -setairportpower "$control" on
fi
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
for attempt in {1..15}; do
    if ifconfig "$data" | awk '/inet / && $2 !~ /^169\.254\./ { ready=1 } END { exit !ready }'; then break; fi
    sleep 3
done
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
