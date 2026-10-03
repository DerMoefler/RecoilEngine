#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <optional>
#include <unordered_map>
#include <vector>

#include "UnBARableAI/unit_data.h"

class UnBARableAI {
  public:
    /**
     * \brief Constructor
     * \param callback The callback interface provided by the game engine.
     */
    UnBARableAI(springai::OOAICallback *callback);
    ~UnBARableAI();

    /**
     * \brief Handles events from the game engine.
     * \param topic type of the event
     * \param data pointer to the event data
     *
     * \ref RecoilEngine/rts/ExternalAI/Interface/AISEvents.h defines the event
     * topics and their corresponding data structures.
     */
    void HandleEvent(int topic, const void *data);

    /// \brief Type Alias for UnitData.
    using UnitData = UnBARableAINS::unit::UnitData;

  private:
    void writeObservationToSharedMemory(springai::Unit *unit);

    UnitData getUnitData(springai::Unit *unit);

    /**
     * \brief Registers a unit if not present, otherwise returns already
     * registered id.
     * \param engineUnitId Unit assigned by the engine.
     * \returns Mapped id.
     */
    int registerUnit(int engineUnitId);
    int getCustomUnitId(int engineUnitId) const;
    int getEngineUnitId(int customUnitId) const;

    springai::OOAICallback *callback_;
    int teamId_;
    bool comInUse_ = true;

    // Engine Unit ID -> eigene fortlaufende Unit ID
    std::unordered_map<int, int> engineToCustomUnitId_;

    // Eigene Unit ID -> Engine Unit ID
    std::unordered_map<int, int> customToEngineUnitId_;

    int nextCustomUnitId_ = 0;
};

#endif // UNBARABLEAI_H
