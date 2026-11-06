#include "UnBARableAI.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream> // TODO remove
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unistd.h>

#include "Economy.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Map.h"
#include "Resource.h"
#include "SkirmishAI.h"
#include "UnBARableAI/UnBARableAIClient.h"
#include "UnBARableAI/debug/dump_action.hpp"
#include "UnBARableAI/debug/dump_unit_data.hpp"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "memory/debug/hexdump.hpp"
#include "utility/debug/print_helpers.hpp"

namespace {
auto logger = spdlog::stdout_color_mt("UnBARableAI");
struct ConfigureLogger {
    ConfigureLogger() {
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n::%!] %v");
    }
};
ConfigureLogger configureLogger;
} // namespace

UnBARableAI::UnBARableAI(springai::OOAICallback *callback)
    : callback_(callback), shm_(openShm()) {
    teamId_ = callback_->GetSkirmishAIId();
    srand(time(NULL));
}

UnBARableAI::~UnBARableAI(void) {
    // Cleanup if needed
}

void UnBARableAI::HandleEvent(int topic, const void *data) {
    switch (topic) {
    case EVENT_INIT: {
        eventInit(data);
        break;
    }
    case EVENT_UNIT_CREATED: {
        eventUnitCreated(data);
        break;
    }
    case EVENT_UNIT_DESTROYED: {
        eventUnitDestroyed(data);
        break;
    }
    case EVENT_LUA_MESSAGE: {
        eventLuaMessage(data);
        break;
    }
    case EVENT_RELEASE: {
        eventRelease(data);
        break;
    }
    case EVENT_UPDATE: {
        eventUpdate(data);
        break;
    }
    default:
        break;
    }
}

std::string UnBARableAI::getShmName(void) {
    const char *const value = std::getenv("UNBARABLEAI_SHM_NAME");

    if (value == nullptr || *value == '\0') {
        const std::string message =
            "UNBARABLEAI_SHM_NAME environment variable is not set,";
        SPDLOG_LOGGER_ERROR(logger, message);
        throw std::runtime_error(message);
    }

    return std::string{value};
}

auto UnBARableAI::openShm(void) -> BarSharedMemory {
    const auto shmName = getShmName();
    try {
        auto shm = BarSharedMemory::open(shmName);
        SPDLOG_LOGGER_INFO(logger, "Opened SharedMemory \"{}\"", shmName);
        return shm;
    } catch (const std::exception &e) {
        const std::string message =
            std::format("Failed opening SharedMemory \"{}\". Reason: {}",
                        shmName, e.what());
        SPDLOG_LOGGER_ERROR(logger, message);
        throw std::runtime_error(message);
    }
}

void UnBARableAI::updateEngineStatus(
    const EngineStatus status, const std::optional<std::string_view> reason) {
    const std::string_view actualReason = reason.value_or("Not specified");
    try {
        shm_.updateEngineStatus(status);
        SPDLOG_LOGGER_INFO(logger,
                           "Engine status updated: "
                           "frame={}, status={}, reason=\"{}\"",
                           currentFrame_, static_cast<int>(status),
                           actualReason);
    } catch (const std::exception &exception) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Failed to update to engine status: "
                            "frame={}, status={}, reason=\"{}\", error={}",
                            currentFrame_, static_cast<int>(status),
                            actualReason, exception.what());
        return;
    } catch (...) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Failed to update to engine status: "
                            "frame={}, status={}, reason=\"{}\", unknown error",
                            currentFrame_, static_cast<int>(status),
                            actualReason);
        return;
    }
}

void UnBARableAI::eventInit(const void *data) {
    loadConfiguration();

    SPDLOG_LOGGER_INFO(logger,
                       "Received INIT event: team_id={}, shared_memory=\"{}\"",
                       teamId_, shm_.getName());
    updateEngineStatus(EngineStatus::RUNNING);
}

void UnBARableAI::eventUnitCreated(const void *data) {
    const SUnitCreatedEvent *event =
        static_cast<const SUnitCreatedEvent *>(data);
    const int engineUnitId = event->unit;
    const auto customUnitIdOpt = registerUnit(engineUnitId);
    if (!customUnitIdOpt.has_value()) {
        SPDLOG_LOGGER_ERROR(logger, "EVENT_UNIT_CREATED failed");
        return;
    }

    const auto customUnitId = customUnitIdOpt.value();

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, engineUnitId);

    springai::UnitDef *unitDef = unit->GetDef();

    SPDLOG_LOGGER_INFO(logger,
                       "Unit created, name: {}, custom ID: {}, engine ID: {}",
                       (unitDef != nullptr ? unitDef->GetName() : "unknown"),
                       customUnitId, engineUnitId);
}

