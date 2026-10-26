#include "UnBARableAI.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream> // TODO remove
#include <string>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Economy.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Map.h"
#include "Resource.h"
#include "UnBARableAI/UnBARableAIClient.h"
#include "UnBARableAI/action.h"
#include "UnBARableAI/bar_shared_memory.h"
#include "UnBARableAI/debug/dump_action.hpp"
#include "UnBARableAI/debug/dump_unit_data.hpp"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "memory/debug/hexdump.hpp"
#include "utility/debug/print_helpers.hpp"
#include "SkirmishAI.h"

namespace {
auto logger = spdlog::stdout_color_mt("UnBARableAI");
struct ConfigureLogger {
  ConfigureLogger() {
    logger->set_level(spdlog::level::trace);
    logger->set_pattern(
      "[%H:%M:%S.%e] [%^%l%$] [%n::%!] %v");
  }
};
ConfigureLogger configureLogger;
} // namespace

UnBARableAI::UnBARableAI(springai::OOAICallback *callback)
    : callback_(callback) {
  teamId_ = callback_->GetSkirmishAIId();
  srand(time(NULL));
}

UnBARableAI::~UnBARableAI() {
  // Cleanup if needed
}

void UnBARableAI::HandleEvent(int topic, const void *data) {
  switch (topic) {
  case EVENT_INIT: {
    // Initialization event
    pid_t pid = getpid();
    shmName_ = "/sm_proc_" + std::to_string(pid);

    loadConfiguration();

    SPDLOG_LOGGER_INFO(
      logger,
      "Received INIT event: team_id={}, pid={}, shared_memory=\"{}\"",
      teamId_,
      pid,
      shmName_);
    break;
  }
  case EVENT_UNIT_CREATED: {
    const SUnitCreatedEvent *event =
        static_cast<const SUnitCreatedEvent *>(data);
    const int engineUnitId = event->unit;
    const int customUnitId = registerUnit(engineUnitId);

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, engineUnitId);

    springai::UnitDef *unitDef = unit->GetDef();

    SPDLOG_LOGGER_INFO(
        logger,
        "Unit created, name: {}, custom ID: {}, engine ID: {}",
        (unitDef != nullptr ? unitDef->GetName() : "unknown"),
        customUnitId,
        engineUnitId);

    break;
  }
  case EVENT_UNIT_DESTROYED: {
    const SUnitDestroyedEvent *event =
        static_cast<const SUnitDestroyedEvent *>(data);

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, event->unit);

    const int engineUnitId = event->unit;
    const int customUnitId = getCustomUnitId(engineUnitId);

    if (unit && unit->GetDef()) {
      SPDLOG_LOGGER_INFO(
        logger,
        "Unit destroyed: name=\"{}\", engine_unit_id={}, "
        "custom_unit_id={}, attacker_engine_id={}",
        unit->GetDef()->GetName(),
        engineUnitId,
        customUnitId,
        event->attacker);
    }

    // Mapping entfernen
    if (customUnitId != -1) {
      engineToCustomUnitId_.erase(engineUnitId);
      customToEngineUnitId_.erase(customUnitId);

      SPDLOG_LOGGER_DEBUG(
        logger,
        "Removed unit mapping: engine ID {} -> custom ID {}",
        engineUnitId,
        customUnitId);
    }
    break;
  }

  case EVENT_LUA_MESSAGE: {
    const auto* event =
        static_cast<const SLuaMessageEvent*>(data);

    if (event == nullptr || event->inData == nullptr) {
        SPDLOG_LOGGER_WARN(
            logger,
            "Received empty EVENT_LUA_MESSAGE");
        break;
    }

    const std::string message{event->inData};

    SPDLOG_LOGGER_INFO(
        logger,
        "Received Lua message: {}",
        message);

    constexpr std::string_view prefix =
        "UNBARABLE_GAME_OVER;";

    if (!message.starts_with(prefix)) {
        break;
    }

    if (gameOver_) {
        SPDLOG_LOGGER_DEBUG(
            logger,
            "Ignoring duplicate game-over message: {}",
            message);

        break;
    }

    const bool won =
        message.find(";result=win;") != std::string::npos;

    const bool lost =
        message.find(";result=loss;") != std::string::npos;

    if (!won && !lost) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Invalid game-over message: {}",
            message);

        break;
    }

    SPDLOG_LOGGER_INFO(
        logger,
        "Authoritative game result received: "
        "result={}, current_frame={}, message={}",
        won ? "win" : "loss",
        currentFrame_,
        message);

    if (won) {
        finishEpisode(
            UnBARableAINS::EngineStatus::GAME_ENDED,
            message);
    } else {
        finishEpisode(
            UnBARableAINS::EngineStatus::TEAM_DIED,
            message);
    }

    break;
  }
  case EVENT_RELEASE: {
    const auto* event =
        static_cast<const SReleaseEvent*>(data);

    const int releaseReason =
        event != nullptr
            ? event->reason
            : 0;

    SPDLOG_LOGGER_INFO(
        logger,
        "EVENT_RELEASE received: "
        "reason={}, game_over={}, "
        "current_frame={}",
        releaseReason,
        gameOver_,
        currentFrame_);

    if (!gameOver_) {
        SPDLOG_LOGGER_WARN(
            logger,
            "EVENT_RELEASE received before game-over message. "
            "Writing terminal loss observation at frame {}. "
            "Release reason={}",
            currentFrame_,
            releaseReason);

        finishEpisode(
            UnBARableAINS::EngineStatus::TEAM_DIED,
            "release_fallback"
            ";frame=" + std::to_string(currentFrame_)
            + ";release_reason=" + std::to_string(releaseReason));
    } else {
        SPDLOG_LOGGER_INFO(
            logger,
            "Episode already finished. "
            "EVENT_RELEASE performs cleanup only.");
    }

    
    engineToCustomUnitId_.clear();
    customToEngineUnitId_.clear();

    SPDLOG_LOGGER_DEBUG(
        logger,
        "EVENT_RELEASE cleanup completed");

    break;
  }
  case EVENT_UPDATE: {
    const SUpdateEvent *event = static_cast<const SUpdateEvent *>(data);
    currentFrame_ = event->frame;
    const bool updateDue = currentFrame_ >= firstUpdateFrame_ && ((currentFrame_ - firstUpdateFrame_) % updateIntervalFrames_ == 0);

    if (updateDue) {
      SPDLOG_LOGGER_DEBUG(
        logger,
        "-------------------------------------------------------------------Starting update for frame {}-----------------------------------------------------------------------",
        currentFrame_);

      if (gameOver_) {
        SPDLOG_LOGGER_INFO(
          logger,
          "EVENT_UPDATE called when Game Over: No Observation written");
        break;
      }
      
      writeObservationToSharedMemory();

      auto sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open(shmName_);
      const auto serializableId =
          sharedMemory.writeEngineStatus(UnBARableAINS::EngineStatus::RUNNING);
      SPDLOG_LOGGER_TRACE(
        logger,
        "Engine status written: running,  serializable_id={}",
        serializableId);

      UnBARableAIClient client = UnBARableAIClient();
      if (!client.HandleEventUpdate(30000)) {
        SPDLOG_LOGGER_ERROR(
          logger,
          "Frame {} event update failed: {}",
          currentFrame_,
          client.GetLastError());
      } else {
        SPDLOG_LOGGER_DEBUG(
          logger,
          "Frame {} event update sent successfully",
          currentFrame_);
      }

      sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open(shmName_);
      std::vector<UnBARableAINS::Action> actions =
          sharedMemory.readAll<UnBARableAINS::Action>();

      SPDLOG_LOGGER_DEBUG(
        logger,
        "Frame {} received {} actions",
        currentFrame_,
        actions.size());
      
      for (const UnBARableAINS::Action &action : actions) {
        if (logger->should_log(spdlog::level::trace)) {
          std::ostringstream dump;

          UnBARableAINS::debug::dumpAction(
              dump,
              action,
              1);

          SPDLOG_LOGGER_TRACE(
              logger,
              "Received action:\n{}",
              dump.str());
        }
        int engineUnitId = getEngineUnitId(action.unit_id);
        if (engineUnitId == -1) {
          SPDLOG_LOGGER_ERROR(
            logger,
            "No engine unit ID for custom unit ID {}",
            action.unit_id);
          continue;
        }
        springai::Unit *unit =
            springai::WrappUnit::GetInstance(action.team_id, engineUnitId);
        if (action.action_id == UnBARableAINS::ActionId::Attack) {
          const int engineTargetUnitId = getEngineUnitId(action.target_unit_id);
          if (engineTargetUnitId == -1) {
            SPDLOG_LOGGER_ERROR(
              logger,
              "No engine unit ID found for target custom unit ID {}",
              action.target_unit_id);
            continue;
          }
          springai::Unit *enemyUnit = springai::WrappUnit::GetInstance(
              action.team_id, engineTargetUnitId);
          unit->Attack(enemyUnit);
          SPDLOG_LOGGER_DEBUG(
            logger,
            "Unit {} attacks unit {}",
            action.unit_id,
            action.target_unit_id);
        } else {
          springai::AIFloat3 currentPos = unit->GetPos();
          springai::AIFloat3 targetPos = currentPos;
          const float moveDistance = moveStepSize_;
          switch (action.action_id) {
          case UnBARableAINS::ActionId::MoveRight: {
            targetPos.x += moveDistance;
            break;
          }
          case UnBARableAINS::ActionId::MoveLeft: {
            targetPos.x -= moveDistance;
            break;
          }
          case UnBARableAINS::ActionId::MoveUp: {
            targetPos.z += moveDistance;
            break;
          }
          case UnBARableAINS::ActionId::MoveDown: {
            targetPos.z -= moveDistance;
            break;
          }
          case UnBARableAINS::ActionId::Attack: {
            // Already handled above
            break;
          }
          }
          unit->MoveTo(targetPos);
          SPDLOG_LOGGER_DEBUG(
            logger,
            "Unit {} moves to ({}, {}, {})",
            action.unit_id,
            targetPos.x,
            targetPos.y,
            targetPos.z);
        }
      }
      SPDLOG_LOGGER_DEBUG(
        logger,
        "Finished update for frame {}",
        currentFrame_);
    }
    break;
  }
  default:
    break;
  }
}

