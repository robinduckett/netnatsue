#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "NetMemoryPack.h"

#include <string.h>

NetMemoryPack::NetMemoryPack(int size)
	: myBuffer(NULL), mySize(size), myUsed(0)
{
	if (mySize < 0)
		mySize = 0;
	myBuffer = new char[mySize + 1]; // +1 so a zero sized pack still has an address
}

NetMemoryPack::~NetMemoryPack()
{
	delete[] myBuffer;
}

void NetMemoryPack::PackIn(const void* data, int size)
{
	if (size <= 0)
		return;
	if (myUsed + size > mySize)
		return; // overflow; drop, the caller precalculated wrongly
	memcpy(myBuffer + myUsed, data, size);
	myUsed += size;
}

void NetMemoryPack::PackIn(const std::string& str)
{
	int len = str.size();
	PackIn(&len, sizeof(len));
	if (len > 0)
		PackIn(str.data(), len);
}

void NetMemoryPack::PackInInt(int value)
{
	PackIn(&value, sizeof(value));
}

void* NetMemoryPack::GetAddress()
{
	return myBuffer;
}

int NetMemoryPack::GetLength()
{
	return myUsed;
}
