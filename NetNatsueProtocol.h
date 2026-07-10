// NetNatsueProtocol.h
//
// NetBabel wire protocol: packet layouts, little-endian codecs
// and packet builders.  The protocol was reconstructed from the
// Natsue project's documentation and server implementation
// (https://github.com/20kdc/c3ds-projects); this file is the one
// place in NetNatsue that knows what the bytes mean.
//
// Every packet starts with a 32 byte header:
//   +0  int type     +4  int A (server UID)   +8  int B (server HID)
//   +12 int C        +16 int D                +20 int ticket
//   +24 int furtherDataLength                 +28 int E
// All values are little-endian.  Transactions echo a client
// allocated non-zero ticket number in the response.

#ifndef NET_NATSUE_PROTOCOL_H
#define NET_NATSUE_PROTOCOL_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>
#include <vector>

namespace NetNatsueProtocol
{
	// Packet types
	const int PACKET_MESSAGE = 0x09;			// CTOS and STOC, formats differ
	const int PACKET_HANDSHAKE_RESPONSE = 0x0A;	// STOC
	const int PACKET_USER_ONLINE = 0x0D;		// STOC
	const int PACKET_USER_OFFLINE = 0x0E;		// STOC
	const int PACKET_GET_CLIENT_INFO = 0x0F;	// CTOS, transactional
	const int PACKET_ADD_WWR_ENTRY = 0x10;		// CTOS
	const int PACKET_REMOVE_WWR_ENTRY = 0x11;	// CTOS
	const int PACKET_GET_CONNECTION_DETAIL = 0x13;	// CTOS, transactional
	const int PACKET_CLIENT_COMMAND = 0x14;		// CTOS and STOC
	const int PACKET_GET_STATUS = 0x18;			// CTOS, transactional
	const int PACKET_ONLINE_CHANGE = 0x1D;		// STOC
	const int PACKET_VIRTUAL_CONNECT = 0x1E;	// CTOS and STOC
	const int PACKET_VIRTUAL_CIRCUIT = 0x1F;	// CTOS and STOC
	const int PACKET_VIRTUAL_CIRCUIT_CLOSE = 0x20;	// STOC (also used as keepalive)
	const int PACKET_HANDSHAKE = 0x25;			// CTOS
	const int PACKET_MIGRATE = 0x2A;			// STOC, ignored
	const int PACKET_DS_FETCH_RANDOM_USER = 0x0221;	// CTOS, transactional
	const int PACKET_DS_FEED_HISTORY = 0x0321;	// CTOS, transactional

	const int PACKET_HEADER_SIZE = 32;

	// Client command subcommand acknowledging a virtual circuit
	const int CLIENT_COMMAND_VIRTUAL_CONNECT_ACCEPT = 0x0E;

	// Handshake constants.  The 1 and 2 are believed to be
	// CLIENTVERSION and PRODUCTCODE (2 = Docking Station).
	const int HANDSHAKE_CLIENT_VERSION = 1;
	const int HANDSHAKE_PRODUCT_CODE = 2;

	// A user identity: UID plus HID, written "uid+hid" in CAOS.
	struct BabelUIN
	{
		int uid;
		int hid;

		BabelUIN() : uid(0), hid(0) {}
		BabelUIN(int u, int h) : uid(u), hid(h) {}

		bool IsSet() const { return uid != 0 || hid != 0; }
		bool operator==(const BabelUIN& other) const
			{ return uid == other.uid && hid == other.hid; }

		std::string ToString() const;
		// Parses "uid+hid"; returns false and zeroes on bad input
		bool FromString(const std::string& str);
	};

	// The 32 byte packet header
	struct PacketHeader
	{
		int type;
		int fieldA;
		int fieldB;
		int fieldC;
		int fieldD;
		int ticket;
		int furtherData;
		int fieldE;

		PacketHeader()
			: type(0), fieldA(0), fieldB(0), fieldC(0), fieldD(0),
			  ticket(0), furtherData(0), fieldE(0) {}

		// data must point at PACKET_HEADER_SIZE bytes
		void Read(const char* data);
		void Write(std::vector<char>& out) const;
	};

	// Details of another user, as sent by the server
	struct ShortUserData
	{
		BabelUIN uin;
		std::string firstName;
		std::string lastName;
		std::string nickName;
	};

	// Little-endian primitives
	int GetInt(const char* data, int offset);
	void PutInt(std::vector<char>& out, int value);
	void PutIntAt(std::vector<char>& out, int offset, int value);

	// Packet builders.  Each returns the complete packet bytes
	std::vector<char> BuildHandshake(const BabelUIN& user, int ticket,
		const std::string& nickname, const std::string& password);
	std::vector<char> BuildWWRModify(bool add, const BabelUIN& server,
		const BabelUIN& target);
	std::vector<char> BuildGetClientInfo(const BabelUIN& server,
		const BabelUIN& target, int ticket);
	std::vector<char> BuildGetConnectionDetail(const BabelUIN& server,
		const BabelUIN& target, int ticket);
	std::vector<char> BuildGetStatus(const BabelUIN& server, int ticket);
	std::vector<char> BuildFetchRandomUser(const BabelUIN& server,
		const BabelUIN& user, int ticket);
	std::vector<char> BuildFeedHistory(const BabelUIN& server,
		const BabelUIN& user, int ticket, const char* blob, int blobSize);
	// Wraps a message payload (which starts with the 12 byte
	// BinaryMessage header) in a Packed Babel Message and a CTOS
	// message packet addressed to target.
	std::vector<char> BuildMessage(const BabelUIN& server, const BabelUIN& user,
		const BabelUIN& target, const char* payload, int payloadSize);
	// Acknowledges an incoming virtual circuit connect.  Natsue
	// uses these circuits as delivery confirmation pings.
	std::vector<char> BuildVirtualConnectAccept(const BabelUIN& server,
		const BabelUIN& initiator, int initiatorVSN, int localVSN);

	// Parses a Packed Babel Message (24 byte header + C2E message).
	// On success fills in the sender, the message type from the
	// C2E header, and the payload following it.  Returns false on
	// malformed data.
	bool ParsePackedBabelMessage(const char* data, int size,
		BabelUIN& sender, int& messageType, std::string& payload);

	// Parses a Packed Babel Short User Data blob
	bool ParseShortUserData(const char* data, int size, ShortUserData& out);
}

#endif // NET_NATSUE_PROTOCOL_H
