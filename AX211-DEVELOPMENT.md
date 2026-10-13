# AX211 driver development

Target: Intel AX211 PCI 8086:7AF0, Bluetooth USB 8087:0033, Intel macOS
Sequoia 15.2+. This fork starts from laobamac d695c97. macOS CI builds Debug
and Release packages and runs sanitized protocol tests before publishing.

The first change fixes unchecked allocation and caller-buffer writes in
AWDL sync-template IOCTLs. A new independent wire-format parser validates
AWDL vendor action headers and bounded little-endian TLVs. It is a foundation
for reception and peer discovery, not an operational radio/AirDrop stack.
The parser is not yet attached to RX/TX, and no feature capabilities are
enabled based on it. Existing firmware and radio behavior remain in use.

Next required work:

- Receive/transmit integration with the Intel HAL, including ACK handling.
- Peer state, election, availability windows and channel scheduling while
  preserving the infrastructure connection and regulatory restrictions.
- Sequoia WCL virtual-interface lifecycle, IPv6 data path and discovery.
- SAE authentication and PMF negotiation for WPA3-only operation.
- Intel Bluetooth firmware/USB lifecycle and coexistence testing.
- Genuine AirDrop transfers, Handoff and Apple Watch unlock with paired
  devices, sleep/wake, reconnect and negative authentication tests.

No working AirDrop, WPA3-only or Watch-unlock claim is made. A passing build
does not qualify these features or the physical adapter. The installed SSD
EFI and working USB are retained independently of experimental releases.

Protocol reference: https://github.com/seemoo-lab/owl/blob/master/src/frame.h
Only public wire-format facts were used; GPL-3 OWL implementation code was
not imported into this GPL-2 fork.
Bluetooth driver: https://github.com/OpenIntelWireless/IntelBluetoothFirmware
Watch requirements: https://support.apple.com/en-us/102442
