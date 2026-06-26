// Client.h - minimal Arduino Client class for the eModbus component.
// eModbus's TCP transports include <Client.h>. The RTU build does not
// link any TCP transport, so the class is here purely to satisfy the
// include; the link will fail loudly if a TCP TU ever references it.
#ifndef EMODBUS_CLIENT_H
#define EMODBUS_CLIENT_H
#include "Stream.h"
class Client : public Stream {
public:
    virtual int connect(const char * /*host*/, uint16_t /*port*/) { return 0; }
    virtual int connect(IPAddress /*ip*/, uint16_t /*port*/) { return 0; }
    virtual void stop() {}
    virtual uint8_t connected() { return 0; }
    virtual operator bool() { return false; }
};
// IPAddress is only used by TCP; not needed for RTU. Forward-declare a
// placeholder so the include compiles.
struct IPAddress { uint8_t b[4]; };
#endif
