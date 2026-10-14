# AX211 driver development

Target: Intel AX211 PCI 8086:7AF0, Bluetooth USB 8087:0033, Intel macOS
Sequoia 15.2+. This fork starts from laobamac d695c97. macOS CI builds Debug
and Release packages and runs sanitized protocol tests before publishing.

The first change fixes unchecked allocation and caller-buffer writes in
AWDL sync-template IOCTLs. A new independent wire-format parser validates
AWDL vendor action headers and bounded little-endian TLVs. It is a foundation
for reception and peer discovery, not an operational radio/AirDrop stack.
The parser is attached to an opt-in receive observer (`awdl_observe=1`). A
bounded 16-entry table records valid unicast peer identities and ages entries
when subsequent frames arrive. Only peer counts, frame counts and observation
timestamps are published; peer addresses are not published. Observation does
not authenticate peers or establish an AWDL link. It uses frames the firmware
already delivers on its current channel; firmware filtering may deliver none.
No firmware filters, channels or AirDrop/Watch capabilities are changed.

IORegistry properties on AirportItlwm: AWDLObservationEnabled,
AWDLObservedFrames, AWDLObservedMalformed, AWDLObservedPeerCount and
AWDLObservationTimestampMS. Peer count describes the last observation, not
a continuously refreshed or authenticated peer list. This build still cannot
transmit AWDL, schedule availability windows or complete an AirDrop transfer.

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

## RX throughput investigation

The supplied macOS diagnostics show WPA2-Personal, 802.11ac, channel 44 at
80 MHz, two spatial streams, a 585 Mbps TX PHY rate and -62 dBm RSSI. A later
networkQuality result after the timer build still shows severe low throughput.
The timer repairs have not established a working speed fix. A PHY rate does
not establish the RX rate or the cause. Building does not change the EFI.

The AX211 iwx receive path now recreates both BlockAck timers after teardown:
`iwx_clear_reorder_buffer` frees the timer objects, while previously the next
session tried to schedule the null objects. This also affects adapter stop
and restart. A missing gap timer can strand packets behind a sequence gap.

An existing gap timer is preserved while later MPDUs arrive. If its callback
finds a newer unexpired frame, it schedules only that frame's remaining
lifetime, rounded up to milliseconds. A-MSDU subframes retain the first
arrival time. The expiry scan wraps sequence numbers at 4096 before buffer
indexing, including non-power-of-two negotiated windows.

Session inactivity uses the peer's negotiated BlockAck timeout rather than
the separate fixed 100 ms reorder timeout. Zero remains disabled. Many APs
negotiate zero, so the inactivity repair is not a universal speed fix.

CI runs both helper tests and the actual timer/init/clear/release methods in
a host fixture with fake clocks, timers and packet delivery. The fixture
tests repeated arrivals across a missing sequence, remaining deadlines,
A-MSDU release flags, sequence wrap and multiple teardown/restart cycles.
These checks do not exercise the Intel firmware, native packet stack or RF.
Debug and Release Sequoia builds are also required before publication.

Upstream also reports that frequent Airport-requested scanning can reduce
throughput. The current firmware already has home/away scan timing controls;
this change does not suppress scans or claim that scanning explains this PC.

Opt-in `itlwm_diag=1` publishes packet-error, BlockAck, CCMP and native scan
counters to IORegistry once per watchdog interval (one second). Publication
does not run in the packet path and is disabled by default. Scan-attempt
counts reflect calls requesting firmware work, not confirmed radio scans.
These measurements are intended to identify stalls; they are not a speed fix.

References:
- [Upstream slow-throughput discussion](https://github.com/OpenIntelWireless/itlwm/issues/621)
- [Linux negotiated BA inactivity handling](https://github.com/torvalds/linux/blob/master/net/mac80211/agg-rx.c)
- [Apple packet delivery and output scheduling](https://github.com/apple-oss-distributions/IONetworkingFamily/blob/main/IONetworkInterface.cpp)

Protocol reference: https://github.com/seemoo-lab/owl/blob/master/src/frame.h
Only public wire-format facts were used; GPL-3 OWL implementation code was
not imported into this GPL-2 fork.
Bluetooth driver: https://github.com/OpenIntelWireless/IntelBluetoothFirmware
Watch requirements: https://support.apple.com/en-us/102442
