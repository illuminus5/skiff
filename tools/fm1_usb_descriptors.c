/* SPDX-License-Identifier: GPL-3.0-only */
/* Read cached FM-1 USB configuration descriptors on macOS.
 * Build: cc tools/fm1_usb_descriptors.c -framework IOKit -framework CoreFoundation -o /tmp/fm1-usb-descriptors
 * No USBDeviceOpen/Seize, control requests, resets or configuration changes.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>

int main(void)
{
    io_iterator_t it = IO_OBJECT_NULL;
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostDevice");
    int vid = 0x1209, pid = 1, found = 0, failed = 0;
    if (!match) return 1;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid);
    CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    CFDictionarySetValue(match, CFSTR("idVendor"), v);
    CFDictionarySetValue(match, CFSTR("idProduct"), p);
    CFRelease(v); CFRelease(p);
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &it);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "USB enumeration failed: 0x%08x\n", kr);
        return 1;
    }
    io_service_t svc;
    while ((svc = IOIteratorNext(it))) {
        IOCFPlugInInterface **plugin = NULL;
        IOUSBDeviceInterface **dev = NULL;
        SInt32 score = 0;
        ++found;
        kr = IOCreatePlugInInterfaceForService(svc, kIOUSBDeviceUserClientTypeID,
                                               kIOCFPlugInInterfaceID, &plugin, &score);
        IOObjectRelease(svc);
        if (kr != KERN_SUCCESS || !plugin) {
            fprintf(stderr, "USB plugin unavailable: 0x%08x\n", kr);
            failed = 1;
            continue;
        }
        HRESULT hr = (*plugin)->QueryInterface(plugin,
            CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&dev);
        IODestroyPlugInInterface(plugin);
        if (hr || !dev) {
            fprintf(stderr, "USB interface unavailable: 0x%08x\n", (unsigned)hr);
            failed = 1;
            continue;
        }
        UInt8 count = 0;
        kr = (*dev)->GetNumberOfConfigurations(dev, &count);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "Configuration count failed: 0x%08x\n", kr);
            failed = 1;
        }
        for (UInt8 c = 0; kr == KERN_SUCCESS && c < count; ++c) {
            IOUSBConfigurationDescriptorPtr desc = NULL;
            IOReturn rc = (*dev)->GetConfigurationDescriptorPtr(dev, c, &desc);
            if (rc != kIOReturnSuccess || !desc) {
                fprintf(stderr, "Configuration %u unavailable: 0x%08x\n", c, rc);
                failed = 1;
                continue;
            }
            const unsigned char *b = (const unsigned char *)desc;
            unsigned n = b[2] | ((unsigned)b[3] << 8);
            printf("FM-1 configuration index %u, %u bytes\n", c, n);
            if (n < 9 || b[0] != 9 || b[1] != 2) {
                fprintf(stderr, "Invalid configuration header\n");
                failed = 1;
                continue;
            }
            for (unsigned off = 0; off < n;) {
                unsigned len = b[off];
                if (len < 2 || len > n - off) {
                    fprintf(stderr, "Invalid descriptor at %u\n", off);
                    failed = 1;
                    break;
                }
                printf("%04u:", off);
                for (unsigned j = 0; j < len; ++j) printf(" %02x", b[off+j]);
                putchar('\n');
                off += len;
            }
        }
        (*dev)->Release(dev);
    }
    IOObjectRelease(it);
    if (!found) fprintf(stderr, "FM-1 VID 1209 PID 0001 not found\n");
    return failed || !found;
}