void UnBARableAI::finishEpisode(
    UnBARableAINS::EngineStatus status,
    const std::string& reason)
{
    if (gameOver_) {
        SPDLOG_LOGGER_WARN(
            logger,
            "Ignoring duplicate episode end: "
            "current_frame={}, "
            "status={}, reason=\"{}\"",
            currentFrame_,
            static_cast<int>(status),
            reason);
        return;
    }


    SPDLOG_LOGGER_INFO(
        logger,
        "Finishing episode: frame={}, status={}, reason=\"{}\"",
        currentFrame_,
        static_cast<int>(status),
        reason);

    try {
        writeObservationToSharedMemory();
    } catch (const std::exception& exception) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Failed to write terminal observation at frame {}: {}",
            currentFrame_,
            exception.what());
    } catch (...) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Failed to write terminal observation at frame {} "
            "with unknown exception",
            currentFrame_);
    }

    
    gameOver_ = true;

    try {
        auto sharedMemory =
            UnBARableAINS::memory::BarSharedMemory::open(shmName_);

        const auto serializableId =
            sharedMemory.writeEngineStatus(status);

        SPDLOG_LOGGER_INFO(
            logger,
            "Terminal engine status written: "
            "frame={}, status={}, reason=\"{}\", serializable_id={}",
            currentFrame_,
            static_cast<int>(status),
            reason,
            serializableId);
    } catch (const std::exception& exception) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Failed to write terminal engine status: "
            "frame={}, status={}, reason=\"{}\", error={}",
            currentFrame_,
            static_cast<int>(status),
            reason,
            exception.what());

        return;
    } catch (...) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Failed to write terminal engine status: "
            "frame={}, status={}, reason=\"{}\", unknown error",
            currentFrame_,
            static_cast<int>(status),
            reason);

        return;
    }

    UnBARableAIClient client;

    if (!client.HandleEventUpdate(30000)) {
        SPDLOG_LOGGER_ERROR(
            logger,
            "Failed to send terminal event update: "
            "frame={}, status={}, reason=\"{}\", error={}",
            currentFrame_,
            static_cast<int>(status),
            reason,
            client.GetLastError());

        return;
    }

    SPDLOG_LOGGER_INFO(
        logger,
        "Terminal event update sent successfully: "
        "frame={}, status={}, reason=\"{}\"",
        currentFrame_,
        static_cast<int>(status),
        reason);
}


