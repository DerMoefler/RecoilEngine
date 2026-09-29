#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <vector>
#include <unordered_map>
#include <optional>

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
	int teamId_;
	bool comInUse_ = true;

    // Engine Unit ID -> eigene fortlaufende Unit ID
    std::unordered_map<int, int> engineToCustomUnitId_;

    // Eigene Unit ID -> Engine Unit ID
    std::unordered_map<int, int> customToEngineUnitId_;

    int nextCustomUnitId_ = 0;

    int registerUnit(int engineUnitId);
    std::optional<int> getCustomUnitId(int engineUnitId) const;
    std::optional<int> getEngineUnitId(int customUnitId) const;
};

#endif // UNBARABLEAI_H