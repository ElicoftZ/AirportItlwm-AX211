# Native Wi-Fi controls with Ethernet IP traffic

Experimental Sequoia mode: `itlwm_ethernet=1`. The original driver branch and
existing SSD EFI remain independent rollback checkpoints. This mode is off
without the boot argument; it is not a second driver that claims the PCI card.

The native WCL/Skywalk interface still handles scanning, credentials, joining
and radio power. A child IOEthernetController with a plain IOEthernetInterface
handles IP input and output. It uses the existing Intel HAL and shared transmit
queue, with an ordinary Ethernet BSD subfamily and no Skywalk provider override.
The Wi-Fi interface remains registered for native connection controls.

EAPOL copies are delivered to the Wi-Fi control interface. IP/ARP packets are
delivered to Ethernet. Low-water wakeups reach both transmit schedulers; each
dequeues its own packets into the shared HAL queue. Ethernet carrier requires
association and, on an RSN network, authorization. Opening the Ethernet network
service does not change radio power or restart firmware. MAC changes mirror to
the Ethernet interface, and teardown detaches it before the HAL/workloop dies.

IORegistry reports `HybridControlBSDName`, `HybridDataBSDName` and
`HybridEthernetMode`. These names must be discovered, not assumed to be en0/en1.
The Ethernet data interface uses the `itl` BSD name prefix (for example `itl0`);
IONetworkStack assigns its unit. Native Wi-Fi keeps its existing `en` prefix.
The setup helper creates an Ethernet DHCP service and disables IPv4 and IPv6 on
the corresponding Wi-Fi control service. It preserves those service settings
for restoration. Native Wi-Fi may show no IP address while Ethernet has it.

Host routing/carrier tests and macOS builds do not prove that native WCL accepts
this split at runtime. AX211 physical speed, Wi-Fi menu operation, DHCP, IPv6,
reconnect and sleep/wake still require one real-machine test. This mode cannot
repair a radio/firmware/DMA bottleneck merely by using an Ethernet interface.
AirDrop, WPA3-only and Watch unlock remain outside the implemented scope.

The first physical test of 98babc6 loaded the hybrid driver, but produced no
throughput measurement. The data interface took the known `en0` assignment
before native Wi-Fi registered. InterfaceNamer then rejected native Wi-Fi's
fixed `en0` name with `interface name en0 is unavailable`; Wi-Fi disappeared.
The setup helper stopped before writing any IP preferences. Using a distinct
data prefix repairs that naming conflict in the driver design. Registration,
joining, DHCP and throughput after this repair still need physical verification.

The second physical test of 2d402e4 restored native Wi-Fi registration and
association. Ethernet still failed BSD attachment with
`ifnet_allocate_extended error 16`, so setup again stopped before writing IP
preferences or measuring throughput. Both interfaces inherited the physical
MAC as their ifnet unique ID from IOEthernetInterface. The data interface now
overrides initIfnetParams to select XNU's name+unit unique ID fallback, keeping
the actual link-layer MAC unchanged. Physical dual-interface attachment and
throughput after this additional repair remain unverified.

The prefix hook and separate unit namespaces are described by
[Apple's getNamePrefix API](https://developer.apple.com/documentation/kernel/ionetworkinterface/1810563-getnameprefix)
and implemented in
[IONetworkStack](https://github.com/apple-oss-distributions/IONetworkingFamily/blob/main/IONetworkStack.cpp).
