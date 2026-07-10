// CBabelServerInfo.h
//
// Details of one game server: hostname, friendly name, port and
// server ID.  The engine module reads and writes these to
// server.cfg, and the server can send an updated list in its
// handshake response.
//
// Recreated for NetNatsue.  The original class lived in the
// BabelClient library; only the surface used by the engine module
// is provided here.

#ifndef CBABEL_SERVER_INFO_H
#define CBABEL_SERVER_INFO_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>

class CBabelServerInfo
{
public:
	CBabelServerInfo()
		: myPort(0), myID(0)
	{
	}

	void SetServer(const std::string& host, const std::string& friendlyName, int port, int id)
	{
		myHost = host;
		myFriendlyName = friendlyName;
		myPort = port;
		myID = id;
	}

	std::string* GetServer() { return &myHost; }
	std::string* GetNotes() { return &myFriendlyName; }
	int GetPort() const { return myPort; }
	int GetID() const { return myID; }

private:
	std::string myHost;
	std::string myFriendlyName;
	int myPort;
	int myID;
};

#endif // CBABEL_SERVER_INFO_H