void UnBARableAI::writeObservationToSharedMemory() {
  std::vector<springai::Unit *> friendlyUnits = callback_->GetFriendlyUnits();
  std::vector<springai::Unit *> enemyUnits = callback_->GetEnemyUnitsInRadarAndLos();
  std::vector<springai::Unit *> neutralUnits = callback_->GetNeutralUnits();

  for (springai::Unit *unit : friendlyUnits) {
    writeUnitToSharedMemory(unit);
  }
  SPDLOG_LOGGER_TRACE(
    logger,
    "friendly Units written size: {}",
    friendlyUnits.size()
  );

  for (springai::Unit *unit : enemyUnits) {
    writeUnitToSharedMemory(unit);
  }

  SPDLOG_LOGGER_TRACE(
    logger,
    "enemy Units written size: {}",
    enemyUnits.size()
  );

  for (springai::Unit *unit : neutralUnits) {
    writeUnitToSharedMemory(unit);
  }

  SPDLOG_LOGGER_TRACE(
    logger,
    "neutral Units written size: {}",
    neutralUnits.size()
  );

  SPDLOG_LOGGER_DEBUG(
    logger,
    "observations written: friendly={}, enemy={}, neutral={}",
    friendlyUnits.size(),
    enemyUnits.size(),
    neutralUnits.size());
}

