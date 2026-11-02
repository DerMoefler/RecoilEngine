#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "UnBARableAI/engine_status.h"
#include "UnBARableAI/unit_data.h"

class UnBARableAI {
  public:
    /**
     * \brief Constructor
     * \param callback The callback interface provided by the game engine.
     *
     * Sets own team id and initializes built-in random number generator
     * (srand). Also calls \ref initShmName.
     */
    UnBARableAI(springai::OOAICallback *callback);

    /// \brief Empty destructor.
    ~UnBARableAI(void);

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
    /// \brief Shm name consists of this base name + the process id.
    static constexpr std::string_view c_shm_base_name = "/sm_proc";

    /**
     * \brief Initializes \ref shmName_ using \ref c_shm_base_name and the
     * process id.
     */
    void initShmName(void);

    /**
     * \brief Implementation for the topic "EVENT_INIT".
     * \param data Data passed to \ref HandleEvent.
     *
     * Calls \ref loadConfiguration.
     */
    void eventInit(const void *data);

    /**
     * \brief Implementation for the topic "EVENT_UNIT_CREATED".
     * \param data Data passed to \ref HandleEvent.
     *
     * Simply registers a unit using \ref registerUnit.
     */
    void eventUnitCreated(const void *data);

    void finishEpisode(UnBARableAINS::EngineStatus status,
                       const std::string &reason);
    void writeObservationToSharedMemory();
    void writeUnitToSharedMemory(springai::Unit *unit);

    UnitData getUnitData(springai::Unit *unit);

    /// \brief Hexdumps the \ref c_shm_name to a logfile (see \ref
    /// getSharedMemoryStateFilename).
    std::filesystem::path writeSharedMemoryState(void) const;

    /// \brief Get (and create missing directories) for a file to write
    /// SharedMemory errorstate to.
    static std::filesystem::path getSharedMemoryStateFilename(void);

    /**
     * \brief Registers a unit if not present, otherwise returns already
     * registered id.
     * \param engineUnitId Unit assigned by the engine.
     * \returns Mapped id.
     */
    int registerUnit(int engineUnitId);
    int getCustomUnitId(int engineUnitId) const;
    int getEngineUnitId(int customUnitId) const;

    void loadConfiguration(void);

    springai::OOAICallback *callback_;
    int teamId_;
    bool comInUse_ = true;
    std::string shmName_;

    // Engine Unit ID -> eigene fortlaufende Unit ID
    std::unordered_map<int, int> engineToCustomUnitId_;

    // Eigene Unit ID -> Engine Unit ID
    std::unordered_map<int, int> customToEngineUnitId_;

    bool gameOver_ = false;
    int nextCustomUnitId_ = 0;
    int currentFrame_ = -1;
    int updateIntervalFrames_ = 2;
    int firstUpdateFrame_ = 2;
    float moveStepSize_ = 100.0f;
};

#endif // UNBARABLEAI_H
