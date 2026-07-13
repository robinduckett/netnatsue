#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "NetNatsueProtocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace NetNatsueProtocol
{

// ---------------------------------------------------------------------
// Little-endian primitives
// ---------------------------------------------------------------------

int GetInt(const char* data, int offset)
{
	const unsigned char* bytes = (const unsigned char*)(data + offset);
	return (int)((unsigned int)bytes[0]
		| ((unsigned int)bytes[1] << 8)
		| ((unsigned int)bytes[2] << 16)
		| ((unsigned int)bytes[3] << 24));
}

void PutInt(std::vector<char>& out, int value)
{
	unsigned int v = (unsigned int)value;
	out.push_back((char)(v & 0xFF));
	out.push_back((char)((v >> 8) & 0xFF));
	out.push_back((char)((v >> 16) & 0xFF));
	out.push_back((char)((v >> 24) & 0xFF));
}

void PutIntAt(std::vector<char>& out, int offset, int value)
{
	unsigned int v = (unsigned int)value;
	out[offset] = (char)(v & 0xFF);
	out[offset + 1] = (char)((v >> 8) & 0xFF);
	out[offset + 2] = (char)((v >> 16) & 0xFF);
	out[offset + 3] = (char)((v >> 24) & 0xFF);
}

// ---------------------------------------------------------------------
// BabelUIN
// ---------------------------------------------------------------------

std::string BabelUIN::ToString() const
{
	char buffer[32];
	sprintf(buffer, "%d+%d", uid, hid);
	return std::string(buffer);
}

bool BabelUIN::FromString(const std::string& str)
{
	uid = 0;
	hid = 0;

	std::string::size_type plus = str.find('+');
	if (plus == std::string::npos || plus == 0 || plus + 1 >= str.size())
		return false;

	// Both halves must be plain non-negative decimal numbers
	std::string::size_type i;
	for (i = 0; i < str.size(); ++i)
	{
		if (i == plus)
			continue;
		if (str[i] < '0' || str[i] > '9')
			return false;
	}

	uid = atoi(str.substr(0, plus).c_str());
	hid = atoi(str.substr(plus + 1).c_str());
	return true;
}

// ---------------------------------------------------------------------
// PacketHeader
// ---------------------------------------------------------------------

void PacketHeader::Read(const char* data)
{
	type = GetInt(data, 0);
	fieldA = GetInt(data, 4);
	fieldB = GetInt(data, 8);
	fieldC = GetInt(data, 12);
	fieldD = GetInt(data, 16);
	ticket = GetInt(data, 20);
	furtherData = GetInt(data, 24);
	fieldE = GetInt(data, 28);
}

void PacketHeader::Write(std::vector<char>& out) const
{
	PutInt(out, type);
	PutInt(out, fieldA);
	PutInt(out, fieldB);
	PutInt(out, fieldC);
	PutInt(out, fieldD);
	PutInt(out, ticket);
	PutInt(out, furtherData);
	PutInt(out, fieldE);
}

// ---------------------------------------------------------------------
// Packet builders
// ---------------------------------------------------------------------

std::vector<char> BuildHandshake(const BabelUIN& user, int ticket,
	const std::string& nickname, const std::string& password,
	int magic)
{
	// 52 byte header, then nickname and password with terminators
	PacketHeader header;
	header.type = PACKET_HANDSHAKE;
	header.fieldC = user.uid;
	header.fieldD = user.hid;
	header.ticket = ticket;

	std::vector<char> out;
	header.Write(out);
	PutInt(out, HANDSHAKE_CLIENT_VERSION);
	PutInt(out, HANDSHAKE_PRODUCT_CODE);
	// 0 marks us as a stock Babel client, which keeps all of
	// Natsue's compatibility workarounds enabled;
	// HANDSHAKE_MAGIC_MODERN claims the not-actually-Babel extension
	PutInt(out, magic);
	PutInt(out, nickname.size() + 1);	// lengths include the terminator
	PutInt(out, password.size() + 1);
	out.insert(out.end(), nickname.begin(), nickname.end());
	out.push_back('\0');
	out.insert(out.end(), password.begin(), password.end());
	out.push_back('\0');
	return out;
}

std::vector<char> BuildWWRModify(bool add, const BabelUIN& server,
	const BabelUIN& target)
{
	PacketHeader header;
	header.type = add ? PACKET_ADD_WWR_ENTRY : PACKET_REMOVE_WWR_ENTRY;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = target.uid;
	header.fieldD = target.hid;

	std::vector<char> out;
	header.Write(out);
	return out;
}

std::vector<char> BuildGetClientInfo(const BabelUIN& server,
	const BabelUIN& target, int ticket)
{
	PacketHeader header;
	header.type = PACKET_GET_CLIENT_INFO;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = target.uid;
	header.fieldD = target.hid;
	header.ticket = ticket;

	std::vector<char> out;
	header.Write(out);
	return out;
}

std::vector<char> BuildGetConnectionDetail(const BabelUIN& server,
	const BabelUIN& target, int ticket)
{
	PacketHeader header;
	header.type = PACKET_GET_CONNECTION_DETAIL;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = target.uid;
	header.fieldD = target.hid;
	header.ticket = ticket;

	std::vector<char> out;
	header.Write(out);
	return out;
}

std::vector<char> BuildGetStatus(const BabelUIN& server, int ticket)
{
	PacketHeader header;
	header.type = PACKET_GET_STATUS;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.ticket = ticket;

	std::vector<char> out;
	header.Write(out);
	return out;
}

std::vector<char> BuildFetchRandomUser(const BabelUIN& server,
	const BabelUIN& user, int ticket)
{
	PacketHeader header;
	header.type = PACKET_DS_FETCH_RANDOM_USER;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = user.uid;
	header.fieldD = user.hid;
	header.ticket = ticket;
	header.fieldE = 3; // always 3, meaning unknown

	std::vector<char> out;
	header.Write(out);
	return out;
}

std::vector<char> BuildFeedHistory(const BabelUIN& server,
	const BabelUIN& user, int ticket, const char* blob, int blobSize)
{
	PacketHeader header;
	header.type = PACKET_DS_FEED_HISTORY;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = user.uid;
	header.fieldD = user.hid;
	header.ticket = ticket;
	header.furtherData = blobSize;

	std::vector<char> out;
	header.Write(out);
	out.insert(out.end(), blob, blob + blobSize);
	return out;
}

std::vector<char> BuildMessage(const BabelUIN& server, const BabelUIN& user,
	const BabelUIN& target, const char* payload, int payloadSize)
{
	// The payload (12 byte BinaryMessage header plus data) is
	// wrapped in a 24 byte Packed Babel Message header:
	//   +0 total size  +4 sender HID  +8 sender UID
	//   +12 message data size  +16 "something else" size  +20 major type
	const int packedHeaderSize = 24;
	int packedSize = packedHeaderSize + payloadSize;

	PacketHeader header;
	header.type = PACKET_MESSAGE;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = user.uid;
	header.fieldD = user.hid;
	header.furtherData = packedSize;

	std::vector<char> out;
	header.Write(out);
	// CTOS message packets carry the target after the header
	PutInt(out, target.uid);
	PutInt(out, target.hid);
	// Packed Babel Message header.  Note HID before UID
	PutInt(out, packedSize);
	PutInt(out, user.hid);
	PutInt(out, user.uid);
	PutInt(out, payloadSize);
	PutInt(out, 0);
	PutInt(out, 1); // major type 1, binary message
	out.insert(out.end(), payload, payload + payloadSize);
	return out;
}

std::vector<char> BuildVirtualConnectAccept(const BabelUIN& server,
	const BabelUIN& initiator, int initiatorVSN, int localVSN)
{
	PacketHeader header;
	header.type = PACKET_CLIENT_COMMAND;
	header.fieldA = server.uid;
	header.fieldB = server.hid;
	header.fieldC = initiator.uid;
	header.fieldD = initiator.hid;
	// Upper half is the initiator's VSN from the connect packet,
	// lower half is our own (never zero, zero means failure)
	header.fieldE = ((initiatorVSN & 0xFFFF) << 16) | (localVSN & 0xFFFF);

	std::vector<char> out;
	header.Write(out);
	PutInt(out, CLIENT_COMMAND_VIRTUAL_CONNECT_ACCEPT);
	return out;
}

// ---------------------------------------------------------------------
// Packet parsers
// ---------------------------------------------------------------------

bool ParsePackedBabelMessage(const char* data, int size,
	BabelUIN& sender, int& messageType, std::string& payload)
{
	const int packedHeaderSize = 24;
	const int c2eHeaderSize = 12;
	if (size < packedHeaderSize + c2eHeaderSize)
		return false;

	// Header.  HID is stored before UID, see BuildMessage
	sender.hid = GetInt(data, 4) & 0xFFFF;
	sender.uid = GetInt(data, 8);
	int messageDataLen = GetInt(data, 12);

	if (messageDataLen < c2eHeaderSize ||
		messageDataLen > size - packedHeaderSize)
		return false;

	// C2E message header: length (12), type, reserved
	messageType = GetInt(data, packedHeaderSize + 4);

	int payloadOffset = packedHeaderSize + c2eHeaderSize;
	int payloadSize = messageDataLen - c2eHeaderSize;
	payload.assign(data + payloadOffset, payloadSize);
	return true;
}

bool ParseShortUserData(const char* data, int size, ShortUserData& out)
{
	const int fixedSize = 24;
	if (size < fixedSize)
		return false;

	// +0 is the total length of the structure, checked lightly
	int totalLen = GetInt(data, 0);
	if (totalLen > size)
		return false;

	out.uin.uid = GetInt(data, 4);
	out.uin.hid = GetInt(data, 8) & 0xFFFF;
	int firstNameLen = GetInt(data, 12);
	int lastNameLen = GetInt(data, 16);
	int nickNameLen = GetInt(data, 20);

	if (firstNameLen < 0 || lastNameLen < 0 || nickNameLen < 0)
		return false;
	// Guard each addition against overflow
	if (firstNameLen > size - fixedSize ||
		lastNameLen > size - fixedSize - firstNameLen ||
		nickNameLen > size - fixedSize - firstNameLen - lastNameLen)
		return false;

	int offset = fixedSize;
	out.firstName.assign(data + offset, firstNameLen);
	offset += firstNameLen;
	out.lastName.assign(data + offset, lastNameLen);
	offset += lastNameLen;
	out.nickName.assign(data + offset, nickNameLen);
	return true;
}

} // namespace NetNatsueProtocol
