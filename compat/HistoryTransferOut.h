// HistoryTransferOut.h
//
// Packs creature history and life events into the blob format the
// Babel server's history feed expects (see the "Creature History
// Blob" chapter of the c3ds-projects NetBabel documentation).
//
// This is a fresh NetNatsue implementation of the engine's
// HistoryTransferOut class, whose original source is not present
// in all Docking Station source dumps.  Install this file and its
// .cpp at c2e/server/HistoryFeed/; the engine's netbabel module
// includes it from there.
//
// Blob layout (little-endian ints, strings are int length + bytes):
//   string moniker
//   byte   hasCreatureState
//   if hasCreatureState: int sex, genus, variant,
//                        pointMutations, crossoverPoints
//   int    eventCount
//   events[eventCount]:
//     int eventType, worldTime, ageTicks, unixTime, lifeStage
//     string moniker1, moniker2, worldName, worldID, userID
//     int solidIndex
//   string name
//   int    userTextCount
//   userTexts[userTextCount]: string userText, int solidIndex

#ifndef HISTORY_TRANSFER_OUT_H
#define HISTORY_TRANSFER_OUT_H

#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include <string>
#include <vector>

#include "../../engine/Creature/History/HistoryStore.h"

class HistoryTransferOut
{
public:
	HistoryTransferOut();

	// Tell us the moniker (always required, and first)
	void WriteMoniker(const std::string& moniker);
	// Call this to send the header of a creature
	void WriteCreatureHistory(const CreatureHistory& history);
	// Call this to send each new life event
	void WriteLifeEvent(const LifeEvent& event, int solidIndex);
	// Call this if their name has changed
	void WriteName(const std::string& name);
	// Call this for each life event where user text has changed
	void WriteUserText(int solidIndex, const std::string& userText);

	// Serialise and return the blob.  NULL/0 if no moniker was given.
	const void* GetAddress();
	int GetLength();

private:
	void Serialise();
	void PutInt(int value);
	void PutByte(unsigned char value);
	void PutString(const std::string& str);

	std::string myMoniker;

	bool myHaveCreatureState;
	int myGender;
	int myGenus;
	int myVariant;
	int myPointMutations;
	int myCrossoverPoints;

	std::vector<LifeEvent> myLifeEventsToSend;
	std::vector<int> mySolidIndices;

	std::string myName;

	std::vector<std::string> myUserTexts;
	std::vector<int> myUserTextIndices;

	bool mySerialised;
	std::string myBuffer;
};

#endif // HISTORY_TRANSFER_OUT_H
