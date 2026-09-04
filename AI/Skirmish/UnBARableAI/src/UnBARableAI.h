#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <vector>

class UnBARableAI {
public:

	/**
	 * \brief Constructor
	 * \param callback The callback interface provided by the game engine.
	 */
	UnBARableAI(springai::OOAICallback* callback);
	~UnBARableAI();


	/**
	 * \brief Handles events from the game engine.
	 * \param topic type of the event 
	 * \param data pointer to the event data
	 * 
	 * \ref RecoilEngine/rts/ExternalAI/Interface/AISEvents.h defines the event topics and their corresponding data structures.
	 */
	void HandleEvent(int topic, const void* data);

private:
	void writeObservationToSharedMemory(springai::Unit* unit);

	springai::OOAICallback* callback_;
	std::vector<int> myUnits_;
	int teamId_;
	bool comInUse_ = true;
};

#endif // UNBARABLEAI_H