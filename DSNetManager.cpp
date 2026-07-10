#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "DSNetManager.h"
#include "NetLogInterface.h"
#include "QueuedMessage.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#else
	#include <sys/time.h>
	#include <sys/types.h>
	#include <dirent.h>
	#include <unistd.h>
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
	#define nn_vsnprintf _vsnprintf
#else
	#define nn_vsnprintf vsnprintf
#endif

using namespace NetNatsueProtocol;

// ---------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------

// Overall time allowed for connect plus login
static const int CONNECT_TIMEOUT_MS = 30000;
// How long blocking CAOS commands (NET: UNIK etc) wait for the server
static const int QUERY_TIMEOUT_MS = 10000;
// How long a NET: WHON waits for the server to confirm a user
static const int WWR_ADD_TIMEOUT_MS = 4000;
// How long NET: ULIN may stall the caller for a non-WWR user
static const int USER_ONLINE_TIMEOUT_MS = 5000;
// How long a creature history upload waits for confirmation
static const int HISTORY_TIMEOUT_MS = 8000;
// Outbox rescan period, in case a dirty flag was missed
static const int OUTBOX_RESCAN_MS = 5000;
// Upper bound on claimed further-data lengths; protects the framer
static const int MAX_FURTHER_DATA = 32 * 1024 * 1024;

// The default server, as in the original client
static const char* DEFAULT_HOST = "heart.creatures.net";
static const int DEFAULT_PORT = 49152;
static const int DEFAULT_ID = 1;
static const char* DEFAULT_FRIENDLY_NAME = "Heart";

// ---------------------------------------------------------------------
// Small platform helpers
// ---------------------------------------------------------------------

static unsigned int NowMs()
{
#ifdef _WIN32
	return (unsigned int)GetTickCount();
#else
	struct timeval now;
	gettimeofday(&now, NULL);
	return (unsigned int)(now.tv_sec * 1000 + now.tv_usec / 1000);
#endif
}

// Wrap-around safe "has deadline passed"
static bool TimeAfter(unsigned int now, unsigned int deadline)
{
	return (int)(now - deadline) >= 0;
}

static void SleepMs(int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

static bool FileExists(const std::string& path)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (!file)
		return false;
	fclose(file);
	return true;
}

static bool ReadWholeFile(const std::string& path, std::string& contents)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (!file)
		return false;
	contents.erase();
	char chunk[4096];
	size_t got;
	while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
		contents.append(chunk, got);
	bool ok = ferror(file) == 0;
	fclose(file);
	return ok;
}

// Writes via a temporary file and renames it into place, so
// nothing ever sees a half written file
static bool WriteWholeFileAtomic(const std::string& path, const std::string& contents)
{
	std::string tempPath = path + ".tmp";
	FILE* file = fopen(tempPath.c_str(), "wb");
	if (!file)
		return false;
	bool ok = true;
	if (!contents.empty())
		ok = fwrite(contents.data(), 1, contents.size(), file) == contents.size();
	if (fclose(file) != 0)
		ok = false;
	if (!ok)
	{
		remove(tempPath.c_str());
		return false;
	}
	remove(path.c_str()); // rename onto an existing file fails on Windows
	if (rename(tempPath.c_str(), path.c_str()) != 0)
	{
		remove(tempPath.c_str());
		return false;
	}
	return true;
}

// Names (not paths) of every file in the directory with the given
// extension (which includes the dot)
static void ListFilesWithExtension(const std::string& directory,
	const std::string& extension, std::vector<std::string>& names)
{
	names.clear();
#ifdef _WIN32
	std::string pattern = directory + "*" + extension;
	WIN32_FIND_DATAA findData;
	HANDLE find = FindFirstFileA(pattern.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			names.push_back(findData.cFileName);
	}
	while (FindNextFileA(find, &findData));
	FindClose(find);
#else
	DIR* dir = opendir(directory.c_str());
	if (!dir)
		return;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string name = entry->d_name;
		if (name.size() > extension.size() &&
			name.compare(name.size() - extension.size(), extension.size(), extension) == 0)
		{
			names.push_back(name);
		}
	}
	closedir(dir);
#endif
}

static std::string EnsureTrailingSeparator(const std::string& directory)
{
	if (directory.empty())
		return directory;
	char last = directory[directory.size() - 1];
	if (last == '/' || last == '\\')
		return directory;
	return directory + "/";
}

static std::string BaseName(const std::string& path)
{
	std::string::size_type slash = path.find_last_of("/\\");
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}

// Extracts the user id encoded in a spool filename of the form
// <serial>T-<uid>+<hid>.<extension>
static std::string UserFromSpoolName(const std::string& name)
{
	std::string::size_type lastDot = name.rfind('.');
	std::string::size_type lastDash = name.rfind('-');
	if (lastDot == std::string::npos || lastDash == std::string::npos)
		return "";
	if (lastDash + 1 >= lastDot)
		return "";
	std::string user = name.substr(lastDash + 1, lastDot - lastDash - 1);
	BabelUIN uin;
	if (!uin.FromString(user))
		return "";
	return user;
}

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

std::string DSNetManager::ourOverrideHost;
int DSNetManager::ourOverridePort = 0;

DSNetManager::DSNetManager()
{
	Construct(NULL);
}

DSNetManager::DSNetManager(NetLogInterface* logger)
{
	Construct(logger);
}

void DSNetManager::Construct(NetLogInterface* logger)
{
	myLogger = logger;
	myMessageThread = MessageThread(this);
	myConnectPhase = PHASE_IDLE;
	myConnectDeadline = 0;
	myServerRotation = 0;
	myConnectedAtMs = 0;
	myLastError = ERROR_UNKNOWN;
	myRawError = 0;
	myConnectedPort = 0;
	myConnectedID = 0;
	myNextTicketNumber = 0;
	myNextVSN = 0;
	myOutboxDirty = true;
	myNextOutboxScan = 0;
}

