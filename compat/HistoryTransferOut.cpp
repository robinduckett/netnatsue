#ifdef _MSC_VER
#pragma warning(disable:4786 4503)
#endif

#include "HistoryTransferOut.h"

HistoryTransferOut::HistoryTransferOut()
	: myHaveCreatureState(false), myGender(0), myGenus(0), myVariant(0),
	  myPointMutations(0), myCrossoverPoints(0), mySerialised(false)
{
}

void HistoryTransferOut::WriteMoniker(const std::string& moniker)
{
	myMoniker = moniker;
}

void HistoryTransferOut::WriteCreatureHistory(const CreatureHistory& history)
{
	myHaveCreatureState = true;
	myGender = history.myGender;
	myGenus = history.myGenus;
	myVariant = history.myVariant;
	myPointMutations = history.myCrossoverMutationCount;
	myCrossoverPoints = history.myCrossoverCrossCount;
}

void HistoryTransferOut::WriteLifeEvent(const LifeEvent& event, int solidIndex)
{
	myLifeEventsToSend.push_back(event);
	mySolidIndices.push_back(solidIndex);
}

void HistoryTransferOut::WriteName(const std::string& name)
{
	myName = name;
}

void HistoryTransferOut::WriteUserText(int solidIndex, const std::string& userText)
{
	myUserTexts.push_back(userText);
	myUserTextIndices.push_back(solidIndex);
}

// Explicitly little-endian on every platform.  Raw struct copies
// would break on 64 bit and big-endian builds
void HistoryTransferOut::PutInt(int value)
{
	unsigned int v = (unsigned int)value;
	myBuffer += (char)(v & 0xFF);
	myBuffer += (char)((v >> 8) & 0xFF);
	myBuffer += (char)((v >> 16) & 0xFF);
	myBuffer += (char)((v >> 24) & 0xFF);
}

void HistoryTransferOut::PutByte(unsigned char value)
{
	myBuffer += (char)value;
}

void HistoryTransferOut::PutString(const std::string& str)
{
	PutInt(str.size());
	myBuffer += str;
}

void HistoryTransferOut::Serialise()
{
	if (mySerialised)
		return;
	mySerialised = true;
	myBuffer.erase();

	PutString(myMoniker);

	PutByte(myHaveCreatureState ? 1 : 0);
	if (myHaveCreatureState)
	{
		PutInt(myGender);
		PutInt(myGenus);
		PutInt(myVariant);
		PutInt(myPointMutations);
		PutInt(myCrossoverPoints);
	}

	PutInt(myLifeEventsToSend.size());
	{
		for (std::vector<LifeEvent>::size_type i = 0; i < myLifeEventsToSend.size(); ++i)
		{
			const LifeEvent& event = myLifeEventsToSend[i];
			PutInt((int)event.myEventType);
			PutInt((int)event.myWorldTick);
			PutInt((int)event.myAgeInTicks);
			PutInt((int)event.myRealWorldTime);
			PutInt(event.myLifeStage);
			PutString(event.myRelatedMoniker1);
			PutString(event.myRelatedMoniker2);
			// No photo; photos travel separately as PRAY chunks
			PutString(event.myWorldName);
			PutString(event.myWorldUniqueIdentifier);
			PutString(event.myNetworkUser);
			PutInt(mySolidIndices[i]);
		}
	}

	PutString(myName);

	PutInt(myUserTexts.size());
	{
		for (std::vector<std::string>::size_type i = 0; i < myUserTexts.size(); ++i)
		{
			PutString(myUserTexts[i]);
			PutInt(myUserTextIndices[i]);
		}
	}
}

const void* HistoryTransferOut::GetAddress()
{
	// The moniker must be set with WriteMoniker first
	if (myMoniker.empty())
		return NULL;
	Serialise();
	return myBuffer.data();
}

int HistoryTransferOut::GetLength()
{
	if (myMoniker.empty())
		return 0;
	Serialise();
	return myBuffer.size();
}