void UnBARableAI::eventUnitDestroyed(const void *data) {
    const SUnitDestroyedEvent *event =
        static_cast<const SUnitDestroyedEvent *>(data);

    const int engineUnitId = event->unit;
    const auto customUnitIdOpt = getCustomUnitId(engineUnitId);
    const std::string customUnitIdDisplayName =
        customUnitIdOpt.has_value() ? std::to_string(customUnitIdOpt.value())
                                    : "Unregistered";

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, engineUnitId);

    if (unit && unit->GetDef()) {
        SPDLOG_LOGGER_INFO(logger,
                           "Unit destroyed: name=\"{}\", engine_unit_id={}, "
                           "custom_unit_id={}, attacker_engine_id={}",
                           unit->GetDef()->GetName(), engineUnitId,
                           customUnitIdDisplayName, event->attacker);
    }

    // Remove mapping if unit was registered
    if (customUnitIdOpt.has_value()) {
        engineToCustomUnitId_.erase(engineUnitId);
        customToEngineUnitId_.erase(customUnitIdOpt.value());

        SPDLOG_LOGGER_DEBUG(
            logger, "Removed unit mapping: engine ID {} -> custom ID {}",
            engineUnitId, customUnitIdOpt.value());
    }
}

void UnBARableAI::eventLuaMessage(const void *data) {
    const auto *event = static_cast<const SLuaMessageEvent *>(data);

    if (event == nullptr || event->inData == nullptr) {
        SPDLOG_LOGGER_WARN(logger, "Received empty EVENT_LUA_MESSAGE");
        return;
    }

    const std::string message{event->inData};
    SPDLOG_LOGGER_INFO(logger, "Received Lua message: {}", message);

    constexpr std::string_view gameOverPrefix = "UNBARABLE_GAME_OVER;";
    if (!message.starts_with(gameOverPrefix)) {
        return;
    }

    if (gameOver_) {
        SPDLOG_LOGGER_DEBUG(logger, "Ignoring duplicate game-over message: {}",
                            message);
        return;
    }

    const bool won = message.find(";result=win;") != std::string::npos;
    const bool lost = message.find(";result=loss;") != std::string::npos;
    if (!won && !lost) {
        SPDLOG_LOGGER_ERROR(logger, "Invalid game-over message: {}", message);
        return;
    }

    SPDLOG_LOGGER_INFO(logger,
                       "Authoritative game result received: "
                       "result={}, current_frame={}, message={}",
                       won ? "win" : "loss", currentFrame_, message);

    if (won) {
        finishEpisode(UnBARableAINS::EngineStatus::GAME_ENDED, message);
    } else {
        finishEpisode(UnBARableAINS::EngineStatus::TEAM_DIED, message);
    }
}

void UnBARableAI::eventRelease(const void *data) {
    const auto *event = static_cast<const SReleaseEvent *>(data);

    const int releaseReason = event != nullptr ? event->reason : 0;

    SPDLOG_LOGGER_INFO(logger,
                       "EVENT_RELEASE received: "
                       "reason={}, game_over={}, "
                       "current_frame={}",
                       releaseReason, gameOver_, currentFrame_);

    if (!gameOver_) {
        SPDLOG_LOGGER_WARN(logger,
                           "EVENT_RELEASE received before game-over message. "
                           "Writing terminal loss observation at frame {}. "
                           "Release reason={}",
                           currentFrame_, releaseReason);

        finishEpisode(UnBARableAINS::EngineStatus::TEAM_DIED,
                      "release_fallback"
                      ";frame=" +
                          std::to_string(currentFrame_) +
                          ";release_reason=" + std::to_string(releaseReason));
    } else {
        SPDLOG_LOGGER_INFO(logger, "Episode already finished. "
                                   "EVENT_RELEASE performs cleanup only.");
    }

    engineToCustomUnitId_.clear();
    customToEngineUnitId_.clear();

    SPDLOG_LOGGER_DEBUG(logger, "EVENT_RELEASE cleanup completed");
}

