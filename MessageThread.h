// MessageThread.h
//
// In the original Babel library this was the worker thread which
// processed queued outgoing messages.  NetNatsue is deliberately
// single threaded; queued messages are sent from the per-tick
// pump instead, so this survives only as the context object
// passed to QueuedMessage::Send.
//
// Recreated for NetNatsue.

#ifndef MESSAGE_THREAD_H
#define MESSAGE_THREAD_H

class DSNetManager;

class MessageThread
{
public:
	MessageThread() : myNetManager(0) {}
	explicit MessageThread(DSNetManager* netManager) : myNetManager(netManager) {}

	DSNetManager* GetNetManager() const { return myNetManager; }

private:
	DSNetManager* myNetManager;
};

#endif // MESSAGE_THREAD_H
