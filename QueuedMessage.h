// QueuedMessage.h
//
// Base class for messages queued for asynchronous sending, such
// as creature history uploads (see HistoryQueuedMessage in
// modules/netbabel/NetworkImplementation.cpp).  The manager owns
// queued messages and deletes them after calling Send.
//
// Recreated for NetNatsue.

#ifndef QUEUED_MESSAGE_H
#define QUEUED_MESSAGE_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>

class MessageThread;

class QueuedMessage
{
public:
	QueuedMessage() {}
	virtual ~QueuedMessage() {}

	// Perform the actual send.  Called by the manager from its
	// per-tick pump when the message reaches the head of the queue.
	virtual void Send(const MessageThread& messageThread) const = 0;

	void SetDebugText(const std::string& text) { myDebugText = text; }
	std::string GetDebugText() const { return myDebugText; }

private:
	std::string myDebugText;
};

#endif // QUEUED_MESSAGE_H
