// NetMemoryPack.h
//
// Simple memory packer used to build binary blobs for sending
// over the network.  Strings are packed as an integer length
// followed by the raw bytes without a terminator, which is
// the NetBabel wire format for strings.
//
// Recreated for NetNatsue.

#ifndef NET_MEMORY_PACK_H
#define NET_MEMORY_PACK_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>

class NetMemoryPack
{
public:
	NetMemoryPack(int size);
	~NetMemoryPack();

	// Raw copy of memory into the pack
	void PackIn(const void* data, int size);
	// Integer length followed by characters, no terminator
	void PackIn(const std::string& str);
	// Pack a value as a plain int.  Use this for enums and other
	// int-like types whose size you don't want to trust
	void PackInInt(int value);

	void* GetAddress();
	int GetLength();

private:
	// not copyable
	NetMemoryPack(const NetMemoryPack&);
	NetMemoryPack& operator=(const NetMemoryPack&);

	char* myBuffer;
	int mySize;
	int myUsed;
};

#endif // NET_MEMORY_PACK_H
