//
//  AirportItlwmEthernetInterface.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmEthernetInterface.hpp"

#include <sys/_if_ether.h>
#include <net80211/ieee80211_var.h>
#ifdef AIRPORT_WCL
#include <IOKit/IOCommandGate.h>
#include "AirportItlwmV2.hpp"
extern IOCommandGate *_fCommandGate;
#endif

#define super IOEthernetInterface
OSDefineMetaClassAndStructors(AirportItlwmEthernetInterface, IOEthernetInterface);

bool AirportItlwmEthernetInterface::
initWithSkywalkInterfaceAndProvider(IONetworkController *controller, IO80211SkywalkInterface *interface)
{
    bool ret = super::init(controller);
    if (ret)
        this->interface = interface;
    this->isAttach = false;
    return ret;
}

#ifdef AIRPORT_WCL
IOReturn AirportItlwmEthernetInterface::prepareBSDGated(OSObject *, void *arg0, void *, void *, void *)
{
    auto self = static_cast<AirportItlwmEthernetInterface *>(arg0);
    auto controller = OSDynamicCast(IO80211Controller, self->getController());
    auto queue = controller ? controller->getWorkQueue() : nullptr;
    if (!queue || !queue->inGate() || queue->onThread()) {
        return kIOReturnNotPermitted;
    }
    return self->interface->prepareBSDInterface(self->getIfnet(), 0);
}
#endif

IOReturn AirportItlwmEthernetInterface::
attachToDataLinkLayer( IOOptionBits options, void *parameter )
{
    XYLog("%s\n", __FUNCTION__);
    char infName[IFNAMSIZ];
#ifdef AIRPORT_WCL
    if (!setInterfaceSubType(3)) {
        return kIOReturnNotReady;
    }
#endif
    IOReturn ret = super::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess && interface) {
        UInt8 builtIn = 0;
        IOEthernetAddress addr;
        interface->setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
        snprintf(infName, sizeof(infName), "%s%u", ifnet_name(getIfnet()), ifnet_unit(getIfnet()));
#ifdef AIRPORT_WCL
        auto airport = OSDynamicCast(AirportItlwm, getController());
        if (airport && airport->ethernetMode)
            airport->setProperty("HybridControlBSDName", infName);
#endif
        interface->setProperty("IOInterfaceName", OSString::withCString(infName));
        interface->setProperty(kIOInterfaceUnit, OSNumber::withNumber(ifnet_unit(getIfnet()), 8));
        interface->setProperty(kIOInterfaceNamePrefix, OSString::withCString(ifnet_name(getIfnet())));
        if (OSDynamicCast(IOEthernetController, getController())->getHardwareAddress(&addr) == kIOReturnSuccess)
            setProperty(kIOMACAddress,  (void *) &addr,
                        kIOEthernetAddressSize);
#ifdef AIRPORT_WCL
        ret = _fCommandGate ? _fCommandGate->runAction(prepareBSDGated, this) : kIOReturnNotReady;
        if (ret != kIOReturnSuccess) {
            super::detachFromDataLinkLayer(options, parameter);
            isAttach = false;
            return ret;
        }
        isAttach = true;
        interface->registerService();
#else
        interface->registerService();
        interface->prepareBSDInterface(getIfnet(), 0);
#endif
//        ret = bpf_attach(getIfnet(), DLT_RAW, 0x48, &AirportItlwmEthernetInterface::bpfOutputPacket, &AirportItlwmEthernetInterface::bpfTap);
    }
#ifdef AIRPORT_WCL
    isAttach = ret == kIOReturnSuccess;
#else
    isAttach = true;
#endif
    return ret;
}

void AirportItlwmEthernetInterface::
detachFromDataLinkLayer(IOOptionBits options, void *parameter)
{
    super::detachFromDataLinkLayer(options, parameter);
    isAttach = false;
}

/**
 Add another hack to fake that the provider is IOSkywalkNetworkInterface, to avoid skywalkfamily instance cast panic.
 */
IOService *AirportItlwmEthernetInterface::
getProvider() const
{
    return isAttach ? this->interface : super::getProvider();
}

errno_t AirportItlwmEthernetInterface::
bpfOutputPacket(ifnet_t interface, u_int32_t data_link_type, mbuf_t packet)
{
    XYLog("%s data_link_type: %d\n", __FUNCTION__, data_link_type);
    AirportItlwmEthernetInterface *networkInterface = (AirportItlwmEthernetInterface *)ifnet_softc(interface);
    return networkInterface->enqueueOutputPacket(packet);
}