void UnBARableAI::writeUnitToSharedMemory(springai::Unit *unit) {
  UnitData unitData{};
  try {
    unitData = getUnitData(unit);
  } catch (const std::exception &e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "Getting UnitData failed for engine_unit_id={}: {}",
        unit->GetUnitId(),
        e.what());
  }

  SPDLOG_LOGGER_DEBUG(
      logger,
      "Writing observation to shared memory for engine_unit_id={}",
      unit->GetUnitId());
  
  
  if (logger->should_log(spdlog::level::trace)) {
    std::ostringstream dump;

    UnBARableAINS::debug::dumpUnitData(
        dump,
        unitData,
        1);

    SPDLOG_LOGGER_TRACE(
        logger,
        "UnitData for engine_unit_id={}:\n{}",
        unit->GetUnitId(),
        dump.str());
  }

  try {
    auto sharedMemory =
        UnBARableAINS::memory::BarSharedMemory::open(shmName_);
    UnBARableAINS::debug::makeIndentation(std::cout, 1);
    SPDLOG_LOGGER_DEBUG(
        logger,
        "1) Opened BarSharedMemory with name \"{}\"",
        shmName_);
    try {
      UnBARableAINS::debug::makeIndentation(std::cout, 1);
      SPDLOG_LOGGER_DEBUG(
          logger,
          "2) Writing UnitData to shared memory: engine_unit_id={}",
          unit->GetUnitId());

      const auto serializableId = sharedMemory.writeUnitData(unitData);

      UnBARableAINS::debug::makeIndentation(std::cout, 1);
      SPDLOG_LOGGER_DEBUG(
          logger,
          "3) Wrote UnitData to shared memory: engine_unit_id={}, serializable_id={}",
          unit->GetUnitId(),
          serializableId);
    } catch (const std::exception &exception) {
      SPDLOG_LOGGER_ERROR(
          logger,
          "writeUnitData failed for engine_unit_id={}: {}",
          unit->GetUnitId(),
          exception.what());

      throw;
    } catch (...) {
      SPDLOG_LOGGER_ERROR(
          logger,
          "writeUnitData failed for engine_unit_id={} with unknown error",
          unit->GetUnitId());

      throw;
    }
  } catch (const std::system_error &e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "UnBARableAI::writeUnitToSharedMemory failed: "
        "System Error "
        "{} when opening the BarSharedMemory.",
        e.what());
  } catch (const std::exception &e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "UnBARableAI::writeUnitToSharedMemory failed: "
        "Exception {} when opening the BarSharedMemory.",
        e.what());

    try {
      auto path = writeSharedMemoryState();
      SPDLOG_LOGGER_ERROR(
          logger,
          "Shared memory state written to {} for debugging.",
          path.string());
    } catch (const std::exception &e) {
      SPDLOG_LOGGER_ERROR(
          logger,
          "Could not write shared memory state. Reason: {}",
          e.what());
    }
  }
}

