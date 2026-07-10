// NetNatsueSocket.h
//
// Minimal cross-platform TCP client socket for the NetNatsue
// library.  Wraps Winsock2 on Windows and BSD sockets on
// Linux/macOS behind one non-blocking interface.
//
// The socket is always non-blocking.  Writes are buffered
// internally and flushed opportunistically, so callers never
// block or have to deal with partial writes.  Reads accumulate
// into an internal buffer which the protocol layer inspects for
// complete packets.

#ifndef NET_NATSUE_SOCKET_H
#define NET_NATSUE_SOCKET_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>
#include <vector>

class NetNatsueSocket
{
public:
	enum State
	{
		STATE_CLOSED,
		STATE_RESOLVING,
		STATE_CONNECTING,
		STATE_CONNECTED,
		STATE_FAILED,
	};

	NetNatsueSocket();
	~NetNatsueSocket();

	// Begin a non-blocking connection.  Name resolution is
	// performed synchronously (normally fast, cached by the OS).
	// Poll with PumpConnect until the state settles.
	bool BeginConnect(const std::string& host, int port);
	// Progress a pending connection; returns the current state.
	State PumpConnect();

	State GetState() const { return myState; }
	bool IsConnected() const { return myState == STATE_CONNECTED; }

	void Close();

	// Queue data for sending; flushed by Pump.  Returns false if
	// the socket is not connected.
	bool Write(const void* data, int size);

	// Move data both ways: flush as much of the output buffer as
	// the OS will take, and pull everything available into the
	// input buffer.  Returns false if the connection has been
	// lost (the socket closes itself in that case).
	bool Pump();

	// Block for up to timeoutMs waiting for readable data (or a
	// dead connection).  Used only by bounded synchronous
	// transactions.  Returns true if there may be new data.
	bool WaitReadable(int timeoutMs);

	// Access to the input buffer
	int InputSize() const { return (int)myInput.size(); }
	const char* InputData() const { return myInput.empty() ? "" : &myInput[0]; }
	void ConsumeInput(int bytes);

	// Bytes moved over the wire since construction
	int GetBytesSent() const { return myBytesSent; }
	int GetBytesReceived() const { return myBytesReceived; }

	// OS level error code from the last failure (errno / WSA error)
	int GetLastSystemError() const { return myLastSystemError; }

private:
	// not copyable
	NetNatsueSocket(const NetNatsueSocket&);
	NetNatsueSocket& operator=(const NetNatsueSocket&);

	bool SetNonBlocking();
	void RecordSystemError();
	bool FlushOutput();
	bool ReadInput();

	// Opaque handle, so OS headers stay out of this header.
	// Large enough for SOCKET on Win64 and int elsewhere.
	void* myHandle;
	State myState;

	std::vector<char> myInput;
	std::vector<char> myOutput;

	int myBytesSent;
	int myBytesReceived;
	int myLastSystemError;
};

#endif // NET_NATSUE_SOCKET_H