DSNetManager::~DSNetManager()
{
	Disconnect();

	// Any messages still queued are dropped; their senders will
	// retry next session
	std::list<QueuedMessage*>::iterator it;
	for (it = myQueuedMessages.begin(); it != myQueuedMessages.end(); ++it)
		delete *it;
	myQueuedMessages.clear();
}

void DSNetManager::Log(const char* format, ...)
{
	if (!myLogger)
		return;
	char buffer[512];
	va_list args;
	va_start(args, format);
	nn_vsnprintf(buffer, sizeof(buffer) - 1, format, args);
	va_end(args);
	buffer[sizeof(buffer) - 1] = '\0';
	myLogger->Log(buffer);
}

int DSNetManager::NextTicket()
{
	++myNextTicketNumber;
	if (myNextTicketNumber == 0) // tickets are never zero
		++myNextTicketNumber;
	return myNextTicketNumber;
}

int DSNetManager::NextVSN()
{
	myNextVSN = (myNextVSN + 1) & 0xFFFF;
	if (myNextVSN == 0) // zero is not a valid virtual socket number
		myNextVSN = 1;
	return myNextVSN;
}

// static
void DSNetManager::OverrideHost(const std::string& host, int port)
{
	ourOverrideHost = host;
	ourOverridePort = port;
}

// ---------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------

void DSNetManager::SetUser(const std::string& nickname, const std::string& password)
{
	myNickname = nickname;
	myPassword = password;
}

bool DSNetManager::UserSet()
{
	return !myNickname.empty();
}

std::string DSNetManager::GetUser()
{
	if (!myUserUIN.IsSet())
		return "";
	return myUserUIN.ToString();
}

bool DSNetManager::Online()
{
	return myConnectPhase == PHASE_ONLINE && mySocket.IsConnected();
}

DSNetManager::Error DSNetManager::GetLastError()
{
	return myLastError;
}

int DSNetManager::RawGetLastError()
{
	return myRawError;
}

std::string DSNetManager::DebugGetCurrentAction()
{
	return myCurrentAction;
}

void DSNetManager::StartConnecting()
{
	// Decide which server to talk to
	if (!ourOverrideHost.empty())
	{
		myConnectedHost = ourOverrideHost;
		myConnectedPort = ourOverridePort != 0 ? ourOverridePort : DEFAULT_PORT;
		myConnectedID = 0;
		myConnectedFriendlyName = "Override";
	}
	else if (!myServers.empty())
	{
		CBabelServerInfo& server = myServers[myServerRotation % myServers.size()];
		++myServerRotation;
		myConnectedHost = *server.GetServer();
		myConnectedPort = server.GetPort() != 0 ? server.GetPort() : DEFAULT_PORT;
		myConnectedID = server.GetID();
		myConnectedFriendlyName = *server.GetNotes();
	}
	else
	{
		myConnectedHost = DEFAULT_HOST;
		myConnectedPort = DEFAULT_PORT;
		myConnectedID = DEFAULT_ID;
		myConnectedFriendlyName = DEFAULT_FRIENDLY_NAME;
	}

	Log("NetNatsue: connecting to %s:%d as \"%s\"",
		myConnectedHost.c_str(), myConnectedPort, myNickname.c_str());
	myCurrentAction = "Connecting to " + myConnectedHost;

	if (!mySocket.BeginConnect(myConnectedHost, myConnectedPort))
	{
		Log("NetNatsue: connection failed immediately (%d)",
			mySocket.GetLastSystemError());
		myLastError = ERROR_OFFLINE;
		myRawError = -mySocket.GetLastSystemError();
		myConnectPhase = PHASE_FAILED;
		myCurrentAction = "";
		return;
	}

	myConnectPhase = PHASE_CONNECTING;
	myConnectDeadline = NowMs() + CONNECT_TIMEOUT_MS;
}

void DSNetManager::PumpConnectPhase()
{
	if (myConnectPhase == PHASE_CONNECTING)
	{
		NetNatsueSocket::State state = mySocket.PumpConnect();
		if (state == NetNatsueSocket::STATE_CONNECTED)
		{
			// Log straight in.  If we have logged in before we
			// quote the user id the server previously gave us.
			myCurrentAction = "Logging in as " + myNickname;
			std::vector<char> handshake = BuildHandshake(
				myUserUIN, NextTicket(), myNickname, myPassword);
			if (!mySocket.Write(&handshake[0], (int)handshake.size()))
			{
				myLastError = ERROR_OFFLINE;
				myRawError = -mySocket.GetLastSystemError();
				myConnectPhase = PHASE_FAILED;
				myCurrentAction = "";
				return;
			}
			myConnectPhase = PHASE_AWAIT_HANDSHAKE;
		}
		else if (state != NetNatsueSocket::STATE_CONNECTING)
		{
			Log("NetNatsue: connection failed (%d)", mySocket.GetLastSystemError());
			myLastError = ERROR_OFFLINE;
			myRawError = -mySocket.GetLastSystemError();
			myConnectPhase = PHASE_FAILED;
			myCurrentAction = "";
			return;
		}
	}

	if (myConnectPhase == PHASE_AWAIT_HANDSHAKE)
	{
		bool alive = mySocket.Pump();
		// Process what arrived even if the server then hung up -
		// a login refusal is sent just before the disconnection
		ProcessInput();
		if (myConnectPhase == PHASE_AWAIT_HANDSHAKE && !alive)
		{
			Log("NetNatsue: connection lost during login (%d)",
				mySocket.GetLastSystemError());
			myLastError = ERROR_OFFLINE;
			myRawError = -mySocket.GetLastSystemError();
			myConnectPhase = PHASE_FAILED;
			myCurrentAction = "";
			return;
		}
	}

	// Give up if it is all taking too long
	if ((myConnectPhase == PHASE_CONNECTING || myConnectPhase == PHASE_AWAIT_HANDSHAKE)
		&& TimeAfter(NowMs(), myConnectDeadline))
	{
		Log("NetNatsue: connection timed out");
		mySocket.Close();
		myLastError = ERROR_OFFLINE;
		myRawError = 0;
		myConnectPhase = PHASE_FAILED;
		myCurrentAction = "";
	}
}

