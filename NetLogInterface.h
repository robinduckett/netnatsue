// NetLogInterface.h
//
// Abstract logging interface for the network client library.
// The engine module implements this (see NetLogImplementation in
// modules/netbabel) to route library logs into the flight recorder.
//
// Recreated for NetNatsue, a replacement for the lost Babel
// client library, speaking the NetBabel protocol as documented
// by the Natsue project.

#ifndef NET_LOG_INTERFACE_H
#define NET_LOG_INTERFACE_H

class NetLogInterface
{
public:
	virtual ~NetLogInterface() {}
	virtual void Log(const char* text) = 0;
};

#endif // NET_LOG_INTERFACE_H
