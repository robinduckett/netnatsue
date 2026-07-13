// NetNatsueTest.cpp
//
// Standalone protocol test for the NetNatsue library.  Connects
// two DSNetManager instances to a Natsue server (a local one by
// default) and exercises the same call sequences the engine's
// netbabel module makes: login, presence, user queries, PRAY
// message transfer through the outbox/inbox spools, NET: WRIT
// style direct messages and creature history upload.
//
// Build with CMake ("cmake -B build && cmake --build build"), or
// directly: g++ -Wall -I. -o netnatsue-test test/NetNatsueTest.cpp
// NetMemoryPack.cpp NetMemoryUnpack.cpp NetNatsueProtocol.cpp
// NetNatsueSocket.cpp DSNetManager.cpp (add -lws2_32 on Windows)
//
// Run a Natsue server on localhost:49152 first (firewall level
// "minimal" recommended so hand-rolled PRAY files pass through).

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <iostream>
#include <string>
#include <vector>

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#include <direct.h>
	#define MakeDir(d) _mkdir(d)
	static void TestSleep(int ms) { Sleep(ms); }
#else
	#include <sys/stat.h>
	#include <unistd.h>
	#include <dirent.h>
	#define MakeDir(d) mkdir(d, 0777)
	static void TestSleep(int ms) { usleep(ms * 1000); }
#endif

#include "../DSNetManager.h"
#include "../NetLogInterface.h"
#include "../NetMessages.h"
#include "../NetMemoryPack.h"
#include "../NetNatsueProtocol.h"

static int theFailures = 0;
static int theChecks = 0;

static void Check(bool condition, const char* what)
{
	++theChecks;
	if (condition)
	{
		std::cout << "  ok: " << what << std::endl;
	}
	else
	{
		++theFailures;
		std::cout << "  FAIL: " << what << std::endl;
	}
}

class StdoutLog : public NetLogInterface
{
public:
	StdoutLog(const char* name) : myName(name) {}
	virtual void Log(const char* text)
	{
		std::cout << "  [" << myName << "] " << text << std::endl;
	}
private:
	std::string myName;
};

// Let both managers move data for a while
static void PumpBoth(DSNetManager& a, DSNetManager& b, int ms)
{
	int elapsed = 0;
	while (elapsed < ms)
	{
		a.SendOrdinaryMessages();
		b.SendOrdinaryMessages();
		TestSleep(50);
		elapsed += 50;
	}
}

// Offline: the two client modes must differ only in the handshake
// extension field at offset +40 (tob/Packets/CTOS.md)
static void TestHandshakeBytes()
{
	using namespace NetNatsueProtocol;

	std::cout << "Handshake bytes:" << std::endl;
	BabelUIN uin(42, 1);
	std::vector<char> original = BuildHandshake(uin, 7, "nick", "pass", 0);
	std::vector<char> modern = BuildHandshake(uin, 7, "nick", "pass",
		HANDSHAKE_MAGIC_MODERN);
	Check(original.size() == 52 + 5 + 5, "handshake is 52 bytes plus strings");
	Check(original.size() == modern.size(), "same length in both modes");
	Check(GetInt(&original[0], 40) == 0, "original mode sends 0 at +40");
	Check(GetInt(&modern[0], 40) == HANDSHAKE_MAGIC_MODERN,
		"modern mode sends the Natsue magic at +40");
	bool sameOtherwise = true;
	for (size_t i = 0; i < original.size() && sameOtherwise; ++i)
		if (i < 40 || i >= 44)
			sameOtherwise = (original[i] == modern[i]);
	Check(sameOtherwise, "modes differ only in the +40 field");
}

// A minimal but well-formed PRAY file: the magic followed by one
// uncompressed chunk with an empty pair of tag groups
static std::string MakeTestPrayFile(const char* chunkType, const char* chunkName)
{
	using namespace NetNatsueProtocol;

	// Tag data: no int tags, no string tags
	std::vector<char> tagData;
	PutInt(tagData, 0);
	PutInt(tagData, 0);

	std::vector<char> file;
	file.push_back('P'); file.push_back('R'); file.push_back('A'); file.push_back('Y');
	// chunk type, 4 bytes
	file.insert(file.end(), chunkType, chunkType + 4);
	// chunk name, 128 bytes zero padded
	char name[128];
	memset(name, 0, sizeof(name));
	strncpy(name, chunkName, sizeof(name) - 1);
	file.insert(file.end(), name, name + sizeof(name));
	PutInt(file, (int)tagData.size());	// compressed size
	PutInt(file, (int)tagData.size());	// uncompressed size
	PutInt(file, 0);					// flags: not compressed
	file.insert(file.end(), tagData.begin(), tagData.end());

	return std::string(&file[0], file.size());
}