void DSNetManager::ConnectInBackground(bool& block, std::vector<std::string>& usersForWWR)
{
	if (Online())
	{
		block = false;
		return;
	}

	if (myConnectPhase == PHASE_IDLE || myConnectPhase == PHASE_FAILED ||
		myConnectPhase == PHASE_ONLINE /* socket died */)
	{
		myUsersForWWR = usersForWWR;
		StartConnecting();
	}
	else
	{
		PumpConnectPhase();
	}

	block = (myConnectPhase == PHASE_CONNECTING ||
		myConnectPhase == PHASE_AWAIT_HANDSHAKE);
}

bool DSNetManager::Connect()
{
	std::vector<std::string> noUsers;
	bool block = true;
	while (true)
	{
		ConnectInBackground(block, noUsers);
		if (!block)
			break;
		SleepMs(50);
	}
	return Online();
}

void DSNetManager::Disconnect()
{
	if (mySocket.GetState() != NetNatsueSocket::STATE_CLOSED)
		Log("NetNatsue: disconnecting");
	mySocket.Close();
	myConnectPhase = PHASE_IDLE;
	myCurrentAction = "";
	myTransactions.clear();
	MarkEveryoneOffline();
}

void DSNetManager::ConnectionLost()
{
	Log("NetNatsue: connection lost (%d)", mySocket.GetLastSystemError());
	myLastError = ERROR_OFFLINE;
	myRawError = -mySocket.GetLastSystemError();
	mySocket.Close();
	myConnectPhase = PHASE_IDLE;
	myCurrentAction = "";
	myTransactions.clear();
	MarkEveryoneOffline();
}

// static
DSNetManager::Error DSNetManager::MapHandshakeError(int code)
{
	switch (code)
	{
	case 0: return ERROR_OFFLINE;			// connect failed at server end
	case 1: return ERROR_ALREADY_LOGGED_IN;	// nickname already logged on
	case 3: case 6: return ERROR_BAD_USER;	// invalid nickname/password
	case 7: case 13: case 15: return ERROR_OFFLINE;
	case 12: return ERROR_TOO_MANY_USERS;
	case 14: return ERROR_NEEDS_UPDATE;
	case 16: return ERROR_UNKNOWN;
	default: return ERROR_INTERNAL;
	}
}

// ---------------------------------------------------------------------
// Server list
// ---------------------------------------------------------------------

void DSNetManager::SetServerList(const std::vector<CBabelServerInfo>& servers)
{
	myServers = servers;
	myServerRotation = 0;
}

void DSNetManager::GetServerList(std::vector<CBabelServerInfo>& servers)
{
	servers = myServers;
}

std::string DSNetManager::GetServerHost()
{
	return myConnectedHost;
}

int DSNetManager::GetServerPort()
{
	return myConnectedPort;
}

int DSNetManager::GetServerID()
{
	return myConnectedID;
}

std::string DSNetManager::GetServerFriendlyName()
{
	return myConnectedFriendlyName;
}

// ---------------------------------------------------------------------
// Socket pump and packet despatch
// ---------------------------------------------------------------------

void DSNetManager::Pump()
{
	if (myConnectPhase == PHASE_CONNECTING || myConnectPhase == PHASE_AWAIT_HANDSHAKE)
	{
		PumpConnectPhase();
		return;
	}
	if (myConnectPhase != PHASE_ONLINE)
		return;

	bool alive = mySocket.Pump();
	// Handle everything which arrived before any disconnection
	ProcessInput();
	if (!alive && myConnectPhase == PHASE_ONLINE)
		ConnectionLost();
}

