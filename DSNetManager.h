// DSNetManager.h
//
// The Docking Station network manager.  The engine's netbabel
// module drives this class to provide the NET: family of CAOS
// commands and the Warp.
//
// This is NetNatsue's recreation of the lost Babel client
// library's DSNetManager, implementing the NetBabel protocol as
// reverse engineered by the Natsue project
// (https://github.com/20kdc/c3ds-projects).  It connects
// to Natsue servers such as the one at eemfoo.org.
//
// Differences from the original library:
//
// * Single threaded.  The original ran Babel on worker threads;
//   here everything happens on the caller's thread.  Commands
//   which used to block a worker use the engine's block-and-poll
//   pattern (the bool& block out parameters), driven by the
//   per-tick pump in SendOrdinaryMessages.  Only DSFeedHistory
//   performs a bounded synchronous wait, because its caller
//   (QueuedMessage::Send) requires a confirmed result.
//
// * NET: WRIT messages are sent down the ordinary message path
//   rather than over virtual circuits.  The original virtual
//   circuit transport could freeze the game; Natsue explicitly
//   supports routing writs sent as ordinary messages.
//
// * Incoming virtual circuit connects are acknowledged and then
//   ignored.  Natsue uses such circuits as delivery confirmation
//   pings; answering them is what makes creature and mail
//   transfer reliable.

#ifndef DS_NET_MANAGER_H
#define DS_NET_MANAGER_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>

#include "CBabelServerInfo.h"
#include "NetMessages.h"
#include "MessageThread.h"
#include "NetNatsueProtocol.h"
#include "NetNatsueSocket.h"

class NetLogInterface;
class QueuedMessage;

class DSNetManager
{
public:
	// Error codes as documented for NET: ERRA
	enum Error
	{
		ERROR_UNKNOWN = 0,
		ERROR_OK = 1,				// connection OK
		ERROR_OFFLINE = 2,			// you or the server are offline
		ERROR_BAD_USER = 3,			// invalid user name/password
		ERROR_ALREADY_LOGGED_IN = 4,
		ERROR_TOO_MANY_USERS = 5,
		ERROR_INTERNAL = 6,
		ERROR_NEEDS_UPDATE = 7,		// new client version required
	};

	DSNetManager();
	DSNetManager(NetLogInterface* logger);
	~DSNetManager();

	// Overrides all other server configuration.  Set from the
	// "Override Server" / "Override Port" entries in server.cfg
	static void OverrideHost(const std::string& host, int port);

	// Handshake identity, set from the "Client Mode" entry in
	// server.cfg.  Modern (the default) claims Natsue's
	// not-actually-Babel extension, so the server drops its
	// vanilla-bug workarounds and contacts can be added live;
	// Original presents as a stock Babel client.
	enum ClientMode
	{
		CLIENT_MODE_MODERN,
		CLIENT_MODE_ORIGINAL,
	};
	static void SetClientMode(ClientMode mode);

	// --- session -----------------------------------------------------

	void SetUser(const std::string& nickname, const std::string& password = "");
	bool UserSet();
	// The user's own id as told to us by the server ("uid+hid"),
	// or an empty string before the first successful login
	std::string GetUser();

	// Connect and log in, blocking until done.  Returns success.
	bool Connect();
	// Connect and log in over several calls.  While the connection
	// is still in progress block is set true and the caller should
	// call again next tick.  usersForWWR are registered with the
	// server once the connection is up.
	void ConnectInBackground(bool& block, std::vector<std::string>& usersForWWR);
	void Disconnect();
	bool Online();

	Error GetLastError();
	// Raw diagnostic code: the server's handshake response code,
	// or a negative OS socket error
	int RawGetLastError();
	std::string DebugGetCurrentAction();

	// --- server list -------------------------------------------------

	void SetServerList(const std::vector<CBabelServerInfo>& servers);
	void GetServerList(std::vector<CBabelServerInfo>& servers);
	std::string GetServerHost();
	int GetServerPort();
	int GetServerID();
	std::string GetServerFriendlyName();

	// --- warp file spooling ------------------------------------------

	void SetOutboxDirectory(const std::string& dir);
	void SetInboxDirectory(const std::string& dir);
	// Full path of a fresh file in the outbox addressed to the
	// given user, or an empty string if the user id is invalid.
	// The target is encoded in the filename: <serial>T-<uid>+<hid>.warp
	std::string ReturnUniqueOutboxFilename(const std::string& userID);
	// Given the full path of a spooled inbox file, returns the
	// user id of its sender (decoded from the filename)
	std::string FindUserWhoSentFile(const std::string& file);
	// The per-tick pump: moves socket data, despatches incoming
	// packets, sends outbox files and queued messages
	void SendOrdinaryMessages();

	// --- presence / whose wanted register ----------------------------

	bool IsUserOnline(const std::string& user);
	void AddToWWR(const std::string& user, bool& block);
	void RemoveFromWWR(const std::string& user);
	void ResetIteratorWWR();
	bool NextChangedUserWWR(std::string& user, bool& online);

	// --- server queries ----------------------------------------------

	void FetchUserData(const std::string& user, std::string& nickName, bool& block);
	std::string DSFetchRandomUser();
	std::string DSFetchRandomUser(bool& block);
	void GetStatus(int& timeOnline, int& usersOnline,
		int& bytesReceived, int& bytesSent, bool& block);

	// --- messaging ---------------------------------------------------

	// Send a message payload (starting with a BinaryMessage
	// header) directly to a user.  Used for NET: WRIT.
	bool PostDirectMessage(const std::string& user, char* data, int size);
	// Collect received non-PRAY messages.  Each entry gives the
	// sending user, the message type and the payload following
	// the BinaryMessage header.
	void GetMessages(std::vector<std::string>& users,
		std::vector<NetMessages::MessageType>& types,
		std::vector<std::string>& messages);