errno_t AirportItlwmEthernetInterface::
bpfTap(ifnet_t interface, u_int32_t data_link_type, bpf_tap_mode direction)
{
    XYLog("%s data_link_type: %d direction: %d\n", __FUNCTION__, data_link_type, direction);
    return 0;
}

bool AirportItlwmEthernetInterface::
setLinkState(IO80211LinkState state)
{
    if (state == kIO80211NetworkLinkUp) {
        ifnet_set_flags(getIfnet(), ifnet_flags(getIfnet()) | (IFF_UP | IFF_RUNNING), (IFF_UP | IFF_RUNNING));
    } else {
        ifnet_set_flags(getIfnet(), ifnet_flags(getIfnet()) & ~(IFF_UP | IFF_RUNNING), 0);
    }
    return true;
}

extern const char* hexdump(uint8_t *buf, size_t len);

#ifdef AIRPORT_WCL
static mbuf_t normalizeInputPacket(mbuf_t packet, UInt32 length)
{
    const size_t packetLength = length ? length : mbuf_pkthdr_len(packet);
    if (!(mbuf_flags(packet) & MBUF_PKTHDR) || packetLength < ETHER_HDR_LEN) {
        mbuf_freem(packet);
        return NULL;
    }
    if (!mbuf_next(packet) && mbuf_len(packet) >= packetLength)
        return packet;

    unsigned int maxChunks = 1;
    mbuf_t linear = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, packetLength + ETHER_ALIGN, &maxChunks, &linear)) {
        mbuf_freem(packet);
        return NULL;
    }
    UInt8 *data = static_cast<UInt8 *>(mbuf_data(linear)) + ETHER_ALIGN;
    if (mbuf_copydata(packet, 0, packetLength, data) ||
        mbuf_copy_pkthdr(linear, packet) ||
        mbuf_setdata(linear, data, packetLength)) {
        mbuf_freem(linear);
        mbuf_freem(packet);
        return NULL;
    }
    mbuf_pkthdr_setlen(linear, packetLength);
    mbuf_pkthdr_setheader(linear, NULL);
    mbuf_freem(packet);
    return linear;
}
#endif

UInt32 AirportItlwmEthernetInterface::
inputPacket(mbuf_t packet, UInt32 length, IOOptionBits options, void *param)
{
#ifdef AIRPORT_WCL
    if (!packet)
        return 0;
    if (!getIfnet()) {
        mbuf_freem(packet);
        return 0;
    }
    const bool directInput = param == packet;
    packet = normalizeInputPacket(packet, length);
    if (!packet) {
        IONetworkData *data = getParameter(kIONetworkStatsKey);
        IONetworkStats *stats = data ? (IONetworkStats *)data->getBuffer() : NULL;
        if (stats)
            stats->inputErrors++;
        return 0;
    }
    if (directInput)
        param = packet;
#endif
#ifndef AIRPORT_WCL
    ether_header_t *eh;
    size_t len = mbuf_len(packet);
    
    eh = (ether_header_t *)mbuf_data(packet);

    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(packet), len);
        XYLog("input EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
#endif
    return IOEthernetInterface::inputPacket(packet, length, options, param);
}

#ifdef AIRPORT_WCL
OSDefineMetaClassAndStructors(AirportItlwmDataController, IOEthernetController);
OSDefineMetaClassAndStructors(AirportItlwmDataInterface, IOEthernetInterface);

