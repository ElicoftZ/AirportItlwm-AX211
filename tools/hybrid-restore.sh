#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
[[ -f "$dir/network-backup.plist" && -f "$dir/hybrid-network" ]] || { echo "Network backup or helper is missing."; exit 1; }
temp="$(mktemp -d /private/tmp/airport-hybrid.XXXXXX)"
trap 'rm -f "$temp/hybrid-network"; rmdir "$temp"' EXIT
cp "$dir/hybrid-network" "$temp/hybrid-network"
chmod 755 "$temp/hybrid-network"
sudo "$temp/hybrid-network" --restore "$dir/network-backup.plist"
echo "Previous macOS network settings restored. The EFI driver can now be restored from Windows."