UnBARableAI::UnitData UnBARableAI::getUnitData(springai::Unit *unit) {
  UnitData unitData{};

  int engineUnitId = unit->GetUnitId();
  int customUnitId = registerUnit(
      engineUnitId); // wenn id schon regestriert ist wird diese zurückgegeben

  unitData.unit_id = customUnitId;
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

  std::string shmName{shmName_};
  if (!shmName.empty() && shmName.front() == '/') {
    shmName.erase(0, 1);
  }
  const std::filesystem::path shmFilename =
      std::filesystem::path{"/dev/shm/"} / shmName;
  std::ifstream shmFile(shmFilename, std::ios::binary);
  if (!shmFile) {
    throw std::ios_base::failure{
        "UnBARableAI::writeSharedMemoryState: Could not open shared-memory "
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
  return directory / ("shared_memor_state_" + timestamp.str() + ".log");
}

int UnBARableAI::registerUnit(int engineUnitId) {
  const auto existingUnit = engineToCustomUnitId_.find(engineUnitId);

  if (existingUnit != engineToCustomUnitId_.end()) {
    return existingUnit->second;
  }

  const int customUnitId = nextCustomUnitId_++;

  engineToCustomUnitId_[engineUnitId] = customUnitId;
  customToEngineUnitId_[customUnitId] = engineUnitId;

  SPDLOG_LOGGER_DEBUG(
      logger,
      "Registered unit mapping: engine_unit_id={} -> custom_unit_id={}",
      engineUnitId,
      customUnitId);

  return customUnitId;
}

int UnBARableAI::getCustomUnitId(int engineUnitId) const {
  const auto unitEntry = engineToCustomUnitId_.find(engineUnitId);

  if (unitEntry == engineToCustomUnitId_.end()) {
    return -1;
  }

  return unitEntry->second;
}

int UnBARableAI::getEngineUnitId(int customUnitId) const {
  const auto unitEntry = customToEngineUnitId_.find(customUnitId);

  if (unitEntry == customToEngineUnitId_.end()) {
    return -1;
  }

  return unitEntry->second;
}

#include "SkirmishAI.h"
#include "OptionValues.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

void UnBARableAI::loadConfiguration()
{
    springai::SkirmishAI* skirmishAI =
        callback_->GetSkirmishAI();

    springai::OptionValues* options =
        skirmishAI->GetOptionValues();

    const auto readString =
        [options](const std::string& key) -> std::string {
            const char* value = options->GetValueByKey(key.c_str());
            return value != nullptr ? std::string{value} : std::string{};
        };

    const auto readInt =
        [&readString](
            const std::string& key,
            int defaultValue) -> int {
            const std::string value = readString(key);

            if (value.empty()) {
                SPDLOG_LOGGER_INFO(
                    logger,
                    "AI option '{}' not set; using default {}",
                    key,
                    defaultValue);

                return defaultValue;
            }

            try {
                std::size_t parsedCharacters = 0;

                const int parsedValue =
                    std::stoi(value, &parsedCharacters);

                if (parsedCharacters != value.size()) {
                    throw std::invalid_argument{
                        "trailing characters"};
                }

                return parsedValue;
            } catch (const std::exception& exception) {
                SPDLOG_LOGGER_WARN(
                    logger,
                    "Invalid integer AI option '{}={}': {}. "
                    "Using default {}",
                    key,
                    value,
                    exception.what(),
                    defaultValue);

                return defaultValue;
            }
        };

    const auto readFloat =
        [&readString](
            const std::string& key,
            float defaultValue) -> float {
            const std::string value = readString(key);

            if (value.empty()) {
                SPDLOG_LOGGER_INFO(
                    logger,
                    "AI option '{}' not set; using default {}",
                    key,
                    defaultValue);

                return defaultValue;
            }

            try {
                std::size_t parsedCharacters = 0;

                const float parsedValue =
                    std::stof(value, &parsedCharacters);

                if (parsedCharacters != value.size()) {
                    throw std::invalid_argument{
                        "trailing characters"};
                }

                return parsedValue;
            } catch (const std::exception& exception) {
                SPDLOG_LOGGER_WARN(
                    logger,
                    "Invalid floating-point AI option '{}={}': {}. "
                    "Using default {}",
                    key,
                    value,
                    exception.what(),
                    defaultValue);

                return defaultValue;
            }
        };

    updateIntervalFrames_ = std::max(
        1,
        readInt("unbarable_update_interval_frames", 2));

    firstUpdateFrame_ = std::max(
        0,
        readInt("unbarable_first_update_frame", 2));

    moveStepSize_ = std::clamp(
        readFloat("unbarable_move_step_size", 100.0f),
        1.0f,
        5000.0f);

    SPDLOG_LOGGER_INFO(
        logger,
        "Loaded AI configuration: update_interval_frames={}, "
        "first_update_frame={}, move_step_size={}",
        updateIntervalFrames_,
        firstUpdateFrame_,
        moveStepSize_);
}