bool AirportItlwmDataController::initWithOwner(AirportItlwm *airport) {
    owner = airport;
    return owner && IOEthernetController::init(nullptr);
}
bool AirportItlwmDataController::createWorkLoop() { return owner && owner->getWorkLoop(); }
IOWorkLoop *AirportItlwmDataController::getWorkLoop() const { return owner ? owner->getWorkLoop() : nullptr; }
IONetworkInterface *AirportItlwmDataController::createInterface() {
    auto interface = new AirportItlwmDataInterface;
    if (interface && !interface->init(this)) { interface->release(); interface = nullptr; }
    return interface;
}
bool AirportItlwmDataController::start(IOService *provider) {
    if (!IOEthernetController::start(provider)) return false;
    auto media = OSDictionary::withCapacity(1);
    auto medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 0);
    if (!media || !medium) {
        OSSafeReleaseNULL(media); OSSafeReleaseNULL(medium);
        IOEthernetController::stop(provider); return false;
    }
    IONetworkMedium::addMedium(media, medium);
    bool published = publishMediumDictionary(media);
    if (published) setSelectedMedium(medium);
    medium->release(); media->release();
    if (!published || !attachInterface(&dataInterface, false)) {
        if (dataInterface) {
            detachInterface(dataInterface, true);
            OSSafeReleaseNULL(dataInterface);
        }
        IOEthernetController::stop(provider); return false;
    }
    setProperty("HybridEthernetDataController", true);
    dataInterface->setProperty("HybridEthernetData", true);
    setLinkStatus(kIONetworkLinkValid);
    dataInterface->registerService(); registerService();
    return true;
}
void AirportItlwmDataController::stop(IOService *provider) {
    dataEnabled = false;
    if (dataInterface) {
#ifdef __PRIVATE_SPI__
        dataInterface->stopOutputThread(); dataInterface->flushOutputQueue();
#endif
        detachInterface(dataInterface, true);
        OSSafeReleaseNULL(dataInterface);
    }
    IOEthernetController::stop(provider);
}
bool AirportItlwmDataController::configureInterface(IONetworkInterface *interface) {
    return IOEthernetController::configureInterface(interface) && owner->bindEthernetDataInterface(interface);
}
IOReturn AirportItlwmDataController::enable(IONetworkInterface *) {
    // Native WCL owns radio power and association. An Ethernet service must
    // not reset firmware or change the network when DHCP opens the interface.
    dataEnabled = true;
#ifdef __PRIVATE_SPI__
    if (linkUp && dataInterface) dataInterface->startOutputThread();
#endif
    return kIOReturnSuccess;
}
IOReturn AirportItlwmDataController::disable(IONetworkInterface *interface) {
    dataEnabled = false;
#ifdef __PRIVATE_SPI__
    interface->stopOutputThread(); interface->flushOutputQueue();
#endif
    return kIOReturnSuccess;
}
UInt32 AirportItlwmDataController::outputPacket(mbuf_t packet, void *parameter) {
    return owner->outputPacket(packet, parameter);
}
#ifdef __PRIVATE_SPI__
IOReturn AirportItlwmDataController::outputStart(IONetworkInterface *interface, IOOptionBits options) {
    return owner->outputStart(interface, options);
}
#endif
IOReturn AirportItlwmDataController::getHardwareAddress(IOEthernetAddress *address) { return owner->getHardwareAddress(address); }
IOReturn AirportItlwmDataController::setMulticastMode(IOEnetMulticastMode mode) { return owner->setMulticastMode(mode); }
IOReturn AirportItlwmDataController::setPromiscuousMode(IOEnetPromiscuousMode mode) { return owner->setPromiscuousMode(mode); }
IOReturn AirportItlwmDataController::setMulticastList(IOEthernetAddress *addresses, UInt32 count) { return owner->setMulticastList(addresses, count); }
IOReturn AirportItlwmDataController::getPacketFilters(const OSSymbol *group, UInt32 *filters) const { return owner->getPacketFilters(group, filters); }
UInt32 AirportItlwmDataController::getFeatures() const { return owner->getFeatures(); }
const OSString *AirportItlwmDataController::newVendorString() const { return OSString::withCString("Intel"); }
const OSString *AirportItlwmDataController::newModelString() const { return OSString::withCString("Intel Wi-Fi Ethernet"); }
void AirportItlwmDataController::updateCarrier(bool active) {
    if (!dataInterface || active == linkUp) return;
    linkUp = active;
    setLinkStatus(kIONetworkLinkValid | (active ? kIONetworkLinkActive : 0));
#ifdef __PRIVATE_SPI__
    if (active && dataEnabled) dataInterface->startOutputThread();
    else { dataInterface->stopOutputThread(); dataInterface->flushOutputQueue(); }
#endif
}
IOReturn AirportItlwmDataController::updateAddress(const UInt8 *address, UInt32 length) {
    if (!dataInterface || !dataInterface->getIfnet()) return kIOReturnSuccess;
    if (ifnet_set_lladdr(dataInterface->getIfnet(), address, length)) return kIOReturnError;
    dataInterface->setProperty(kIOMACAddress, const_cast<UInt8 *>(address), length);
    return kIOReturnSuccess;
}
IOReturn AirportItlwmDataInterface::attachToDataLinkLayer(IOOptionBits options, void *parameter) {
    IOReturn status = IOEthernetInterface::attachToDataLinkLayer(options, parameter);
    auto controller = OSDynamicCast(AirportItlwmDataController, getController());
    if (status == kIOReturnSuccess && controller && controller->owner) {
        IOEthernetAddress address;
        if (controller->owner->getHardwareAddress(&address) == kIOReturnSuccess)
            controller->updateAddress(address.bytes, sizeof(address.bytes));
        char name[IFNAMSIZ];
        snprintf(name, sizeof(name), "%s%u", ifnet_name(getIfnet()), ifnet_unit(getIfnet()));
        controller->owner->setProperty("HybridDataBSDName", name);
    }
    return status;
}
#endif