	// --- asynchronous queue ------------------------------------------

	// Takes ownership; Send is called from the per-tick pump
	void QueueMessage(QueuedMessage* message);
	// Upload a creature history blob and wait (bounded) for the
	// server to confirm it arrived.  Returns success.
	bool DSFeedHistory(char* data, int size);

private:
	// not copyable
	DSNetManager(const DSNetManager&);
	DSNetManager& operator=(const DSNetManager&);

	// A transaction we are waiting on.  Kept until its response
	// arrives even after the waiter gives up, because the response
	// length on the wire is only known to the request's sender.
	struct Transaction
	{
		enum Kind
		{
			TRANS_CLIENT_INFO,
			TRANS_CONNECTION_DETAIL,
			TRANS_STATUS,
			TRANS_RANDOM_USER,
			TRANS_FEED_HISTORY,
			TRANS_ABANDONED,
		};

		int kind;
		int fixedExtra;			// response bytes after the header, before furtherData
		bool done;
		bool success;
		// results, meaning depends on kind
		NetNatsueProtocol::BabelUIN resultUIN;
		std::string resultString;
		int resultInts[4];

		Transaction()
			: kind(TRANS_ABANDONED), fixedExtra(0), done(false), success(false)
		{
			resultInts[0] = resultInts[1] = resultInts[2] = resultInts[3] = 0;
		}
	};

	enum ConnectPhase
	{
		PHASE_IDLE,
		PHASE_CONNECTING,
		PHASE_AWAIT_HANDSHAKE,
		PHASE_ONLINE,
		PHASE_FAILED,
	};

	struct Presence
	{
		bool online;
		std::string nickName;
		Presence() : online(false) {}
	};

	struct IncomingMessage
	{
		std::string user;
		int type;
		std::string payload;
	};

	// A blocking CAOS command's outstanding server query
	struct PendingQuery
	{
		int ticket;					// 0 when idle
		unsigned int deadline;		// give up after this time
		PendingQuery() : ticket(0), deadline(0) {}
	};

	void Construct(NetLogInterface* logger);
	void Log(const char* format, ...);

	int NextTicket();
	int NextVSN();

	// Socket pump and packet despatch
	void Pump();
	void ProcessInput();
	bool HandlePacket(const NetNatsueProtocol::PacketHeader& header,
		const char* body, int bodySize);
	void HandleTransactionResponse(Transaction& transaction,
		const NetNatsueProtocol::PacketHeader& header,
		const char* body, int bodySize);
	void HandleHandshakeResponse(const NetNatsueProtocol::PacketHeader& header,
		const char* raw, int rawSize);
	void ConnectionLost();

	int StartTransaction(int kind, int fixedExtra);
	// Pump until the transaction completes or timeoutMs passes.
	// Returns true and removes the transaction if it completed,
	// filling result; abandons it otherwise.
	bool WaitForTransaction(int ticket, int timeoutMs, Transaction& result);
	// Non-blocking: if done, fills result, removes and returns
	// true.  Returns false while pending; abandons on timeout.
	bool PollTransaction(int ticket, Transaction& result, bool& stillPending);
	void AbandonTransaction(int ticket);

	static Error MapHandshakeError(int code);

	void UpdatePresence(const NetNatsueProtocol::BabelUIN& uin, bool online,
		const std::string& nickName);
	void MarkEveryoneOffline();

	void SendWWRRegistrations(const std::vector<std::string>& users);
	void ProcessQueuedMessages();
	void ScanOutbox();
	bool SendOutboxFile(const std::string& path);
	void SpoolPrayFile(const NetNatsueProtocol::BabelUIN& sender,
		const std::string& payload);

	void StartConnecting();
	void PumpConnectPhase();

	// --- static configuration ---
	static std::string ourOverrideHost;
	static int ourOverridePort;
	static ClientMode ourClientMode;

	// --- state ---
	NetLogInterface* myLogger;
	MessageThread myMessageThread;

	NetNatsueSocket mySocket;
	ConnectPhase myConnectPhase;
	unsigned int myConnectDeadline;
	int myServerRotation;			// which server list entry to try next

	std::string myNickname;
	std::string myPassword;
	NetNatsueProtocol::BabelUIN myUserUIN;
	NetNatsueProtocol::BabelUIN myServerUIN;
	unsigned int myConnectedAtMs;

	Error myLastError;
	int myRawError;
	std::string myCurrentAction;

	std::vector<CBabelServerInfo> myServers;
	std::string myConnectedHost;
	int myConnectedPort;
	int myConnectedID;
	std::string myConnectedFriendlyName;

	int myNextTicketNumber;
	int myNextVSN;
	std::map<int, Transaction> myTransactions;

	// presence, keyed by "uid+hid"
	std::map<std::string, Presence> myPresence;
	std::set<std::string> myWWRMembers;		// users the game is watching
	std::list<std::string> myChangedUsers;
	std::map<std::string, unsigned int> myPendingWWRAdds; // user -> deadline
	std::vector<std::string> myUsersForWWR;	// to register once connected

	// pending queries
	std::map<std::string, PendingQuery> myPendingUserFetches;
	PendingQuery myRandomUserQuery;
	PendingQuery myStatusQuery;

	std::list<IncomingMessage> myIncomingMessages;
	std::list<QueuedMessage*> myQueuedMessages;

	std::string myOutboxDirectory;
	std::string myInboxDirectory;
	bool myOutboxDirty;
	unsigned int myNextOutboxScan;
};

#endif // DS_NET_MANAGER_H