void DSNetManager::ProcessInput()
{
	while (true)
	{
		if (mySocket.InputSize() < PACKET_HEADER_SIZE)
			return;

		const char* input = mySocket.InputData();
		int inputSize = mySocket.InputSize();

		PacketHeader header;
		header.Read(input);

		if (header.furtherData < 0 || header.furtherData > MAX_FURTHER_DATA)
		{
			Log("NetNatsue: berserk further data length %d, disconnecting",
				header.furtherData);
			ConnectionLost();
			return;
		}

		// Transaction responses are recognised by ticket before
		// type.  The response type field is not reliable.  Only
		// the requestor knows how long its response is.
		int totalSize = -1;
		std::map<int, Transaction>::iterator transIt = myTransactions.end();
		if (header.ticket != 0)
			transIt = myTransactions.find(header.ticket);

		if (transIt != myTransactions.end())
		{
			totalSize = PACKET_HEADER_SIZE + transIt->second.fixedExtra
				+ header.furtherData;
		}
		else
		{
			// The server writes the type as a single byte
			int type = header.type & 0xFF;
			switch (type)
			{
			case PACKET_MESSAGE:
			case PACKET_USER_ONLINE:
			case PACKET_USER_OFFLINE:
				totalSize = PACKET_HEADER_SIZE + header.furtherData;
				break;
			case PACKET_HANDSHAKE_RESPONSE:
				// 48 bytes, then an additional-data length at +44
				if (inputSize < 48)
					return;
				{
					int additional = GetInt(input, 44);
					if (additional < 0 || additional > MAX_FURTHER_DATA)
					{
						Log("NetNatsue: berserk handshake data length %d", additional);
						ConnectionLost();
						return;
					}
					totalSize = 48 + additional;
				}
				break;
			case PACKET_CLIENT_COMMAND:
				totalSize = 36;
				break;
			case PACKET_VIRTUAL_CONNECT:
				totalSize = 44;
				break;
			case PACKET_VIRTUAL_CIRCUIT:
				totalSize = 44 + header.furtherData;
				break;
			case PACKET_ONLINE_CHANGE:
			case PACKET_VIRTUAL_CIRCUIT_CLOSE:
			case PACKET_MIGRATE:
				totalSize = PACKET_HEADER_SIZE;
				break;
			default:
				// We cannot know how long an unknown packet is, so
				// the stream is unrecoverable from here
				Log("NetNatsue: unknown packet type 0x%X, disconnecting", type);
				ConnectionLost();
				return;
			}
		}

		if (inputSize < totalSize)
			return; // whole packet not here yet

		if (transIt != myTransactions.end())
		{
			Transaction& transaction = transIt->second;
			HandleTransactionResponse(transaction, header,
				input + PACKET_HEADER_SIZE, totalSize - PACKET_HEADER_SIZE);
			if (transaction.kind == Transaction::TRANS_ABANDONED)
				myTransactions.erase(transIt); // nobody is waiting any more
		}
		else if ((header.type & 0xFF) == PACKET_HANDSHAKE_RESPONSE)
		{
			HandleHandshakeResponse(header, input, totalSize);
		}
		else
		{
			if (!HandlePacket(header, input + PACKET_HEADER_SIZE,
				totalSize - PACKET_HEADER_SIZE))
				return; // connection was dropped while handling
		}

		mySocket.ConsumeInput(totalSize);
	}
}

void DSNetManager::HandleHandshakeResponse(const PacketHeader& header,
	const char* raw, int rawSize)
{
	if (myConnectPhase != PHASE_AWAIT_HANDSHAKE)
		return; // stray, ignore

	int errorCode = (unsigned char)raw[1];
	BabelUIN serverUIN(header.fieldA, header.fieldB);
	BabelUIN userUIN(header.fieldC, header.fieldD);

	if (errorCode != 0 || serverUIN.uid == 0)
	{
		Log("NetNatsue: login refused, server error code %d", errorCode);
		myLastError = MapHandshakeError(errorCode);
		myRawError = errorCode;
		mySocket.Close();
		myConnectPhase = PHASE_FAILED;
		myCurrentAction = "";
		return;
	}

	myServerUIN = serverUIN;
	myUserUIN = userUIN;
	myConnectedAtMs = NowMs();
	myLastError = ERROR_OK;
	myRawError = 0;
	myConnectPhase = PHASE_ONLINE;
	myCurrentAction = "";
	Log("NetNatsue: logged in as %s (server %s)",
		myUserUIN.ToString().c_str(), myServerUIN.ToString().c_str());

	// The server may send an updated server list for us to
	// remember for next time
	if (rawSize >= 60)
	{
		int flagA = GetInt(raw, 48);
		int flagB = GetInt(raw, 52);
		int count = GetInt(raw, 56);
		if (flagA == 1 && flagB == 1 && count > 0 && count < 1000)
		{
			std::vector<CBabelServerInfo> servers;
			int offset = 60;
			int i;
			for (i = 0; i < count; ++i)
			{
				if (offset + 8 > rawSize)
					break;
				int port = GetInt(raw, offset);
				int id = GetInt(raw, offset + 4);
				offset += 8;
				// Two zero terminated strings: address, then name
				std::string address;
				while (offset < rawSize && raw[offset] != '\0')
					address += raw[offset++];
				++offset;
				std::string name;
				while (offset < rawSize && raw[offset] != '\0')
					name += raw[offset++];
				++offset;
				if (offset > rawSize)
					break;
				CBabelServerInfo info;
				info.SetServer(address, name, port, id);
				servers.push_back(info);
			}
			if (!servers.empty())
			{
				myServers = servers;
				myServerRotation = 0;
			}
		}
	}

	// Tell the server which users the game is watching
	SendWWRRegistrations(myUsersForWWR);
	myUsersForWWR.clear();

	// Any files waiting in the outbox can go now
	myOutboxDirty = true;
}

void DSNetManager::HandleTransactionResponse(Transaction& transaction,
	const PacketHeader& header, const char* body, int bodySize)
{
	transaction.done = true;
	transaction.success = false;

	switch (transaction.kind)
	{
	case Transaction::TRANS_CLIENT_INFO:
		// Zero further data means the user does not exist
		if (header.furtherData > 0 && bodySize >= header.furtherData)
		{
			ShortUserData userData;
			if (ParseShortUserData(body + transaction.fixedExtra,
				header.furtherData, userData))
			{
				transaction.success = true;
				transaction.resultUIN = userData.uin;
				transaction.resultString = userData.nickName;
			}
		}
		break;

	case Transaction::TRANS_CONNECTION_DETAIL:
		// Success if the response carries an address or E is set
		transaction.success =
			(header.fieldA != 0 && header.fieldB != 0) || header.fieldE != 0;
		break;

	case Transaction::TRANS_STATUS:
		if (bodySize >= 16)
		{
			transaction.success = true;
			transaction.resultInts[0] = GetInt(body, 0);	// time online
			transaction.resultInts[1] = GetInt(body, 4);	// users online
			transaction.resultInts[2] = GetInt(body, 8);	// bytes sent
			transaction.resultInts[3] = GetInt(body, 12);	// bytes received
		}
		break;

	case Transaction::TRANS_RANDOM_USER:
		if (header.fieldE == 1)
		{
			transaction.success = true;
			transaction.resultUIN = BabelUIN(header.fieldC, header.fieldD);
		}
		break;

	case Transaction::TRANS_FEED_HISTORY:
		// Any response at all confirms the server got the history
		transaction.success = true;
		break;

	default:
		break; // abandoned; discarded by the caller
	}
}

