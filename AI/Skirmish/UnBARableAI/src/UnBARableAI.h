#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "UnBARableAI/bar_shared_memory.h"
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

    /// \brief Type alias for BarSharedMemory.
    using BarSharedMemory = UnBARableAINS::memory::BarSharedMemory;

    /// \brief Type alias for EngineStatus.
    using EngineStatus = UnBARableAINS::EngineStatus;

  private:
    /// \brief Shm name consists of this base name + the process id.
    static constexpr std::string_view c_shm_base_name = "/sm_proc";

    /**
     * \brief Returns the name for the shared-memory constructed from the \ref
     * c_shm_base_name and the process id.
     */
    static std::string getShmName(void);

    /**
     * \brief Helper method updating the status and logging an optional reason.
     * \param status New EngineStatus.
     * \param reason Optional reason, otherwise logs "Not specified".
     *
     * Catches errors while updating the engine status and logs them.
     */
    void updateEngineStatus(
        const EngineStatus status,
        const std::optional<std::string_view> reason = std::nullopt);

    /**
     * \brief Implementation for the topic "EVENT_INIT".
     * \param data Data passed to \ref HandleEvent.
     *
     * Calls \ref loadConfiguration and sets the EngineStatus to running.
     */
    void eventInit(const void *data);

    /**
     * \brief Implementation for the topic "EVENT_UNIT_CREATED".
     * \param data Data passed to \ref HandleEvent.
     *
     * Simply registers a unit using \ref registerUnit.
     */
    void eventUnitCreated(const void *data);

    /**
     * \brief Implementation for the topic "EVENT_UNIT_DESTROYED".
     * \param data Data passed to \ref HandleEvent.
     *
     * Removes unit mapping if the destroyed unit was registered.
     */
    void eventUnitDestroyed(const void *data);

    /**
     * \brief Implementation for the topic "EVENT_UNIT_DESTROYED".
     * \param data Data passed to \ref HandleEvent.
     *
     * This method currently only handles messages starting with the prefix
     * "UNBARABLE_GAME_OVER". Such a message must contain either ";result=win"
     * or ";result=loss". It finishes the game/episode appropriately by calling
     * \ref finishEpisode.
     */
    void eventLuaMessage(const void *data);

    /**
     * \brief Implementation for the topic "EVENT_RELEASE".
     * \param data Data passed to \ref HandleEvent.
     *
     * EVENT_RELEASE is issued when the AI is to clean itself up. If the game is
     * not yet over, it finishes the episode with a call to \ref finishEpisode
     * with the status EngineStatus::TEAM_DIED. In either case it then clears
     * its mapping from engine to custom id and vice versa.
     */
    void eventRelease(const void *data);

    void eventUpdate(const void *data);

    /**
     * \brief Finishes an episode by writing one last observation and
     * waiting for a HandleEventUpdate.
     * \param status New status of the engine, written to shared-memory.
     * \param reason Reason to print for why/how the episode finished.
     *
     * Writes the last observation and then sets gameOver_. Waits 30s for
     * the HandleEventUpdate response. The method ignores calls to it after
     * gameOver_ has been set already.
     */
    void finishEpisode(const UnBARableAINS::EngineStatus status,
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
    uint32_t registerUnit(int engineUnitId);

    /**
     * \brief Get the custom unit id from an engine id.
     * \param engineUnitId Id assigned to the unit by the engine.
     * \returns Custom id assigned by the shared-memory, std::nullopt if not
     * registered.
     */
    std::optional<uint32_t> getCustomUnitId(int engineUnitId) const;

    /**
     * \brief Get the engine unit id from a custom id.
     * \param customUnitId Id assigend by the shared-memory.
     * \returns Engine unit id, std::nullopt if not registered.
     */
    std::optional<int> getEngineUnitId(uint32_t customUnitId) const;

    void loadConfiguration(void);

    springai::OOAICallback *callback_;
    int teamId_;
    bool comInUse_ = true;
    BarSharedMemory shm_;

    // Engine Unit ID -> eigene fortlaufende Unit ID
    std::unordered_map<int, uint32_t> engineToCustomUnitId_;

    // Eigene Unit ID -> Engine Unit ID
    std::unordered_map<uint32_t, int> customToEngineUnitId_;

    bool gameOver_ = false;
    uint32_t nextCustomUnitId_ = 0;
    int currentFrame_ = -1;
    int updateIntervalFrames_ = 2;
    int firstUpdateFrame_ = 2;
    float moveStepSize_ = 100.0f;
};

#endif // UNBARABLEAI_H