void UnBARableAI::eventUpdate(const void *data) {
    const SUpdateEvent *event = static_cast<const SUpdateEvent *>(data);
    currentFrame_ = event->frame;
    const bool updateDue =
        currentFrame_ >= firstUpdateFrame_ &&
        ((currentFrame_ - firstUpdateFrame_) % updateIntervalFrames_ == 0);

    if (updateDue) {
        if (gameOver_) {
            SPDLOG_LOGGER_INFO(logger, "EVENT_UPDATE called when Game "
                                       "Over: No Observation written");
            return;
        }

        SPDLOG_LOGGER_DEBUG(
            logger,
            "--------------------------------------------------------------"
            "-----Starting update for frame "
            "{}------------------------------------------------------------"
            "-----------",
            currentFrame_);

        updateObservation();

        // Call python side
        const auto connection = shm_.getGrpcConnection();
        UnBARableAIClient client = UnBARableAIClient(connection);
        if (!client.HandleEventUpdate(30000)) {
            SPDLOG_LOGGER_ERROR(logger, "Frame {} event update failed: {}",
                                currentFrame_, client.GetLastError());
        } else {
            SPDLOG_LOGGER_DEBUG(logger,
                                "Frame {} event update sent successfully",
                                currentFrame_);
        }

        const auto actions = shm_.readAll<ActionOpt>();

        SPDLOG_LOGGER_DEBUG(logger, "Frame {} received {} actions",
                            currentFrame_, actions.size());

        for (const auto &a : actions) {
            if (!a.has_value()) {
                SPDLOG_LOGGER_DEBUG(logger, "Empty action received");
                continue;
            }
            const auto &action = a.value();

            executeAction(action);
        }
    }
    SPDLOG_LOGGER_DEBUG(logger, "Finished update for frame {}", currentFrame_);
}

void UnBARableAI::executeAction(const Action &action) {
    if (logger->should_log(spdlog::level::trace)) {
        std::ostringstream dump;
        UnBARableAINS::debug::dumpAction(dump, action, 1);
        SPDLOG_LOGGER_TRACE(logger, "Received action:\n{}", dump.str());
    }

    const uint32_t customUnitId = action.unit_id;
    // TODO possibly refactor into function so e.g. executeActionAttack can use
    // it.
    const auto engineUnitIdOpt = getEngineUnitId(customUnitId);
    if (!engineUnitIdOpt.has_value()) {
        SPDLOG_LOGGER_ERROR(logger, "No engine unit ID for custom unit ID {}",
                            action.unit_id);
        return;
    }
    const int engineUnitId = engineUnitIdOpt.value();
    springai::Unit *unit =
        springai::WrappUnit::GetInstance(action.team_id, engineUnitId);
    if (!unit) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Custom unit ID {} maps to engine unit ID {}, "
                            "which does not actually exist.",
                            customUnitId, engineUnitId);
        return;
    }

    switch (action.action_id) {
    case ActionId::Attack: {
        executeActionAttack(unit, customUnitId, action.team_id,
                            action.target_unit_id);
        break;
    }
    default: {
        executeActionMove(unit, customUnitId, action.action_id);
    }
    }
}

void UnBARableAI::executeActionAttack(springai::Unit *unit,
                                      const uint32_t unitId,
                                      const uint32_t targetTeamId,
                                      const uint32_t targetUnitId) {
    if (!unit) {
        SPDLOG_LOGGER_CRITICAL(logger, "Do not call with unit=nullptr");
        return;
    }
    const auto engineTargetUnitIdOpt = getEngineUnitId(targetUnitId);
    if (!engineTargetUnitIdOpt.has_value()) {
        SPDLOG_LOGGER_ERROR(logger,
                            "No engine unit ID found for "
                            "target custom unit ID {}",
                            targetUnitId);
        return;
    }

    const int engineTargetUnitId = engineTargetUnitIdOpt.value();
    springai::Unit *enemyUnit =
        springai::WrappUnit::GetInstance(targetTeamId, engineTargetUnitId);
    unit->Attack(enemyUnit);
    SPDLOG_LOGGER_DEBUG(logger, "Unit {} attacks unit {}", unitId,
                        targetUnitId);
}

