// NetMemoryUnpack.h
//
// Companion to NetMemoryPack.  Extracts values from a binary
// blob received over the network.  All PackOut methods return
// false if there is not enough data left, so a truncated or
// malicious message can never read out of bounds.
//
// Recreated for NetNatsue.

#ifndef NET_MEMORY_UNPACK_H
#define NET_MEMORY_UNPACK_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>

class NetMemoryUnpack
{
public:
	NetMemoryUnpack(const void* data, int size);

	// Raw copy of memory out of the pack
	bool PackOut(void* data, int size);
	// Integer length followed by characters, as packed by NetMemoryPack
	bool PackOut(std::string& str);

	int Remaining();

private:
	const char* myBuffer;
	int mySize;
	int myRead;
};

#endif // NET_MEMORY_UNPACK_H