bool DSNetManager::HandlePacket(const PacketHeader& header,
	const char* body, int bodySize)
{
	int type = header.type & 0xFF;
	switch (type)
	{
	case PACKET_MESSAGE:
		{
			BabelUIN sender;
			int messageType = 0;
			std::string payload;
			if (!ParsePackedBabelMessage(body, bodySize, sender, messageType, payload))
			{
				Log("NetNatsue: dropping malformed message packet");
				break;
			}
			if (messageType == NetMessages::MESG_PRAY_FILE)
			{
				SpoolPrayFile(sender, payload);
			}
			else
			{
				IncomingMessage message;
				message.user = sender.ToString();
				message.type = messageType;
				message.payload = payload;
				myIncomingMessages.push_back(message);
			}
		}
		break;

	case PACKET_USER_ONLINE:
	case PACKET_USER_OFFLINE:
		{
			ShortUserData userData;
			if (ParseShortUserData(body, bodySize, userData))
			{
				UpdatePresence(userData.uin, type == PACKET_USER_ONLINE,
					userData.nickName);
			}
		}
		break;

	case PACKET_ONLINE_CHANGE:
		UpdatePresence(BabelUIN(header.fieldC, header.fieldD),
			header.fieldE != 0, "");
		break;

	case PACKET_VIRTUAL_CONNECT:
		{
			// Somebody (almost certainly the server, using it as a
			// delivery confirmation ping) is opening a virtual
			// circuit.  Accept it; the peer closes it afterwards.
			BabelUIN initiator(header.fieldC, header.fieldD);
			std::vector<char> accept = BuildVirtualConnectAccept(
				myServerUIN, initiator, header.fieldE, NextVSN());
			if (!mySocket.Write(&accept[0], (int)accept.size()))
			{
				ConnectionLost();
				return false;
			}
		}
		break;

	case PACKET_CLIENT_COMMAND:
		// Subcommand 0xE would answer a virtual circuit we opened,
		// but we never open any; other subcommands carried
		// server events nothing in the engine module consumes.
		break;

	case PACKET_VIRTUAL_CIRCUIT:
		// Virtual circuit data.  Nothing meaningful can
		// arrive this way, the message path is used instead
		break;

	case PACKET_VIRTUAL_CIRCUIT_CLOSE:
		// Also doubles as the server's keepalive packet
		break;

	case PACKET_MIGRATE:
		// Server migration was never finished in the original
		// infrastructure; politely ignored
		break;

	default:
		break;
	}
	return true;
}

// ---------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------

int DSNetManager::StartTransaction(int kind, int fixedExtra)
{
	int ticket = NextTicket();
	Transaction transaction;
	transaction.kind = kind;
	transaction.fixedExtra = fixedExtra;
	myTransactions[ticket] = transaction;
	return ticket;
}

void DSNetManager::AbandonTransaction(int ticket)
{
	std::map<int, Transaction>::iterator it = myTransactions.find(ticket);
	if (it == myTransactions.end())
		return;
	if (it->second.done)
	{
		myTransactions.erase(it);
		return;
	}
	// The response is still owed.  Remember its framing but let
	// the result fall on the floor when it arrives
	it->second.kind = Transaction::TRANS_ABANDONED;
}

bool DSNetManager::PollTransaction(int ticket, Transaction& result, bool& stillPending)
{
	stillPending = false;
	std::map<int, Transaction>::iterator it = myTransactions.find(ticket);
	if (it == myTransactions.end())
		return true; // connection dropped; counts as failed completion
	if (!it->second.done)
	{
		stillPending = true;
		return false;
	}
	result = it->second;
	myTransactions.erase(it);
	return true;
}

bool DSNetManager::WaitForTransaction(int ticket, int timeoutMs, Transaction& result)
{
	unsigned int deadline = NowMs() + timeoutMs;
	while (true)
	{
		Pump();

		bool stillPending = false;
		if (PollTransaction(ticket, result, stillPending))
			return result.done; // false if the connection dropped underneath us

		if (!Online())
			return false; // transactions were cleared

		if (TimeAfter(NowMs(), deadline))
		{
			AbandonTransaction(ticket);
			return false;
		}

		mySocket.WaitReadable(50);
	}
}

// ---------------------------------------------------------------------
// Presence / whose wanted register
// ---------------------------------------------------------------------

void DSNetManager::UpdatePresence(const BabelUIN& uin, bool online,
	const std::string& nickName)
{
	std::string key = uin.ToString();

	Presence& presence = myPresence[key];
	bool changed = presence.online != online;
	presence.online = online;
	if (!nickName.empty())
		presence.nickName = nickName;

	// A pending NET: WHON is waiting for exactly this update; it
	// reports the state itself, so don't also queue a change
	std::map<std::string, unsigned int>::iterator pendingIt =
		myPendingWWRAdds.find(key);
	if (pendingIt != myPendingWWRAdds.end())
	{
		myPendingWWRAdds.erase(pendingIt);
		return;
	}

	if (changed && myWWRMembers.find(key) != myWWRMembers.end())
	{
		// Queue for NextChangedUserWWR, once
		std::list<std::string>::iterator it;
		for (it = myChangedUsers.begin(); it != myChangedUsers.end(); ++it)
		{
			if (*it == key)
				return;
		}
		myChangedUsers.push_back(key);
	}
}