void UnBARableAI::executeActionMove(springai::Unit *unit, const uint32_t unitId,
                                    const ActionId moveAction) {
    if (!unit) {
        SPDLOG_LOGGER_CRITICAL(logger, "Do not call with unit=nullptr");
        return;
    }

    springai::AIFloat3 currentPos = unit->GetPos();
    springai::AIFloat3 targetPos = currentPos;
    const auto moveDistance = moveStepSize_;

    switch (moveAction) {
    case ActionId::MoveUp: {
        targetPos.z += moveDistance;
        break;
    }
    case ActionId::MoveRight: {
        targetPos.x += moveDistance;
        break;
    }

    case ActionId::MoveDown: {
        targetPos.z -= moveDistance;
        break;
    }
    case UnBARableAINS::ActionId::MoveLeft: {
        targetPos.x -= moveDistance;
        break;
    }
    default: {
        SPDLOG_LOGGER_CRITICAL(logger, "Called with invalid action {}",
                               static_cast<int>(moveAction));
    }
    }
    unit->MoveTo(targetPos);
    SPDLOG_LOGGER_DEBUG(logger, "Unit {} moves to ({}, {}, {})", unitId,
                        targetPos.x, targetPos.y, targetPos.z);
}

void UnBARableAI::finishEpisode(const UnBARableAINS::EngineStatus status,
                                const std::string &reason) {
    if (gameOver_) {
        SPDLOG_LOGGER_WARN(logger,
                           "Ignoring duplicate episode end: "
                           "current_frame={}, "
                           "status={}, reason=\"{}\"",
                           currentFrame_, static_cast<int>(status), reason);
        return;
    }

    SPDLOG_LOGGER_INFO(logger,
                       "Finishing episode: frame={}, status={}, reason=\"{}\"",
                       currentFrame_, static_cast<int>(status), reason);

    try {
        updateObservation();
    } catch (const std::exception &exception) {
        SPDLOG_LOGGER_ERROR(
            logger, "Failed to write terminal observation at frame {}: {}",
            currentFrame_, exception.what());
    } catch (...) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Failed to write terminal observation at frame {} "
                            "with unknown exception",
                            currentFrame_);
    }

    gameOver_ = true;

    updateEngineStatus(status, reason);

    // Call python side
    const auto connection = shm_.getGrpcConnection();
    UnBARableAIClient client = UnBARableAIClient(connection);

    if (!client.HandleEventUpdate(30000)) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Failed to send terminal event update: "
                            "frame={}, status={}, reason=\"{}\", error={}",
                            currentFrame_, static_cast<int>(status), reason,
                            client.GetLastError());

        return;
    }

    SPDLOG_LOGGER_INFO(logger,
                       "Terminal event update sent successfully: "
                       "frame={}, status={}, reason=\"{}\"",
                       currentFrame_, static_cast<int>(status), reason);
}

void UnBARableAI::updateObservation(void) {
    std::vector<springai::Unit *> friendlyUnits = callback_->GetFriendlyUnits();
    std::vector<springai::Unit *> enemyUnits =
        callback_->GetEnemyUnitsInRadarAndLos();
    std::vector<springai::Unit *> neutralUnits = callback_->GetNeutralUnits();

    // Lambda to write the three unit types.
    auto updateUnits = [&](std::vector<springai::Unit *> units,
                           const std::string_view name) -> std::size_t {
        std::size_t failed{0};
        const std::size_t totalNumUnits = units.size();
        for (springai::Unit *unit : units) {
            bool success = updateUnit(unit);
            if (!success) {
                failed++;
            }
        }
        if (!failed) {
            SPDLOG_LOGGER_TRACE(logger, "Wrote {} {} units", totalNumUnits,
                                name);
        } else {
            SPDLOG_LOGGER_TRACE(logger,
                                "Total number of {} units: {}. Wrote {} units, "
                                "failed writing for {} units.",
                                name, totalNumUnits, totalNumUnits - failed,
                                failed);
        }
        return failed;
    };

    std::size_t failed{0};
    failed += updateUnits(friendlyUnits, "friendly");
    failed += updateUnits(enemyUnits, "enemy");
    failed += updateUnits(neutralUnits, "neutral");

    if (!failed) {
        SPDLOG_LOGGER_DEBUG(
            logger, "observations written: friendly={}, enemy={}, neutral={}",
            friendlyUnits.size(), enemyUnits.size(), neutralUnits.size());
    } else {
        SPDLOG_LOGGER_ERROR(logger, "Failed writing {} units", failed);
    }
}