// A creature history blob for a fictional creature, packed the
// same way HistoryTransferOut does it
static std::string MakeTestHistoryBlob(const std::string& userID)
{
	NetMemoryPack pack(512);
	std::string moniker = "001-test-aaaaa-bbbbb-ccccc-ddddd";
	pack.PackIn(moniker);
	bool hasState = true;
	pack.PackIn(&hasState, sizeof(hasState));
	pack.PackInInt(1);	// sex: male
	pack.PackInInt(0);	// genus: norn
	pack.PackInInt(1);	// variant
	pack.PackInInt(0);	// point mutations
	pack.PackInInt(0);	// crossover points
	int eventCount = 1;
	pack.PackIn(&eventCount, sizeof(eventCount));
	// one "conceived" style event
	pack.PackInInt(0);	// event type
	pack.PackInInt(100);	// world tick
	pack.PackInInt(0);	// age in ticks
	pack.PackInInt(1234567890);	// real world time
	pack.PackInInt(0);	// life stage
	pack.PackIn(std::string(""));	// related moniker 1
	pack.PackIn(std::string(""));	// related moniker 2
	pack.PackIn(std::string("Test World"));
	pack.PackIn(std::string("ds-test-world-uid"));
	pack.PackIn(userID);	// network user
	pack.PackInInt(0);	// solid index
	pack.PackIn(std::string("Testy"));	// name
	int userTextCount = 0;
	pack.PackIn(&userTextCount, sizeof(userTextCount));

	return std::string((char*)pack.GetAddress(), pack.GetLength());
}

