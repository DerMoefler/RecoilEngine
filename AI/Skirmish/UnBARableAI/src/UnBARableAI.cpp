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
#include "UnBARableAI/engine_status.h"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "memory/debug/hexdump.hpp"
#include "utility/debug/print_helpers.hpp"

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
    SPDLOG_LOGGER_INFO(
      logger,
      "Received INIT event: team_id={}, pid={}, shared_memory=\"{}\"",
      teamId_,
      pid,
      shmName_);
    break;
  }
  case EVENT_RELEASE: {
    /**
     * Values description for reason:
     * 0: unspecified
     * 1: game ended
     * 2: team died
     * 3: AI killed
     * 4: AI crashed
     * 5: AI failed to init
     * 6: connection lost
     * 7: other reason
     **/
    const SReleaseEvent *event = static_cast<const SReleaseEvent *>(data);
    const int reason = event->reason;
    if (reason < 0 || reason > 7) {
      SPDLOG_LOGGER_ERROR(
        logger,
        "EVENT_RELEASE received invalid reason code {}",
        reason);
      break;
    }
    const auto status = static_cast<UnBARableAINS::EngineStatus>(reason);

    auto sharedMemory =
        UnBARableAINS::memory::BarSharedMemory::open(shmName_);
    const auto serializableId = sharedMemory.writeEngineStatus(status);

    SPDLOG_LOGGER_INFO(
        logger,
        "EVENT_RELEASE wrote engine status: reason={}, serializable_id={}",
        reason,
        serializableId);

    UnBARableAIClient client = UnBARableAIClient();
    if (!client.HandleEventUpdate()) {
      SPDLOG_LOGGER_ERROR(
        logger,
        "Failed to send event update: {}",
        client.GetLastError());
    } else {
      SPDLOG_LOGGER_INFO(
        logger,
        "Release event update sent successfully");
    }
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
  case EVENT_UPDATE: {
    const SUpdateEvent *event = static_cast<const SUpdateEvent *>(data);
    if ((event->frame % 2) == 0 && event->frame > 2) {
      SPDLOG_LOGGER_DEBUG(
        logger,
        "-------------------------------------------------------------------Starting update for frame {}-----------------------------------------------------------------------",
        event->frame);
      
      std::vector<springai::Unit *> friendlyUnits = callback_->GetFriendlyUnits();
      std::vector<springai::Unit *> enemyUnits = callback_->GetEnemyUnitsInRadarAndLos();
      std::vector<springai::Unit *> neutralUnits = callback_->GetNeutralUnits();

      for (springai::Unit *unit : friendlyUnits) {
        writeObservationToSharedMemory(unit);
      }
      SPDLOG_LOGGER_TRACE(
        logger,
        "friendly Units written size: {}",
        friendlyUnits.size()
      );

      for (springai::Unit *unit : enemyUnits) {
        writeObservationToSharedMemory(unit);
      }

      SPDLOG_LOGGER_TRACE(
        logger,
        "enemy Units written size: {}",
        enemyUnits.size()
      );

      for (springai::Unit *unit : neutralUnits) {
        writeObservationToSharedMemory(unit);
      }

      SPDLOG_LOGGER_TRACE(
        logger,
        "neutral Units written size: {}",
        neutralUnits.size()
      );

      SPDLOG_LOGGER_DEBUG(
        logger,
        "Frame {} observations written: friendly={}, enemy={}, neutral={}",
        event->frame,
        friendlyUnits.size(),
        enemyUnits.size(),
        neutralUnits.size());

      auto sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open(shmName_);
      const auto serializableId =
          sharedMemory.writeEngineStatus(UnBARableAINS::EngineStatus::RUNNING);
      SPDLOG_LOGGER_TRACE(
        logger,
        "Engine status written: serializable_id={}",
        serializableId);

      UnBARableAIClient client = UnBARableAIClient();
      if (!client.HandleEventUpdate()) {
        SPDLOG_LOGGER_ERROR(
          logger,
          "Frame {} event update failed: {}",
          event->frame,
          client.GetLastError());
      } else {
        SPDLOG_LOGGER_DEBUG(
          logger,
          "Frame {} event update sent successfully",
          event->frame);
      }

      sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open(shmName_);
      std::vector<UnBARableAINS::Action> actions =
          sharedMemory.readAll<UnBARableAINS::Action>();

      SPDLOG_LOGGER_DEBUG(
        logger,
        "Frame {} received {} actions",
        event->frame,
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
          float moveDistance = 100.0f;
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
        event->frame);
    }
    break;
  }
  default:
    break;
  }
}

void UnBARableAI::writeObservationToSharedMemory(springai::Unit *unit) {
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
        "UnBARableAI::writeObservationToSharedMemory failed: "
        "System Error "
        "{} when opening the BarSharedMemory.",
        e.what());
  } catch (const std::exception &e) {
    SPDLOG_LOGGER_ERROR(
        logger,
        "UnBARableAI::writeObservationToSharedMemory failed: "
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
