//
//  AirportItlwmEthernetInterface.hpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef AirportItlwmEthernetInterface_hpp
#define AirportItlwmEthernetInterface_hpp

extern "C" {
#include <net/bpf.h>
}
#include "Airport/Apple80211.h"
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <sys/kernel_types.h>
#include <IOKit/network/IOEthernetInterface.h>

#ifdef AIRPORT_WCL
class AirportItlwm;
class AirportItlwmDataController : public IOEthernetController {
    OSDeclareDefaultStructors(AirportItlwmDataController)
public:
    bool initWithOwner(AirportItlwm *);
    bool start(IOService *) override;
    void stop(IOService *) override;
    bool createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    IONetworkInterface *createInterface() override;
    bool configureInterface(IONetworkInterface *) override;
    IOReturn enable(IONetworkInterface *) override;
    IOReturn disable(IONetworkInterface *) override;
    UInt32 outputPacket(mbuf_t, void *) override;
#ifdef __PRIVATE_SPI__
    IOReturn outputStart(IONetworkInterface *, IOOptionBits) override;
#endif
    IOReturn getHardwareAddress(IOEthernetAddress *) override;
    IOReturn setMulticastMode(IOEnetMulticastMode) override;
    IOReturn setPromiscuousMode(IOEnetPromiscuousMode) override;
    IOReturn setMulticastList(IOEthernetAddress *, UInt32) override;
    IOReturn getPacketFilters(const OSSymbol *, UInt32 *) const override;
    UInt32 getFeatures() const override;
    const OSString *newVendorString() const override;
    const OSString *newModelString() const override;
    void updateCarrier(bool);
    IOReturn updateAddress(const UInt8 *, UInt32);
    AirportItlwm *owner = nullptr;
    IONetworkInterface *dataInterface = nullptr;
    bool linkUp = false;
};
class AirportItlwmDataInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(AirportItlwmDataInterface)
public:
    IOReturn attachToDataLinkLayer(IOOptionBits, void *) override;
};
#endif

class AirportItlwmEthernetInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(AirportItlwmEthernetInterface)
    
public:
    virtual IOReturn attachToDataLinkLayer( IOOptionBits options,
                                            void *       parameter ) override;
    
    virtual void     detachFromDataLinkLayer( IOOptionBits options,
                                              void *       parameter ) override;
    
    virtual bool initWithSkywalkInterfaceAndProvider(IONetworkController *controller, IO80211SkywalkInterface *interface);
    
    virtual bool setLinkState(IO80211LinkState state);
    
    static errno_t bpfOutputPacket(ifnet_t interface, u_int32_t data_link_type,
                                  mbuf_t packet);
    
    static errno_t bpfTap(ifnet_t interface, u_int32_t data_link_type,
                          bpf_tap_mode direction);
    
    virtual UInt32   inputPacket(
                                 mbuf_t          packet,
                                 UInt32          length  = 0,
                                 IOOptionBits    options = 0,
                                 void *          param   = 0 ) override;
    
    virtual IOService * getProvider( void ) const override;
    
private:
#ifdef AIRPORT_WCL
    static IOReturn prepareBSDGated(OSObject *, void *, void *, void *, void *);
#endif
    IO80211SkywalkInterface *interface;
    bool isAttach;
};

#endif /* AirportItlwmEthernetInterface_hpp */