void DSNetManager::MarkEveryoneOffline()
{
	std::map<std::string, Presence>::iterator it;
	for (it = myPresence.begin(); it != myPresence.end(); ++it)
		it->second.online = false;
	// The engine module notifies all watching agents itself when
	// the connection goes down, so no change queue entries here
	myChangedUsers.clear();
	myPendingWWRAdds.clear();
}

void DSNetManager::SendWWRRegistrations(const std::vector<std::string>& users)
{
	std::vector<std::string>::size_type i;
	for (i = 0; i < users.size(); ++i)
	{
		BabelUIN uin;
		if (!uin.FromString(users[i]))
			continue;
		myWWRMembers.insert(users[i]);
		std::vector<char> packet = BuildWWRModify(true, myServerUIN, uin);
		if (!mySocket.Write(&packet[0], (int)packet.size()))
			return;
	}
}

bool DSNetManager::IsUserOnline(const std::string& user)
{
	BabelUIN uin;
	if (!uin.FromString(user))
		return false;
	if (!Online())
		return false;
	if (uin == myUserUIN)
		return true;

	std::map<std::string, Presence>::iterator it = myPresence.find(user);
	if (it != myPresence.end())
		return it->second.online;

	// Not a watched user.  Ask the server, waiting briefly.
	// (NET: ULIN documents itself as slow for non-WWR users.)
	myCurrentAction = "Checking whether " + user + " is online";
	int ticket = StartTransaction(Transaction::TRANS_CONNECTION_DETAIL, 0);
	std::vector<char> packet = BuildGetConnectionDetail(myServerUIN, uin, ticket);
	if (!mySocket.Write(&packet[0], (int)packet.size()))
	{
		AbandonTransaction(ticket);
		myCurrentAction = "";
		return false;
	}
	Transaction result;
	bool completed = WaitForTransaction(ticket, USER_ONLINE_TIMEOUT_MS, result);
	myCurrentAction = "";
	return completed && result.success;
}

void DSNetManager::AddToWWR(const std::string& user, bool& block)
{
	block = false;

	BabelUIN uin;
	if (!uin.FromString(user))
		return;

	myWWRMembers.insert(user);

	if (!Online())
		return; // registered with the server when we next connect

	std::map<std::string, unsigned int>::iterator pendingIt =
		myPendingWWRAdds.find(user);
	if (pendingIt == myPendingWWRAdds.end())
	{
		// First call.  If we already know their state there is
		// nothing to wait for, but still tell the server we care
		bool alreadyKnown = myPresence.find(user) != myPresence.end();

		std::vector<char> packet = BuildWWRModify(true, myServerUIN, uin);
		if (!mySocket.Write(&packet[0], (int)packet.size()))
			return;

		if (alreadyKnown)
			return;

		myPendingWWRAdds[user] = NowMs() + WWR_ADD_TIMEOUT_MS;
		block = true;
		return;
	}

	// Polling for the server to send the user details
	Pump();
	pendingIt = myPendingWWRAdds.find(user);
	if (pendingIt == myPendingWWRAdds.end())
		return; // update arrived, done

	if (TimeAfter(NowMs(), pendingIt->second))
	{
		// The server never answered, probably an unknown user.
		// Give up; they read as offline.
		myPendingWWRAdds.erase(pendingIt);
		return;
	}

	block = true;
}

void DSNetManager::RemoveFromWWR(const std::string& user)
{
	myWWRMembers.erase(user);
	myPendingWWRAdds.erase(user);
	myPresence.erase(user);

	BabelUIN uin;
	if (!uin.FromString(user))
		return;
	if (!Online())
		return;
	std::vector<char> packet = BuildWWRModify(false, myServerUIN, uin);
	mySocket.Write(&packet[0], (int)packet.size());
}

void DSNetManager::ResetIteratorWWR()
{
	// Nothing to do; NextChangedUserWWR consumes a queue
}

bool DSNetManager::NextChangedUserWWR(std::string& user, bool& online)
{
	if (myChangedUsers.empty())
		return false;
	user = myChangedUsers.front();
	myChangedUsers.pop_front();

	std::map<std::string, Presence>::iterator it = myPresence.find(user);
	online = (it != myPresence.end()) ? it->second.online : false;
	return true;
}

// ---------------------------------------------------------------------
// Server queries
// ---------------------------------------------------------------------

