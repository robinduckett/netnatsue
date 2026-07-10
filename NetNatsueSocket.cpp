#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "NetNatsueSocket.h"

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <winsock2.h>
	typedef SOCKET OSSocket;
	typedef int socklen_type;
	#define NN_INVALID_SOCKET INVALID_SOCKET
	#define NN_SOCKET_ERROR SOCKET_ERROR
	#define NN_EWOULDBLOCK WSAEWOULDBLOCK
	#define NN_EINPROGRESS WSAEWOULDBLOCK
	#define NN_EINTR WSAEINTR
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <sys/time.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <errno.h>
	typedef int OSSocket;
	typedef socklen_t socklen_type;
	#define NN_INVALID_SOCKET (-1)
	#define NN_SOCKET_ERROR (-1)
	#define NN_EWOULDBLOCK EWOULDBLOCK
	#define NN_EINPROGRESS EINPROGRESS
	#define NN_EINTR EINTR
#endif

#include <string.h>

// ---------------------------------------------------------------------
// Small platform helpers
// ---------------------------------------------------------------------

static int OSLastError()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

static void OSCloseSocket(OSSocket s)
{
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

#ifdef _WIN32
// Winsock needs starting up once per process
static bool EnsureWinsock()
{
	static bool started = false;
	if (!started)
	{
		WSADATA data;
		if (WSAStartup(MAKEWORD(2, 0), &data) != 0)
			return false;
		started = true;
	}
	return true;
}
#endif

// The handle is stored in a void* so OS headers stay out of our
// header.  These two helpers do the conversion in one place.
static OSSocket HandleToSocket(void* handle)
{
	return (OSSocket)(size_t)handle;
}

static void* SocketToHandle(OSSocket s)
{
	return (void*)(size_t)s;
}

// ---------------------------------------------------------------------
// NetNatsueSocket
// ---------------------------------------------------------------------

NetNatsueSocket::NetNatsueSocket()
	: myHandle(SocketToHandle(NN_INVALID_SOCKET)), myState(STATE_CLOSED),
	  myBytesSent(0), myBytesReceived(0), myLastSystemError(0)
{
}

NetNatsueSocket::~NetNatsueSocket()
{
	Close();
}

void NetNatsueSocket::RecordSystemError()
{
	myLastSystemError = OSLastError();
}

bool NetNatsueSocket::SetNonBlocking()
{
	OSSocket s = HandleToSocket(myHandle);
#ifdef _WIN32
	unsigned long nonBlocking = 1;
	if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
		return false;
#else
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0)
		return false;
	if (fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
		return false;
#endif
	return true;
}

bool NetNatsueSocket::BeginConnect(const std::string& host, int port)
{
	Close();

#ifdef _WIN32
	if (!EnsureWinsock())
	{
		myState = STATE_FAILED;
		RecordSystemError();
		return false;
	}
#endif

	// Resolve the host name.  gethostbyname is old fashioned but
	// works everywhere this engine builds, VC6 included.
	unsigned long address = inet_addr(host.c_str());
	if (address == INADDR_NONE)
	{
		struct hostent* hostEntry = gethostbyname(host.c_str());
		if (!hostEntry || hostEntry->h_addrtype != AF_INET || !hostEntry->h_addr_list[0])
		{
			myState = STATE_FAILED;
			RecordSystemError();
			return false;
		}
		memcpy(&address, hostEntry->h_addr_list[0], sizeof(address));
	}

	OSSocket s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == NN_INVALID_SOCKET)
	{
		myState = STATE_FAILED;
		RecordSystemError();
		return false;
	}
	myHandle = SocketToHandle(s);

	if (!SetNonBlocking())
	{
		RecordSystemError();
		Close();
		myState = STATE_FAILED;
		return false;
	}

	// Keep the connection lively.  The packets are small and NAT
	// routers like to forget about quiet connections
	{
		int optionOn = 1;
		setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optionOn, sizeof(optionOn));
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&optionOn, sizeof(optionOn));
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons((unsigned short)port);
	serverAddress.sin_addr.s_addr = address;

	// A fresh connection starts with fresh buffers
	myInput.clear();
	myOutput.clear();

	if (connect(s, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == 0)
	{
		myState = STATE_CONNECTED;
		return true;
	}

	int error = OSLastError();
	if (error == NN_EINPROGRESS || error == NN_EWOULDBLOCK)
	{
		myState = STATE_CONNECTING;
		return true;
	}

	myLastSystemError = error;
	Close();
	myState = STATE_FAILED;
	return false;
}

