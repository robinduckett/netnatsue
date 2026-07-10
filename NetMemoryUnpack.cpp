#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "NetMemoryUnpack.h"

#include <string.h>

NetMemoryUnpack::NetMemoryUnpack(const void* data, int size)
	: myBuffer((const char*)data), mySize(size), myRead(0)
{
	if (mySize < 0)
		mySize = 0;
}

bool NetMemoryUnpack::PackOut(void* data, int size)
{
	if (size < 0)
		return false;
	if (myRead + size > mySize)
		return false;
	memcpy(data, myBuffer + myRead, size);
	myRead += size;
	return true;
}

bool NetMemoryUnpack::PackOut(std::string& str)
{
	int len = 0;
	if (!PackOut(&len, sizeof(len)))
		return false;
	if (len < 0 || myRead + len > mySize)
		return false;
	str.assign(myBuffer + myRead, len);
	myRead += len;
	return true;
}

int NetMemoryUnpack::Remaining()
{
	return mySize - myRead;
}
