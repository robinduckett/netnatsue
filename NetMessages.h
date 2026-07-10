// NetMessages.h
//
// Message type header shared between the engine module and the
// network client library.  BinaryMessage is the 12 byte header
// which prefixes every message payload, and travels over the wire
// verbatim (little-endian); it is the "C2E Message" header of the
// NetBabel protocol.
//
// Recreated for NetNatsue.

#ifndef NET_MESSAGES_H
#define NET_MESSAGES_H

namespace NetMessages
{
	enum MessageType
	{
		MESG_PRAY_FILE = 0,	// payload is a PRAY file, spooled to the inbox
		MESG_WRIT = 1,		// payload is a packed CAOS message (NET: WRIT)
		TEXT_TEST = 2,		// plain text, only used by test harnesses
	};

	// This must be exactly 12 bytes; it is sent over the wire.
	struct BinaryMessage
	{
		int length;			// sizeof(BinaryMessage), for extensibility
		MessageType type;
		int reserved;		// always 0
	};
}

#endif // NET_MESSAGES_H