NetNatsueSocket::State NetNatsueSocket::PumpConnect()
{
	if (myState != STATE_CONNECTING)
		return myState;

	OSSocket s = HandleToSocket(myHandle);

	fd_set writeSet;
	fd_set errorSet;
	FD_ZERO(&writeSet);
	FD_ZERO(&errorSet);
	FD_SET(s, &writeSet);
	FD_SET(s, &errorSet);
	struct timeval poll;
	poll.tv_sec = 0;
	poll.tv_usec = 0;

	int result = select((int)(s + 1), NULL, &writeSet, &errorSet, &poll);
	if (result < 0)
	{
		RecordSystemError();
		Close();
		myState = STATE_FAILED;
		return myState;
	}
	if (result == 0)
		return myState; // still connecting

	// Check for connection failure
	int socketError = 0;
	socklen_type errorLength = sizeof(socketError);
	getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&socketError, &errorLength);
	if (FD_ISSET(s, &errorSet) || socketError != 0)
	{
		if (socketError != 0)
			myLastSystemError = socketError;
		else
			RecordSystemError();
		Close();
		myState = STATE_FAILED;
		return myState;
	}

	if (FD_ISSET(s, &writeSet))
		myState = STATE_CONNECTED;
	return myState;
}

void NetNatsueSocket::Close()
{
	OSSocket s = HandleToSocket(myHandle);
	if (s != NN_INVALID_SOCKET)
		OSCloseSocket(s);
	myHandle = SocketToHandle(NN_INVALID_SOCKET);
	myState = STATE_CLOSED;
	// Note that the input buffer survives.  Data which arrived
	// before the connection went down (such as a login refusal the
	// server sent just before hanging up) can still be processed.
	// BeginConnect clears it for the next connection.
	myOutput.clear();
}

bool NetNatsueSocket::Write(const void* data, int size)
{
	if (myState != STATE_CONNECTED || size < 0)
		return false;
	if (size == 0)
		return true;
	const char* bytes = (const char*)data;
	myOutput.insert(myOutput.end(), bytes, bytes + size);
	return FlushOutput();
}

bool NetNatsueSocket::FlushOutput()
{
	if (myState != STATE_CONNECTED)
		return false;
	OSSocket s = HandleToSocket(myHandle);

	while (!myOutput.empty())
	{
		int sent = send(s, &myOutput[0], (int)myOutput.size(), 0);
		if (sent > 0)
		{
			myBytesSent += sent;
			myOutput.erase(myOutput.begin(), myOutput.begin() + sent);
			continue;
		}

		int error = OSLastError();
		if (error == NN_EWOULDBLOCK || error == NN_EINTR)
			return true; // try again next pump
		myLastSystemError = error;
		Close();
		return false;
	}
	return true;
}

bool NetNatsueSocket::ReadInput()
{
	if (myState != STATE_CONNECTED)
		return false;
	OSSocket s = HandleToSocket(myHandle);

	char chunk[4096];
	while (true)
	{
		int received = recv(s, chunk, sizeof(chunk), 0);
		if (received > 0)
		{
			myBytesReceived += received;
			myInput.insert(myInput.end(), chunk, chunk + received);
			continue;
		}
		if (received == 0)
		{
			// Orderly shutdown by the server
			Close();
			return false;
		}

		int error = OSLastError();
		if (error == NN_EWOULDBLOCK || error == NN_EINTR)
			return true; // no more data for now
		myLastSystemError = error;
		Close();
		return false;
	}
}

bool NetNatsueSocket::Pump()
{
	if (myState == STATE_CONNECTING)
	{
		PumpConnect();
		return myState == STATE_CONNECTING || myState == STATE_CONNECTED;
	}
	if (myState != STATE_CONNECTED)
		return false;

	if (!FlushOutput())
		return false;
	return ReadInput();
}

bool NetNatsueSocket::WaitReadable(int timeoutMs)
{
	if (myState != STATE_CONNECTED)
		return false;
	OSSocket s = HandleToSocket(myHandle);

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(s, &readSet);
	struct timeval timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;

	int result = select((int)(s + 1), &readSet, NULL, NULL, &timeout);
	return result > 0;
}

void NetNatsueSocket::ConsumeInput(int bytes)
{
	if (bytes <= 0)
		return;
	if (bytes >= (int)myInput.size())
		myInput.clear();
	else
		myInput.erase(myInput.begin(), myInput.begin() + bytes);
}