bool UnBARableAI::updateUnit(springai::Unit *unit) {
    UnitData unitData{};
    try {
        const auto opt = getUnitData(unit);
        if (!opt.has_value()) {
            SPDLOG_LOGGER_ERROR(logger, "Cannot update unit.");
            return false;
        } else {
            unitData = opt.value();
        }
    } catch (const std::exception &e) {
        SPDLOG_LOGGER_ERROR(logger,
                            "Getting UnitData failed for engine_unit_id={}: {}",
                            unit->GetUnitId(), e.what());
        return false;
    }
    SPDLOG_LOGGER_DEBUG(
        logger, "Writing observation to shared memory for engine_unit_id={}",
        unit->GetUnitId());
    if (logger->should_log(spdlog::level::trace)) {
        std::ostringstream dump;

        UnBARableAINS::debug::dumpUnitData(dump, unitData, 1);

        SPDLOG_LOGGER_TRACE(logger, "UnitData for engine_unit_id={}:\n{}",
                            unit->GetUnitId(), dump.str());
    }
    try {
        shm_.updateUnitData(unitData.unit_id, unitData);
        SPDLOG_LOGGER_DEBUG(logger,
                            "Wrote UnitData to shared memory: "
                            "engine_unit_id={}, serializable_id={}",
                            unit->GetUnitId(), unitData.unit_id);
        return true;
    } catch (const std::exception &exception) {
        SPDLOG_LOGGER_ERROR(logger,
                            "writeUnitData failed for engine_unit_id={}: {}",
                            unit->GetUnitId(), exception.what());
        return false;
    } catch (...) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "writeUnitData failed for engine_unit_id={} with unknown error",
            unit->GetUnitId());
        return false;
    }
}

auto UnBARableAI::getUnitData(springai::Unit *unit) -> std::optional<UnitData> {
    UnitData unitData{};

    int engineUnitId = unit->GetUnitId();
    const auto customUnitIdOpt =
        registerUnit(engineUnitId); // wenn id schon regestriert ist
                                    // wird diese zurückgegeben

    if (!customUnitIdOpt.has_value()) {
        SPDLOG_LOGGER_ERROR(logger, "Cannot get unit data");
        return std::nullopt;
    }

    unitData.unit_id = customUnitIdOpt.value();
    unitData.unit_def_id = unit->GetDef()->GetUnitDefId();
    // Currently unused
    std::string unitName = unit->GetDef()->GetName();
    std::string humanName = unit->GetDef()->GetHumanName();

    unitData.team_id = unit->GetTeam();
    unitData.ally_team_id = unit->GetAllyTeam();
    unitData.health = unit->GetHealth();
    unitData.max_health = unit->GetMaxHealth();
    unitData.pos_x = unit->GetPos().x;
    unitData.pos_y = unit->GetPos().y;
    unitData.pos_z = unit->GetPos().z;
    unitData.los_radius = unit->GetDef()->GetLosRadius();
    unitData.air_los_radius = unit->GetDef()->GetAirLosRadius();
    unitData.is_dead = (unitData.health <= 0.0f);
    unitData.being_built = (unit->GetBuildProgress() < 1.0f);
    unitData.build_progress = unit->GetBuildProgress();
    unitData.capture_progress = unit->GetCaptureProgress();
    unitData.paralyze_damage = unit->GetParalyzeDamage();
    return unitData;
}

std::filesystem::path UnBARableAI::writeSharedMemoryState(void) const {
    const std::filesystem::path filename = getSharedMemoryStateFilename();
    std::ofstream output{filename};

    if (!output) {
        throw std::ios_base::failure{"UnBARableAI::writeSharedMemoryState: "
                                     "Could not open output file '" +
                                     filename.string() + "'"};
    }

    auto shmName = shm_.getName();
    if (!shmName.empty() && shmName.front() == '/') {
        shmName.erase(0, 1);
    }
    const std::filesystem::path shmFilename =
        std::filesystem::path{"/dev/shm/"} / shmName;
    std::ifstream shmFile(shmFilename, std::ios::binary);
    if (!shmFile) {
        throw std::ios_base::failure{
            "UnBARableAI::writeSharedMemoryState: Could not open "
            "shared-memory "
            "file '" +
            shmFilename.string() + "'"};
    }

    UnBARableAINS::memory::debug::hexdump(output, shmFile);

    return filename;
}

std::filesystem::path UnBARableAI::getSharedMemoryStateFilename(void) {
    const std::filesystem::path directory = std::filesystem::current_path() /
                                            "UnBARableAI" / "logs" /
                                            "shared_memory";
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    if (error) {
        throw std::filesystem::filesystem_error{
            "Could not create shared-memory log directory", directory, error};
    }

    const auto now = std::chrono::system_clock::now();
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch());
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_r(&time, &localTime);
    std::ostringstream timestamp;
    timestamp << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S") << '.'
              << std::setfill('0') << std::setw(6) << microseconds.count();
    return directory / ("shared_memory_state_" + timestamp.str() + ".log");
}

