#include "UnBARableAI.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <string>
#include <unistd.h>

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

UnBARableAI::UnBARableAI(springai::OOAICallback *callback)
    : callback_(callback) {
  teamId_ = callback_->GetSkirmishAIId();
  srand(time(NULL));
  std::cout << "UnBARableAI initialized for team " << teamId_ << std::endl;
}

UnBARableAI::~UnBARableAI() {
  // Cleanup if needed
}

void UnBARableAI::HandleEvent(int topic, const void *data) {
  switch (topic) {
  case EVENT_INIT: {
    // Initialization event
    std::cout << "UnBARableAI received INIT event" << std::endl;
    pid_t pid = getpid();
    std::cout << "PID: " << pid << '\n';
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
    const auto status = static_cast<UnBARableAINS::EngineStatus>(reason);

    auto sharedMemory =
        UnBARableAINS::memory::BarSharedMemory::open("/unbarable_ai_read");
    const auto serializableId = sharedMemory.writeEngineStatus(status);

    UnBARableAIClient client = UnBARableAIClient();
    if (!client.HandleEventUpdate(5000)) {
      std::cerr << "Failed to send event update: " << client.GetLastError()
                << std::endl;
    } else {
      std::cout << "Event update sent successfully" << std::endl;
    }
  }
  case EVENT_UNIT_CREATED: {
    const SUnitCreatedEvent *event =
        static_cast<const SUnitCreatedEvent *>(data);
    const int engineUnitId = event->unit;
    const int customUnitId = registerUnit(engineUnitId);

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, engineUnitId);

    springai::UnitDef *unitDef = unit->GetDef();

    std::cout << "Unit created, name: "
              << (unitDef != nullptr ? unitDef->GetName() : "unknown")
              << ", custom ID: " << customUnitId
              << ", engine ID: " << engineUnitId << std::endl;

    break;
  }
  case EVENT_UNIT_FINISHED: {
    const SUnitFinishedEvent *event =
        static_cast<const SUnitFinishedEvent *>(data);
    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, event->unit);
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
      std::cout << "Unit destroyed, name: " << unit->GetDef()->GetName()
                << ", ID: " << engineUnitId << ", Attacker: " << event->attacker
                << std::endl;
    }

    // Mapping entfernen
    if (customUnitId != -1) {
      engineToCustomUnitId_.erase(engineUnitId);
      customToEngineUnitId_.erase(customUnitId);

      std::cout << "Removed unit mapping: engine ID " << engineUnitId
                << " -> custom ID " << customUnitId << std::endl;
    }

    break;
  }
  case EVENT_UNIT_IDLE: {
    const SUnitIdleEvent *event = static_cast<const SUnitIdleEvent *>(data);

    springai::Unit *unit =
        springai::WrappUnit::GetInstance(teamId_, event->unit);

    break;
  }
  case EVENT_UPDATE: {
    const SUpdateEvent *event = static_cast<const SUpdateEvent *>(data);
    if ((event->frame % 2) == 0 && event->frame > 2) {
      std::cout << "start of eventUpdate at frame: " << event->frame
                << " ---------------------------" << std::endl;
      for (springai::Unit *unit : callback_->GetFriendlyUnits()) {
        writeObservationToSharedMemory(unit);
      }
      std::cout << "friendly units written to shared memory" << std::endl;

      for (springai::Unit *unit : callback_->GetEnemyUnitsInRadarAndLos()) {
        writeObservationToSharedMemory(unit);
      }
      std::cout << "enemy units written to shared memory" << std::endl;

      for (springai::Unit *unit : callback_->GetNeutralUnits()) {
        writeObservationToSharedMemory(unit);
      }
      std::cout << "neutral units written to shared memory" << std::endl;

      auto sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open("/unbarable_ai_read");
      const auto serializableId =
          sharedMemory.writeEngineStatus(UnBARableAINS::EngineStatus::RUNNING);

      UnBARableAIClient client = UnBARableAIClient();
      if (!client.HandleEventUpdate(5000)) {
        std::cerr << "Failed to send event update: " << client.GetLastError()
                  << std::endl;
      } else {
        std::cout << "Event update sent successfully" << std::endl;
      }

      sharedMemory =
          UnBARableAINS::memory::BarSharedMemory::open("/unbarable_ai_read");
      std::vector<UnBARableAINS::Action> actions =
          sharedMemory.readAll<UnBARableAINS::Action>();

      std::cout << "UnBARableAI::HandleEvent (EVENT_UPDATE): read actions\n";
      for (const UnBARableAINS::Action &action : actions) {
        UnBARableAINS::debug::dumpAction(std::cout, action, 1);
        int engineUnitId = getEngineUnitId(action.unit_id);
        if (engineUnitId == -1) {
          std::cerr << "Error: No engine unit ID found for custom unit ID "
                    << action.unit_id << std::endl;
          continue;
        }
        springai::Unit *unit =
            springai::WrappUnit::GetInstance(action.team_id, engineUnitId);
        if (action.action_id == UnBARableAINS::ActionId::Attack) {
          const int engineTargetUnitId = getEngineUnitId(action.target_unit_id);
          if (engineTargetUnitId == -1) {
            std::cerr << "Error: No engine unit ID found for "
                         "target custom unit ID "
                      << action.target_unit_id << std::endl;
            continue;
          }
          springai::Unit *enemyUnit = springai::WrappUnit::GetInstance(
              action.team_id, engineTargetUnitId);
          unit->Attack(enemyUnit);
          std::cout << "Unit " << action.unit_id << " attacking unit "
                    << action.target_unit_id << std::endl;
        } else {
          springai::AIFloat3 currentPos = unit->GetPos();
          springai::AIFloat3 targetPos = currentPos;
          float moveDistance = 10.0f;
          switch (action.action_id) {
          case UnBARableAINS::ActionId::MoveRight: {
            targetPos.x += moveDistance;
            std::cout << "Unit " << action.unit_id
                      << " moving right to position (" << targetPos.x << ", "
                      << targetPos.y << ", " << targetPos.z << ")" << std::endl;
            break;
          }
          case UnBARableAINS::ActionId::MoveLeft: {
            targetPos.x -= moveDistance;
            std::cout << "Unit " << action.unit_id
                      << " moving left to position (" << targetPos.x << ", "
                      << targetPos.y << ", " << targetPos.z << ")" << std::endl;
            break;
          }
          case UnBARableAINS::ActionId::MoveUp: {
            targetPos.z += moveDistance;
            std::cout << "Unit " << action.unit_id << " moving up to position ("
                      << targetPos.x << ", " << targetPos.y << ", "
                      << targetPos.z << ")" << std::endl;
            break;
          }
          case UnBARableAINS::ActionId::MoveDown: {
            targetPos.z -= moveDistance;
            std::cout << "Unit " << action.unit_id
                      << " moving down to position (" << targetPos.x << ", "
                      << targetPos.y << ", " << targetPos.z << ")" << std::endl;
            break;
          }
          }
          unit->MoveTo(targetPos);
        }
      }
      std::cout << "end of eventUpdate at frame: " << event->frame
                << " ---------------------------" << std::endl;
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
    std::cerr << "UnBARableAI::writeObservationToSharedMemory failed. Reason: "
              << e.what() << std::endl;
  }

  std::cout << "UnBARableAI::writeObservationToSharedMemory" << std::endl;
  UnBARableAINS::debug::makeIndentation(std::cout, 1);
  std::cout << "0) UnitData:\n";
  UnBARableAINS::debug::makeIndentation(std::cout, 2);
  std::cout << "Engine Unit ID: " << unit->GetUnitId() << std::endl;
  UnBARableAINS::debug::dumpUnitData(std::cout, unitData, 2);

  try {
    auto sharedMemory =
        UnBARableAINS::memory::BarSharedMemory::open("/unbarable_ai_read");
    UnBARableAINS::debug::makeIndentation(std::cout, 1);
    std::cout << "1) Shared memory opened for writing unit data." << std::endl;
    try {
      UnBARableAINS::debug::makeIndentation(std::cout, 1);
      std::cout << "2) Calling writeUnitData..." << std::endl;

      const auto serializableId = sharedMemory.writeUnitData(unitData);

      UnBARableAINS::debug::makeIndentation(std::cout, 1);
      std::cout << "3) UnitData successfully written. Serializable ID: "
                << serializableId << std::endl;
    } catch (const std::exception &exception) {
      std::cerr << "writeUnitData failed: " << exception.what() << std::endl;

      throw;
    } catch (...) {
      std::cerr << "writeUnitData failed with an unknown exception."
                << std::endl;

      throw;
    }
  } catch (const std::system_error &e) {
    std::cerr << "UnBARableAI::writeObservationToSharedMemory failed: "
                 "System Error "
              << e.what() << " when opening the BarSharedMemory." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "UnBARableAI::writeObservationToSharedMemory failed: "
                 "Cannot open BarSharedMemory. Reason: "
              << e.what() << std::endl;

    try {
      auto path = writeSharedMemoryState();
      std::cerr << "\tWrote SharedMemory state to file" << path.string()
                << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "\tCould not write shared memory state. Reason: " << e.what()
                << std::endl;
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

  std::string shmName{c_shm_name};
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

  std::cout << "Registered unit mapping: engine ID " << engineUnitId
            << " -> custom ID " << customUnitId << std::endl;

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