void DSNetManager::FetchUserData(const std::string& user, std::string& nickName,
	bool& block)
{
	block = false;
	nickName = "";

	BabelUIN uin;
	if (!uin.FromString(user))
		return;

	std::map<std::string, PendingQuery>::iterator pendingIt =
		myPendingUserFetches.find(user);
	if (pendingIt == myPendingUserFetches.end())
	{
		if (!Online())
			return;

		myCurrentAction = "Fetching details of user " + user;
		PendingQuery query;
		query.ticket = StartTransaction(Transaction::TRANS_CLIENT_INFO, 0);
		query.deadline = NowMs() + QUERY_TIMEOUT_MS;
		std::vector<char> packet = BuildGetClientInfo(myServerUIN, uin, query.ticket);
		if (!mySocket.Write(&packet[0], (int)packet.size()))
		{
			AbandonTransaction(query.ticket);
			myCurrentAction = "";
			return;
		}
		myPendingUserFetches[user] = query;
		block = true;
		return;
	}

	// Polling for the result
	Pump();
	PendingQuery query = pendingIt->second;

	Transaction result;
	bool stillPending = false;
	if (PollTransaction(query.ticket, result, stillPending))
	{
		myPendingUserFetches.erase(user);
		myCurrentAction = "";
		if (result.done && result.success)
		{
			nickName = result.resultString;
			// Remember it for anyone watching this user
			std::map<std::string, Presence>::iterator presenceIt =
				myPresence.find(user);
			if (presenceIt != myPresence.end() && !nickName.empty())
				presenceIt->second.nickName = nickName;
		}
		return;
	}

	if (TimeAfter(NowMs(), query.deadline))
	{
		AbandonTransaction(query.ticket);
		myPendingUserFetches.erase(user);
		myCurrentAction = "";
		return;
	}

	block = true;
}

std::string DSNetManager::DSFetchRandomUser(bool& block)
{
	block = false;

	if (myRandomUserQuery.ticket == 0)
	{
		if (!Online())
			return "";

		myCurrentAction = "Fetching a random online user";
		myRandomUserQuery.ticket =
			StartTransaction(Transaction::TRANS_RANDOM_USER, 0);
		myRandomUserQuery.deadline = NowMs() + QUERY_TIMEOUT_MS;
		std::vector<char> packet = BuildFetchRandomUser(myServerUIN, myUserUIN,
			myRandomUserQuery.ticket);
		if (!mySocket.Write(&packet[0], (int)packet.size()))
		{
			AbandonTransaction(myRandomUserQuery.ticket);
			myRandomUserQuery = PendingQuery();
			myCurrentAction = "";
			return "";
		}
		block = true;
		return "";
	}

	Pump();

	Transaction result;
	bool stillPending = false;
	if (PollTransaction(myRandomUserQuery.ticket, result, stillPending))
	{
		myRandomUserQuery = PendingQuery();
		myCurrentAction = "";
		if (result.done && result.success)
			return result.resultUIN.ToString();
		return "";
	}

	if (TimeAfter(NowMs(), myRandomUserQuery.deadline))
	{
		AbandonTransaction(myRandomUserQuery.ticket);
		myRandomUserQuery = PendingQuery();
		myCurrentAction = "";
		return "";
	}

	block = true;
	return "";
}

std::string DSNetManager::DSFetchRandomUser()
{
	bool block = true;
	std::string user;
	while (true)
	{
		user = DSFetchRandomUser(block);
		if (!block)
			break;
		SleepMs(50);
	}
	return user;
}

void DSNetManager::GetStatus(int& timeOnline, int& usersOnline,
	int& bytesReceived, int& bytesSent, bool& block)
{
	block = false;

	if (!Online())
	{
		timeOnline = -1;
		usersOnline = -1;
		bytesReceived = -1;
		bytesSent = -1;
		if (myStatusQuery.ticket != 0)
		{
			AbandonTransaction(myStatusQuery.ticket);
			myStatusQuery = PendingQuery();
		}
		return;
	}

	if (myStatusQuery.ticket == 0)
	{
		myCurrentAction = "Fetching connection statistics";
		// The status response is 16 bytes longer than a plain header
		myStatusQuery.ticket = StartTransaction(Transaction::TRANS_STATUS, 16);
		myStatusQuery.deadline = NowMs() + QUERY_TIMEOUT_MS;
		std::vector<char> packet = BuildGetStatus(myServerUIN, myStatusQuery.ticket);
		if (!mySocket.Write(&packet[0], (int)packet.size()))
		{
			AbandonTransaction(myStatusQuery.ticket);
			myStatusQuery = PendingQuery();
			myCurrentAction = "";
			return;
		}
		block = true;
		return;
	}

	Pump();

	Transaction result;
	bool stillPending = false;
	if (PollTransaction(myStatusQuery.ticket, result, stillPending))
	{
		myStatusQuery = PendingQuery();
		myCurrentAction = "";
		// Time online and byte counts are things this end knows
		// better than the server (Natsue does not track them)
		timeOnline = (int)(NowMs() - myConnectedAtMs);
		usersOnline = (result.done && result.success) ? result.resultInts[1] : 0;
		bytesSent = mySocket.GetBytesSent();
		bytesReceived = mySocket.GetBytesReceived();
		return;
	}

	if (TimeAfter(NowMs(), myStatusQuery.deadline))
	{
		AbandonTransaction(myStatusQuery.ticket);
		myStatusQuery = PendingQuery();
		myCurrentAction = "";
		timeOnline = (int)(NowMs() - myConnectedAtMs);
		usersOnline = 0;
		bytesSent = mySocket.GetBytesSent();
		bytesReceived = mySocket.GetBytesReceived();
		return;
	}

	block = true;
}

// ---------------------------------------------------------------------
// Messaging
// ---------------------------------------------------------------------

bool DSNetManager::PostDirectMessage(const std::string& user, char* data, int size)
{
	BabelUIN target;
	if (!target.FromString(user))
		return false;
	if (!Online() || size < 0)
		return false;

	std::vector<char> packet = BuildMessage(myServerUIN, myUserUIN, target,
		data, size);
	if (!mySocket.Write(&packet[0], (int)packet.size()))
	{
		ConnectionLost();
		return false;
	}
	return true;
}

void DSNetManager::GetMessages(std::vector<std::string>& users,
	std::vector<NetMessages::MessageType>& types,
	std::vector<std::string>& messages)
{
	Pump();

	while (!myIncomingMessages.empty())
	{
		IncomingMessage& message = myIncomingMessages.front();
		users.push_back(message.user);
		types.push_back((NetMessages::MessageType)message.type);
		messages.push_back(message.payload);
		myIncomingMessages.pop_front();
	}
}