std::optional<uint32_t> UnBARableAI::registerUnit(const int engineUnitId) {
    const auto existingUnit = engineToCustomUnitId_.find(engineUnitId);

    if (existingUnit != engineToCustomUnitId_.end()) {
        return existingUnit->second;
    }

    const auto unitSlot = shm_.getEmptyUnitSlot();

    if (!unitSlot.has_value()) {
        SPDLOG_LOGGER_CRITICAL(logger,
                               "No empty UnitDataOpt Serializable left in "
                               "shared-memory for engine unit {}",
                               engineUnitId);
        return std::nullopt;
    }

    const uint32_t customUnitId = unitSlot.value();

    engineToCustomUnitId_[engineUnitId] = customUnitId;
    customToEngineUnitId_[customUnitId] = engineUnitId;

    SPDLOG_LOGGER_DEBUG(logger,
                        "Registered unit mapping: engine_unit_id={} -> "
                        "custom_unit_id={}",
                        engineUnitId, customUnitId);

    return customUnitId;
}

std::optional<uint32_t> UnBARableAI::getCustomUnitId(int engineUnitId) const {
    const auto unitEntry = engineToCustomUnitId_.find(engineUnitId);

    if (unitEntry == engineToCustomUnitId_.end()) {
        return std::nullopt;
    }

    return unitEntry->second;
}

std::optional<int> UnBARableAI::getEngineUnitId(uint32_t customUnitId) const {
    const auto unitEntry = customToEngineUnitId_.find(customUnitId);

    if (unitEntry == customToEngineUnitId_.end()) {
        return std::nullopt;
    }

    return unitEntry->second;
}

#include "OptionValues.h"
#include "SkirmishAI.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

void UnBARableAI::loadConfiguration(void) {
    springai::SkirmishAI *skirmishAI = callback_->GetSkirmishAI();

    springai::OptionValues *options = skirmishAI->GetOptionValues();

    const auto readString = [options](const std::string &key) -> std::string {
        const char *value = options->GetValueByKey(key.c_str());
        return value != nullptr ? std::string{value} : std::string{};
    };

    const auto readInt = [&readString](const std::string &key,
                                       int defaultValue) -> int {
        const std::string value = readString(key);

        if (value.empty()) {
            SPDLOG_LOGGER_INFO(logger,
                               "AI option '{}' not set; using default {}", key,
                               defaultValue);

            return defaultValue;
        }

        try {
            std::size_t parsedCharacters = 0;

            const int parsedValue = std::stoi(value, &parsedCharacters);

            if (parsedCharacters != value.size()) {
                throw std::invalid_argument{"trailing characters"};
            }

            return parsedValue;
        } catch (const std::exception &exception) {
            SPDLOG_LOGGER_WARN(logger,
                               "Invalid integer AI option '{}={}': {}. "
                               "Using default {}",
                               key, value, exception.what(), defaultValue);

            return defaultValue;
        }
    };

    const auto readFloat = [&readString](const std::string &key,
                                         float defaultValue) -> float {
        const std::string value = readString(key);

        if (value.empty()) {
            SPDLOG_LOGGER_INFO(logger,
                               "AI option '{}' not set; using default {}", key,
                               defaultValue);

            return defaultValue;
        }

        try {
            std::size_t parsedCharacters = 0;

            const float parsedValue = std::stof(value, &parsedCharacters);

            if (parsedCharacters != value.size()) {
                throw std::invalid_argument{"trailing characters"};
            }

            return parsedValue;
        } catch (const std::exception &exception) {
            SPDLOG_LOGGER_WARN(logger,
                               "Invalid floating-point AI option '{}={}': {}. "
                               "Using default {}",
                               key, value, exception.what(), defaultValue);

            return defaultValue;
        }
    };

    updateIntervalFrames_ =
        std::max(1, readInt("unbarable_update_interval_frames", 2));

    firstUpdateFrame_ = std::max(0, readInt("unbarable_first_update_frame", 2));

    moveStepSize_ = std::clamp(readFloat("unbarable_move_step_size", 100.0f),
                               1.0f, 5000.0f);

    SPDLOG_LOGGER_INFO(logger,
                       "Loaded AI configuration: update_interval_frames={}, "
                       "first_update_frame={}, move_step_size={}",
                       updateIntervalFrames_, firstUpdateFrame_, moveStepSize_);
}
