// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void require(BOOL valid, NSString *message) {
    if (!valid) @throw [NSException exceptionWithName:@"HybridNetwork" reason:message userInfo:nil];
}
static NSString *bsdName(SCNetworkServiceRef service) {
    SCNetworkInterfaceRef interface = SCNetworkServiceGetInterface(service);
    return interface ? (__bridge NSString *)SCNetworkInterfaceGetBSDName(interface) : nil;
}
static NSDictionary *snapshot(SCNetworkServiceRef service) {
    NSMutableDictionary *record = [@{@"ID": (__bridge NSString *)SCNetworkServiceGetServiceID(service),
        @"BSDName": bsdName(service) ?: @"",
        @"Enabled": @(SCNetworkServiceGetEnabled(service))} mutableCopy];
    for (NSString *type in @[@"IPv4", @"IPv6"]) {
        SCNetworkProtocolRef protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
        if (protocol) {
            CFDictionaryRef config = SCNetworkProtocolGetConfiguration(protocol);
            record[type] = @{@"Present": @YES, @"Enabled": @(SCNetworkProtocolGetEnabled(protocol)),
                @"HadConfiguration": @(config != NULL),
                @"Config": config ? [(__bridge NSDictionary *)config copy] : @{}};
            CFRelease(protocol);
        } else record[type] = @{@"Present": @NO};
    }
    return record;
}
static void restoreService(SCNetworkServiceRef service, NSDictionary *record) {
    for (NSString *type in @[@"IPv4", @"IPv6"]) {
        NSDictionary *saved = record[type];
        SCNetworkProtocolRef protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
        if (![saved[@"Present"] boolValue]) {
            if (protocol) require(SCNetworkServiceRemoveProtocolType(service, (__bridge CFStringRef)type), @"Remove added IP protocol failed");
        } else {
            if (!protocol) {
                require(SCNetworkServiceAddProtocolType(service, (__bridge CFStringRef)type), @"Restore IP protocol failed");
                protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
            }
            require(protocol != NULL, @"IP protocol unavailable");
            CFDictionaryRef configuration = [saved[@"HadConfiguration"] boolValue] ? (__bridge CFDictionaryRef)saved[@"Config"] : NULL;
            require(SCNetworkProtocolSetConfiguration(protocol, configuration), @"Restore IP configuration failed");
            require(SCNetworkProtocolSetEnabled(protocol, [saved[@"Enabled"] boolValue]), @"Restore IP enabled state failed");
        }
        if (protocol) CFRelease(protocol);
    }
    require(SCNetworkServiceSetEnabled(service, [record[@"Enabled"] boolValue]), @"Restore service state failed");
}
static void configureData(SCNetworkServiceRef service) {
    for (NSString *type in @[@"IPv4", @"IPv6"]) {
        SCNetworkProtocolRef protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
        if (!protocol) {
            require(SCNetworkServiceAddProtocolType(service, (__bridge CFStringRef)type), @"Add data IP protocol failed");
            protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
        }
        require(protocol != NULL, @"Data IP protocol unavailable");
        NSDictionary *config = @{@"ConfigMethod": [type isEqualToString:@"IPv4"] ? @"DHCP" : @"Automatic"};
        require(SCNetworkProtocolSetConfiguration(protocol, (__bridge CFDictionaryRef)config), @"Set data IP method failed");
        require(SCNetworkProtocolSetEnabled(protocol, true), @"Enable data IP failed");
        CFRelease(protocol);
    }
    require(SCNetworkServiceSetEnabled(service, true), @"Enable data service failed");
}
static void configureControl(SCNetworkServiceRef service) {
    // airportd turns radio power off when configd brings the control ifnet
    // down. Keep a local-only IPv4 protocol active, with no DHCP/default route.
    SCNetworkProtocolRef ipv4 = SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeIPv4);
    if (!ipv4) {
        require(SCNetworkServiceAddProtocolType(service, kSCNetworkProtocolTypeIPv4), @"Add control IPv4 failed");
        ipv4 = SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeIPv4);
    }
    require(ipv4 != NULL, @"Control IPv4 unavailable");
    NSDictionary *config = @{@"ConfigMethod": @"LinkLocal"};
    require(SCNetworkProtocolSetConfiguration(ipv4, (__bridge CFDictionaryRef)config) &&
            SCNetworkProtocolSetEnabled(ipv4, true), @"Keep Wi-Fi control interface active failed");
    CFRelease(ipv4);
    for (NSString *type in @[@"IPv6"]) {
        SCNetworkProtocolRef protocol = SCNetworkServiceCopyProtocol(service, (__bridge CFStringRef)type);
        if (protocol) {
            require(SCNetworkProtocolSetEnabled(protocol, false), @"Disable control IP failed");
            CFRelease(protocol);
        }
    }
    require(SCNetworkServiceSetEnabled(service, true), @"Enable control service failed");
}
static void restoreBackup(SCPreferencesRef prefs, NSDictionary *backup) {
    require([backup[@"Services"] isKindOfClass:[NSArray class]] &&
            [backup[@"SetID"] isKindOfClass:[NSString class]] &&
            [backup[@"Order"] isKindOfClass:[NSArray class]], @"Invalid network backup structure");
    SCNetworkSetRef set = SCNetworkSetCopy(prefs, (__bridge CFStringRef)backup[@"SetID"]);
    require(set != NULL, @"Saved network location is missing");
    // Validate every saved identity before staging restoration of any service.
    for (NSDictionary *record in backup[@"Services"]) {
        require([record[@"ID"] isKindOfClass:[NSString class]] &&
                [record[@"BSDName"] isKindOfClass:[NSString class]], @"Invalid saved service identity");
        SCNetworkServiceRef service = SCNetworkServiceCopy(prefs, (__bridge CFStringRef)record[@"ID"]);
        require(service != NULL, @"Saved network service is missing; refusing partial restore");
        BOOL matches = [bsdName(service) isEqualToString:record[@"BSDName"]];
        CFRelease(service);
        require(matches, @"Saved service interface identity changed; refusing restore");
    }
    NSString *created = backup[@"CreatedDataService"];
    if (created.length) {
        SCNetworkServiceRef service = SCNetworkServiceCopy(prefs, (__bridge CFStringRef)created);
        if (service) {
            BOOL matches = [bsdName(service) isEqualToString:backup[@"DataBSDName"]];
            CFRelease(service);
            require(matches, @"Created service identity changed");
        }
    }
    for (NSDictionary *record in backup[@"Services"]) {
        SCNetworkServiceRef service = SCNetworkServiceCopy(prefs, (__bridge CFStringRef)record[@"ID"]);
        require(service != NULL, @"Saved network service is missing; refusing partial restore");
        restoreService(service, record); CFRelease(service);
    }
    if (created.length) {
        SCNetworkServiceRef service = SCNetworkServiceCopy(prefs, (__bridge CFStringRef)created);
        if (service) {
            require([bsdName(service) isEqualToString:backup[@"DataBSDName"]], @"Created service identity changed");
            CFArrayRef services = SCNetworkSetCopyServices(set);
            for (id value in (__bridge NSArray *)services) {
                SCNetworkServiceRef member = (__bridge SCNetworkServiceRef)value;
                if (CFEqual(SCNetworkServiceGetServiceID(member), (__bridge CFStringRef)created)) {
                    require(SCNetworkSetRemoveService(set, service), @"Remove created service from location failed");
                    break;
                }
            }
            if (services) CFRelease(services);
            require(SCNetworkServiceRemove(service), @"Remove created data service failed"); CFRelease(service);
        }
    }
    // Apple's API removes the order key for an empty array. It returns NoKey
    // if that key is already absent, which is already the desired state.
    if (SCNetworkSetGetServiceOrder(set) || [backup[@"Order"] count] > 0)
        require(SCNetworkSetSetServiceOrder(set, (__bridge CFArrayRef)backup[@"Order"]), @"Restore service order failed");
    CFRelease(set);
}
static SCNetworkInterfaceRef findInterface(NSString *name) {
    CFArrayRef interfaces = SCNetworkInterfaceCopyAll();
    SCNetworkInterfaceRef result = NULL;
    for (id value in (__bridge NSArray *)interfaces) {
        SCNetworkInterfaceRef interface = (__bridge SCNetworkInterfaceRef)value;
        if ([(__bridge NSString *)SCNetworkInterfaceGetBSDName(interface) isEqualToString:name]) {
            result = (SCNetworkInterfaceRef)CFRetain(interface); break;
        }
    }
    if (interfaces) CFRelease(interfaces);
    return result;
}
static int selfTest(void) {
    // Isolated in-memory preference ID: never commit/apply or touch host networking.
    SCPreferencesRef prefs = SCPreferencesCreate(NULL, CFSTR("Hybrid fixture"), CFSTR("AirportHybrid-Uncommitted-Test.plist"));
    require(prefs != NULL, @"Fixture preferences failed");
    CFArrayRef interfaces = SCNetworkInterfaceCopyAll();
    SCNetworkInterfaceRef ethernet = NULL;
    for (id value in (__bridge NSArray *)interfaces) {
        SCNetworkInterfaceRef interface = (__bridge SCNetworkInterfaceRef)value;
        if (CFEqual(SCNetworkInterfaceGetInterfaceType(interface), kSCNetworkInterfaceTypeEthernet)) { ethernet = interface; break; }
    }
    require(ethernet != NULL, @"No Ethernet interface available for uncommitted fixture");
    SCNetworkServiceRef service = SCNetworkServiceCreate(prefs, ethernet);
    require(service != NULL, @"Fixture service failed");
    require(SCNetworkServiceEstablishDefaultConfiguration(service), @"Fixture defaults failed");
    SCNetworkSetRef set = SCNetworkSetCreate(prefs);
    require(set && SCNetworkSetSetName(set, CFSTR("Uncommitted hybrid fixture")), @"Fixture location failed");
    require(SCNetworkSetAddService(set, service), @"Fixture service membership failed");
    NSString *identifier = (__bridge NSString *)SCNetworkServiceGetServiceID(service);
    require(SCNetworkSetSetServiceOrder(set, (__bridge CFArrayRef)@[identifier]), @"Fixture service order failed");
    NSDictionary *before = snapshot(service);
    NSDictionary *backup = @{@"Services": @[before], @"CreatedDataService": @"",
        @"SetID": (__bridge NSString *)SCNetworkSetGetSetID(set), @"Order": @[identifier]};
    configureControl(service);
    NSDictionary *disabled = snapshot(service);
    require([disabled[@"IPv4"][@"Enabled"] boolValue] && ![disabled[@"IPv6"][@"Enabled"] boolValue] &&
            [disabled[@"IPv4"][@"Config"][@"ConfigMethod"] isEqual:@"LinkLocal"], @"Local-only control fixture failed");
    SCNetworkProtocolRef oldIPv4 = SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeIPv4);
    require(oldIPv4 && SCNetworkProtocolSetEnabled(oldIPv4, false), @"Old disabled-control fixture failed");
    CFRelease(oldIPv4);
    configureControl(service);
    require([snapshot(service)[@"IPv4"][@"Enabled"] boolValue], @"Repair disabled-control fixture failed");
    configureData(service);
    NSDictionary *configured = snapshot(service);
    require([configured[@"IPv4"][@"Config"][@"ConfigMethod"] isEqual:@"DHCP"], @"DHCP fixture failed");
    restoreBackup(prefs, backup);
    require([snapshot(service) isEqual:before], @"Configuration roundtrip failed");
    require([(__bridge NSArray *)SCNetworkSetGetServiceOrder(set) isEqual:@[identifier]], @"Service order roundtrip failed");
    require(SCNetworkServiceRemoveProtocolType(service, kSCNetworkProtocolTypeIPv6), @"Absent-protocol fixture failed");
    NSDictionary *absent = snapshot(service);
    configureData(service);
    restoreService(service, absent);
    require([snapshot(service) isEqual:absent], @"Absent IP protocol roundtrip failed");
    // Exercise restoration when the helper created a data service. This fixture
    // uses the now-empty named location and is never committed/applied.
    require(SCNetworkSetRemoveService(set, service) && SCNetworkServiceRemove(service), @"Fixture service cleanup failed");
    CFRelease(service);
    service = SCNetworkServiceCreate(prefs, ethernet);
    require(service != NULL, @"Created-service allocation fixture failed");
    require(SCNetworkServiceEstablishDefaultConfiguration(service), @"Created-service defaults fixture failed");
    require(SCNetworkSetAddService(set, service), @"Created-service membership fixture failed");
    identifier = (__bridge NSString *)SCNetworkServiceGetServiceID(service);
    backup = @{@"Services": @[], @"CreatedDataService": identifier,
        @"DataBSDName": bsdName(service), @"SetID": (__bridge NSString *)SCNetworkSetGetSetID(set), @"Order": @[]};
    restoreBackup(prefs, backup);
    SCNetworkServiceRef removed = SCNetworkServiceCopy(prefs, (__bridge CFStringRef)identifier);
    require(removed == NULL, @"Created service was not removed");
    CFArrayRef remaining = SCNetworkSetCopyServices(set);
    require(remaining && CFArrayGetCount(remaining) == 0, @"Created-service location membership was not removed");
    CFRelease(remaining);
    CFRelease(service); CFRelease(set); CFRelease(interfaces); CFRelease(prefs);
    puts("network configuration roundtrip passed; no preferences committed/applied"); return 0;
}
int main(int argc, const char **argv) { @autoreleasepool {
    SCPreferencesRef prefs = NULL; BOOL locked = NO;
    @try {
        if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return selfTest();
        require(argc == 3 && (strcmp(argv[1], "--apply") == 0 || strcmp(argv[1], "--repair") == 0 || strcmp(argv[1], "--restore") == 0),
            @"Usage: sudo hybrid-network --apply|--repair|--restore /path/to/backup.plist");
        require(geteuid() == 0, @"Run with sudo to configure network services");
        NSString *path = [NSString stringWithUTF8String:argv[2]];
        prefs = SCPreferencesCreate(NULL, CFSTR("AirportItlwm hybrid"), NULL);
        require(prefs != NULL, @"Network preferences unavailable");
        require(SCPreferencesLock(prefs, true), @"Network preferences lock failed"); locked = YES;
        if (strcmp(argv[1], "--restore") == 0) {
            NSDictionary *backup = [NSDictionary dictionaryWithContentsOfFile:path];
            require([backup[@"Format"] isEqual:@1], @"Backup missing or invalid");
            restoreBackup(prefs, backup);
            require(SCPreferencesCommitChanges(prefs) && SCPreferencesApplyChanges(prefs), @"Restoring preferences failed");
            puts("Previous IP protocol settings and service order restored. Backup retained.");
        } else {
            BOOL repairing = strcmp(argv[1], "--repair") == 0;
            NSDictionary *originalBackup = repairing ? [NSDictionary dictionaryWithContentsOfFile:path] : nil;
            if (repairing) require([originalBackup[@"Format"] isEqual:@1], @"Original backup missing or invalid; cannot repair");
            else require(![[NSFileManager defaultManager] fileExistsAtPath:path], @"Backup already exists; use --repair or restore it before another apply");
            io_service_t driver = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
            require(driver != IO_OBJECT_NULL, @"AirportItlwm driver not found");
            CFTypeRef mode = IORegistryEntryCreateCFProperty(driver, CFSTR("HybridEthernetMode"), NULL, 0);
            CFTypeRef control = IORegistryEntryCreateCFProperty(driver, CFSTR("HybridControlBSDName"), NULL, 0);
            CFTypeRef data = IORegistryEntryCreateCFProperty(driver, CFSTR("HybridDataBSDName"), NULL, 0);
            require(mode && CFEqual(mode, kCFBooleanTrue) && control && data &&
                    CFGetTypeID(control) == CFStringGetTypeID() && CFGetTypeID(data) == CFStringGetTypeID(), @"Hybrid mode/interfaces not ready");
            NSString *controlName = (__bridge NSString *)control, *dataName = (__bridge NSString *)data;
            require(![controlName isEqual:dataName], @"Control and data interfaces must differ");
            SCNetworkInterfaceRef interface = findInterface(dataName);
            require(interface && CFEqual(SCNetworkInterfaceGetInterfaceType(interface), kSCNetworkInterfaceTypeEthernet), @"Data interface is not ordinary Ethernet");
            SCNetworkSetRef set = SCNetworkSetCopyCurrent(prefs);
            require(set != NULL, @"Current network location unavailable");
            if (repairing) {
                require([originalBackup[@"ControlBSDName"] isEqual:controlName] && [originalBackup[@"DataBSDName"] isEqual:dataName] &&
                        [originalBackup[@"SetID"] isEqual:(__bridge NSString *)SCNetworkSetGetSetID(set)], @"Backup interface/location identity changed");
            }
            NSArray *order = [(__bridge NSArray *)SCNetworkSetGetServiceOrder(set) copy];
            CFArrayRef services = SCNetworkSetCopyServices(set);
            NSMutableArray *saved = [NSMutableArray array];
            SCNetworkServiceRef dataService = NULL;
            unsigned controls = 0;
            for (id value in (__bridge NSArray *)services) {
                SCNetworkServiceRef service = (__bridge SCNetworkServiceRef)value;
                NSString *name = bsdName(service);
                if ([name isEqual:controlName] || [name isEqual:dataName]) [saved addObject:snapshot(service)];
                if ([name isEqual:controlName]) { configureControl(service); ++controls; }
                if ([name isEqual:dataName] && !dataService) dataService = (SCNetworkServiceRef)CFRetain(service);
            }
            require(controls > 0, @"Wi-Fi control service not found; no settings committed");
            NSString *created = @"";
            if (!dataService) {
                require(!repairing, @"Original data service missing; restore the backup before another setup");
                dataService = SCNetworkServiceCreate(prefs, interface);
                require(dataService && SCNetworkServiceEstablishDefaultConfiguration(dataService) &&
                        SCNetworkSetAddService(set, dataService), @"Create Ethernet data service failed");
                created = (__bridge NSString *)SCNetworkServiceGetServiceID(dataService);
                SCNetworkServiceSetName(dataService, CFSTR("Intel Wi-Fi Ethernet"));
            }
            NSDictionary *backup = originalBackup ?: @{@"Format": @1, @"Services": saved, @"CreatedDataService": created,
                @"ControlBSDName": controlName, @"DataBSDName": dataName,
                @"SetID": (__bridge NSString *)SCNetworkSetGetSetID(set), @"Order": order ?: @[]};
            if (!repairing) require([backup writeToFile:path atomically:YES], @"Cannot save rollback backup; no settings committed");
            configureData(dataService);
            NSString *identifier = (__bridge NSString *)SCNetworkServiceGetServiceID(dataService);
            NSMutableArray *newOrder = [(order ? order : @[]) mutableCopy];
            [newOrder removeObject:identifier]; [newOrder insertObject:identifier atIndex:0];
            require(SCNetworkSetSetServiceOrder(set, (__bridge CFArrayRef)newOrder), @"Set Ethernet service priority failed");
            require(SCPreferencesCommitChanges(prefs), @"Commit failed; backup retained");
            if (!SCPreferencesApplyChanges(prefs)) {
                restoreBackup(prefs, backup);
                BOOL committed = SCPreferencesCommitChanges(prefs);
                BOOL applied = committed && SCPreferencesApplyChanges(prefs);
                require(applied, [NSString stringWithFormat:@"Apply and automatic restore failed. Backup retained at %@; run --restore with that file.", path]);
                require(NO, @"Apply failed; previous settings restored");
            }
            printf("Native Wi-Fi control: %s; Ethernet IP traffic: %s\n", controlName.UTF8String, dataName.UTF8String);
            CFRelease(dataService); CFRelease(services); CFRelease(set); CFRelease(interface);
            CFRelease(mode); CFRelease(control); CFRelease(data); IOObjectRelease(driver);
        }
        SCPreferencesUnlock(prefs); CFRelease(prefs); return 0;
    } @catch (NSException *exception) {
        fprintf(stderr, "%s\n", exception.reason.UTF8String);
        if (locked) SCPreferencesUnlock(prefs);
        if (prefs) CFRelease(prefs);
        return 1;
    }
} }