// ---------------------------------------------------------------------
// Warp file spooling
// ---------------------------------------------------------------------

void DSNetManager::SetOutboxDirectory(const std::string& dir)
{
	myOutboxDirectory = EnsureTrailingSeparator(dir);
	myOutboxDirty = true;
}

void DSNetManager::SetInboxDirectory(const std::string& dir)
{
	myInboxDirectory = EnsureTrailingSeparator(dir);
}

std::string DSNetManager::ReturnUniqueOutboxFilename(const std::string& userID)
{
	BabelUIN target;
	if (!target.FromString(userID))
		return "";
	if (myOutboxDirectory.empty())
		return "";

	unsigned int serial = (unsigned int)time(NULL);
	while (true)
	{
		char name[64];
		sprintf(name, "%uT-%d+%d.warp", serial, target.uid, target.hid);
		std::string path = myOutboxDirectory + name;
		if (!FileExists(path))
		{
			myOutboxDirty = true;
			return path;
		}
		++serial;
	}
}

std::string DSNetManager::FindUserWhoSentFile(const std::string& file)
{
	return UserFromSpoolName(BaseName(file));
}

void DSNetManager::SpoolPrayFile(const BabelUIN& sender, const std::string& payload)
{
	if (myInboxDirectory.empty())
	{
		Log("NetNatsue: no inbox directory, dropping a message from %s",
			sender.ToString().c_str());
		return;
	}

	unsigned int serial = (unsigned int)time(NULL);
	std::string path;
	while (true)
	{
		char name[64];
		sprintf(name, "%uT-%d+%d.warp", serial, sender.uid, sender.hid);
		path = myInboxDirectory + name;
		if (!FileExists(path))
			break;
		++serial;
	}

	if (WriteWholeFileAtomic(path, payload))
	{
		Log("NetNatsue: spooled %d byte message from %s to inbox",
			(int)payload.size(), sender.ToString().c_str());
	}
	else
	{
		Log("NetNatsue: FAILED to spool a message from %s, inbox not writable?",
			sender.ToString().c_str());
	}
}

bool DSNetManager::SendOutboxFile(const std::string& path)
{
	std::string target = UserFromSpoolName(BaseName(path));
	if (target.empty())
	{
		// Not one of ours.  Rename it out of the outbox scan
		Log("NetNatsue: outbox file \"%s\" has no valid address, renaming to .bad",
			path.c_str());
		std::string badPath = path + ".bad";
		remove(badPath.c_str());
		rename(path.c_str(), badPath.c_str());
		return true;
	}

	std::string contents;
	if (!ReadWholeFile(path, contents))
		return true; // maybe still being written, try again later

	// The payload is a BinaryMessage header saying "PRAY file"
	// followed by the file itself
	std::vector<char> payload;
	PutInt(payload, 12);							// header length
	PutInt(payload, NetMessages::MESG_PRAY_FILE);	// type
	PutInt(payload, 0);								// reserved
	payload.insert(payload.end(), contents.begin(), contents.end());

	if (!PostDirectMessage(target, &payload[0], (int)payload.size()))
		return false;

	Log("NetNatsue: sent outbox file \"%s\" (%d bytes) to %s",
		BaseName(path).c_str(), (int)contents.size(), target.c_str());
	remove(path.c_str());
	return true;
}

void DSNetManager::ScanOutbox()
{
	if (myOutboxDirectory.empty())
		return;

	unsigned int now = NowMs();
	if (!myOutboxDirty && !TimeAfter(now, myNextOutboxScan))
		return;
	myOutboxDirty = false;
	myNextOutboxScan = now + OUTBOX_RESCAN_MS;

	std::vector<std::string> names;
	ListFilesWithExtension(myOutboxDirectory, ".warp", names);
	std::vector<std::string>::size_type i;
	for (i = 0; i < names.size(); ++i)
	{
		if (!Online())
			return;
		if (!SendOutboxFile(myOutboxDirectory + names[i]))
			return; // connection trouble, leave the rest for later
	}
}

void DSNetManager::ProcessQueuedMessages()
{
	if (myQueuedMessages.empty())
		return;

	// One per tick keeps any waiting bounded
	QueuedMessage* message = myQueuedMessages.front();
	myQueuedMessages.pop_front();
	message->Send(myMessageThread);
	delete message;
}

void DSNetManager::SendOrdinaryMessages()
{
	Pump();
	ProcessQueuedMessages();
	if (Online())
		ScanOutbox();
}

// ---------------------------------------------------------------------
// Asynchronous queue
// ---------------------------------------------------------------------

void DSNetManager::QueueMessage(QueuedMessage* message)
{
	myQueuedMessages.push_back(message);
}

bool DSNetManager::DSFeedHistory(char* data, int size)
{
	if (!Online() || !data || size <= 0)
		return false;

	myCurrentAction = "Uploading creature history";
	int ticket = StartTransaction(Transaction::TRANS_FEED_HISTORY, 0);
	std::vector<char> packet = BuildFeedHistory(myServerUIN, myUserUIN, ticket,
		data, size);
	if (!mySocket.Write(&packet[0], (int)packet.size()))
	{
		AbandonTransaction(ticket);
		myCurrentAction = "";
		ConnectionLost();
		return false;
	}

	Transaction result;
	bool completed = WaitForTransaction(ticket, HISTORY_TIMEOUT_MS, result);
	myCurrentAction = "";
	return completed && result.success;
}