int main(int argc, char* argv[])
{
	if (argc > 1 && std::string(argv[1]) == "--offline")
	{
		TestHandshakeBytes();
		std::cout << theChecks << " checks, " << theFailures
			<< " failures" << std::endl;
		return theFailures ? 1 : 0;
	}

	std::string host = "localhost";
	int port = 49152;
	if (argc > 1)
		host = argv[1];
	if (argc > 2)
		port = atoi(argv[2]);

	std::cout << "NetNatsue protocol test against " << host << ":" << port << std::endl;

	TestHandshakeBytes();

	MakeDir("nn_test_in1");
	MakeDir("nn_test_out1");
	MakeDir("nn_test_in2");
	MakeDir("nn_test_out2");

	DSNetManager::OverrideHost(host, port);

	StdoutLog log1("one");
	StdoutLog log2("two");
	DSNetManager net1(&log1);
	DSNetManager net2(&log2);
	net1.SetInboxDirectory("nn_test_in1/");
	net1.SetOutboxDirectory("nn_test_out1/");
	net2.SetInboxDirectory("nn_test_in2/");
	net2.SetOutboxDirectory("nn_test_out2/");

	// --- login ------------------------------------------------------
	std::cout << "* Login" << std::endl;
	net1.SetUser("nntest1", "wobble1");
	net2.SetUser("nntest2", "wobble2");
	Check(net1.Connect(), "first user connects and logs in");
	Check(net2.Connect(), "second user connects and logs in");
	Check(net1.Online(), "first user is online");
	Check(net1.GetLastError() == DSNetManager::ERROR_OK, "NET: ERRA would give 1");

	std::string user1 = net1.GetUser();
	std::string user2 = net2.GetUser();
	std::cout << "  (server assigned " << user1 << " and " << user2 << ")" << std::endl;
	Check(!user1.empty(), "first user has a user id");
	Check(!user2.empty(), "second user has a user id");

	// --- the natsue_version greeting ----------------------------------
	// Natsue sends every fresh login a NET: WRIT on the
	// "natsue_version" channel; the game runs it as an ordinary
	// message script.  Drain it so later tests start clean.
	std::cout << "* Login greeting" << std::endl;
	{
		std::vector<std::string> users;
		std::vector<NetMessages::MessageType> types;
		std::vector<std::string> messages;
		int spins = 0;
		while (users.empty() && spins < 100)
		{
			PumpBoth(net1, net2, 50);
			net1.GetMessages(users, types, messages);
			++spins;
		}
		bool sawGreeting = false;
		std::vector<std::string>::size_type i;
		for (i = 0; i < messages.size(); ++i)
		{
			if (types[i] == NetMessages::MESG_WRIT &&
				messages[i].size() > 4 + 14 &&
				messages[i].compare(4, 14, "natsue_version") == 0)
				sawGreeting = true;
		}
		Check(sawGreeting, "server's natsue_version greeting writ arrives");

		// and drain the other client's copy
		users.clear(); types.clear(); messages.clear();
		spins = 0;
		while (users.empty() && spins < 100)
		{
			PumpBoth(net1, net2, 50);
			net2.GetMessages(users, types, messages);
			++spins;
		}
	}

	// --- bad login --------------------------------------------------
	std::cout << "* Bad password is refused" << std::endl;
	{
		DSNetManager netBad;
		netBad.SetUser("nntest1", "not-the-password");
		Check(!netBad.Connect(), "wrong password fails to connect");
		Check(netBad.GetLastError() == DSNetManager::ERROR_BAD_USER,
			"NET: ERRA would give 3 (invalid nickname/password)");
	}

	// --- presence ---------------------------------------------------
	std::cout << "* Presence" << std::endl;
	Check(net1.IsUserOnline(user2), "second user reads as online (server query)");
	Check(!net1.IsUserOnline("999999+1"), "unknown user reads as offline");
	Check(!net1.IsUserOnline("i am not a user id"), "garbage user id reads as offline");

	// watch user2 the way NET: WHON does
	{
		bool block = true;
		int spins = 0;
		while (block && spins < 200)
		{
			net1.AddToWWR(user2, block);
			if (block)
				TestSleep(50);
			++spins;
		}
		Check(!block, "NET: WHON completes");
		Check(net1.IsUserOnline(user2), "watched user reads as online (cached)");
	}

	// --- user data (NET: UNIK) --------------------------------------
	std::cout << "* FetchUserData" << std::endl;
	{
		std::string nick;
		bool block = true;
		int spins = 0;
		while (block && spins < 200)
		{
			net1.FetchUserData(user2, nick, block);
			if (block)
				TestSleep(50);
			++spins;
		}
		Check(nick == "nntest2", "NET: UNIK returns the other user's nickname");
	}

	// --- random user (NET: RUSO) ------------------------------------
	std::cout << "* DSFetchRandomUser" << std::endl;
	{
		std::string randomUser = net1.DSFetchRandomUser();
		std::cout << "  (got \"" << randomUser << "\")" << std::endl;
		Check(randomUser == user1 || randomUser == user2,
			"random online user is one of our two users");
	}

	// --- status (NET: STAT) -----------------------------------------
	std::cout << "* GetStatus" << std::endl;
	{
		int timeOnline = -1, usersOnline = -1, bytesReceived = -1, bytesSent = -1;
		bool block = true;
		int spins = 0;
		while (block && spins < 200)
		{
			net1.GetStatus(timeOnline, usersOnline, bytesReceived, bytesSent, block);
			if (block)
				TestSleep(50);
			++spins;
		}
		std::cout << "  (time " << timeOnline << "ms, sent " << bytesSent
			<< ", received " << bytesReceived << ")" << std::endl;
		Check(timeOnline >= 0, "time online is reported");
		Check(bytesSent > 0 && bytesReceived > 0, "byte counts are reported");
	}

	// --- direct message (NET: WRIT path) ----------------------------
	std::cout << "* Direct messages" << std::endl;
	{
		// Same packing NetHandlers::SubCommand_NET_WRIT performs
		std::string channel = "test_channel";
		int channelLength = (int)channel.size();
		int msgid = 1000;
		int intType = 0, intValue = 42;			// _p1_: integer 42
		int strType = 2;						// _p2_: string "hello"
		std::string strValue = "hello";
		int strLen = (int)strValue.size();

		NetMessages::BinaryMessage header;
		header.length = sizeof(header);
		header.type = NetMessages::MESG_WRIT;
		header.reserved = 0;

		int total = sizeof(header) + sizeof(int) + channelLength + sizeof(int)
			+ sizeof(int) * 2 + sizeof(int) * 2 + sizeof(int) + strLen;
		NetMemoryPack pack(total);
		pack.PackIn(&header, sizeof(header));
		pack.PackIn(&channelLength, sizeof(channelLength));
		pack.PackIn(&channel[0], channelLength);
		pack.PackIn(&msgid, sizeof(msgid));
		pack.PackIn(&intType, sizeof(intType));
		pack.PackIn(&intValue, sizeof(intValue));
		pack.PackIn(&strType, sizeof(strType));
		pack.PackIn(&strLen, sizeof(strLen));
		pack.PackIn(&strValue[0], strLen);

		Check(net1.PostDirectMessage(user2, (char*)pack.GetAddress(), total),
			"NET: WRIT style message posts");

		// Wait for it to arrive at the other end
		std::vector<std::string> users;
		std::vector<NetMessages::MessageType> types;
		std::vector<std::string> messages;
		int spins = 0;
		while (users.empty() && spins < 100)
		{
			PumpBoth(net1, net2, 50);
			net2.GetMessages(users, types, messages);
			++spins;
		}
		Check(users.size() == 1, "exactly one message arrives");
		if (users.size() == 1)
		{
			Check(users[0] == user1, "message reports the right sender");
			Check(types[0] == NetMessages::MESG_WRIT, "message has the writ type");
			// payload should start with the channel name again
			Check(messages[0].size() > 4 + channel.size() &&
				messages[0].compare(4, channel.size(), channel) == 0,
				"payload carries the channel name");
		}
	}

	// --- PRAY transfer through the outbox/inbox spools ---------------
	std::cout << "* Warp file transfer" << std::endl;
	{
		std::string outName = net1.ReturnUniqueOutboxFilename(user2);
		Check(!outName.empty(), "outbox filename is issued");
		Check(net1.ReturnUniqueOutboxFilename("not a user").empty(),
			"outbox filename refused for a bad user id");

		std::string pray = MakeTestPrayFile("MESG", "nn_test_message");
		FILE* f = fopen(outName.c_str(), "wb");
		Check(f != NULL, "outbox file can be created");
		if (f)
		{
			fwrite(pray.data(), 1, pray.size(), f);
			fclose(f);
		}

		net1.SendOrdinaryMessages(); // notices the outbox file and sends it

		// Wait for the file to land in net2's inbox
		std::string arrived;
		int spins = 0;
		while (arrived.empty() && spins < 200)
		{
			PumpBoth(net1, net2, 50);
#ifdef _WIN32
			WIN32_FIND_DATAA findData;
			HANDLE find = FindFirstFileA("nn_test_in2/*.warp", &findData);
			if (find != INVALID_HANDLE_VALUE)
			{
				arrived = findData.cFileName;
				FindClose(find);
			}
#else
			DIR* dir = opendir("nn_test_in2");
			if (dir)
			{
				struct dirent* entry;
				while ((entry = readdir(dir)) != NULL)
				{
					std::string name = entry->d_name;
					if (name.size() > 5 &&
						name.compare(name.size() - 5, 5, ".warp") == 0)
					{
						arrived = name;
						break;
					}
				}
				closedir(dir);
			}
#endif
			++spins;
		}
		Check(!arrived.empty(), "warp file arrives in the recipient's inbox");
		if (!arrived.empty())
		{
			std::cout << "  (spooled as " << arrived << ")" << std::endl;
			Check(net2.FindUserWhoSentFile("nn_test_in2/" + arrived) == user1,
				"NET: FROM identifies the sender");

			// The server re-encodes PRAY files in transit, so check
			// the structure rather than for byte equality
			std::string received;
			FILE* in = fopen(("nn_test_in2/" + arrived).c_str(), "rb");
			if (in)
			{
				char chunk[4096];
				size_t got;
				while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0)
					received.append(chunk, got);
				fclose(in);
			}
			Check(received.size() >= 8 &&
				received.compare(0, 4, "PRAY") == 0 &&
				received.compare(4, 4, "MESG") == 0 &&
				received.find("nn_test_message") != std::string::npos,
				"received file is a PRAY file with our chunk in it");
		}

		Check(net1.FindUserWhoSentFile("nowhere/junk.warp").empty(),
			"FindUserWhoSentFile returns nothing for an unaddressed file");
	}

	// --- creature history upload -------------------------------------
	std::cout << "* Creature history upload" << std::endl;
	{
		std::string blob = MakeTestHistoryBlob(user1);
		Check(net1.DSFeedHistory((char*)blob.data(), (int)blob.size()),
			"history upload is confirmed by the server");
	}

	// --- offline notification through the WWR -------------------------
	std::cout << "* WWR offline notification" << std::endl;
	{
		net2.Disconnect();
		std::string changedUser;
		bool online = true;
		bool seen = false;
		int spins = 0;
		while (!seen && spins < 100)
		{
			net1.SendOrdinaryMessages();
			net1.ResetIteratorWWR();
			std::string user;
			bool state;
			while (net1.NextChangedUserWWR(user, state))
			{
				if (user == user2)
				{
					seen = true;
					changedUser = user;
					online = state;
				}
			}
			TestSleep(50);
			++spins;
		}
		Check(seen, "watched user's disconnect is notified");
		Check(!online, "and they are reported offline");
		Check(!net1.IsUserOnline(user2), "IsUserOnline agrees they are gone");
	}

	// --- disconnect ---------------------------------------------------
	std::cout << "* Disconnect" << std::endl;
	net1.Disconnect();
	Check(!net1.Online(), "disconnect takes us offline");

	std::cout << std::endl << theChecks << " checks, " << theFailures
		<< " failures" << std::endl;
	return theFailures == 0 ? 0 : 1;
}
