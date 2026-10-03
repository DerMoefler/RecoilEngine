#include "UnBARableAI.h"

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "Economy.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Map.h"
#include "Resource.h"
#include "UnBARableAI/UnBARableAIClient.h"
#include "UnBARableAI/action.h"
#include "UnBARableAI/bar_shared_memory.h"
#include "UnBARableAI/debug/dump_unit_data.hpp"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"

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
        break;
    }
    case EVENT_RELEASE: {
        // TODO: Vielleicht in sm schreiben warum gecrashed
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
                      << ", ID: " << engineUnitId
                      << ", Attacker: " << event->attacker << std::endl;
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
        if ((event->frame % 230 - 2) == 0 && event->frame > 0) {
            std::cout << "start of eventUpdate at frame: " << event->frame
                      << " ---------------------------" << std::endl;
            for (springai::Unit *unit : callback_->GetFriendlyUnits()) {
                writeObservationToSharedMemory(unit);
            }
            std::cout << "friendly units written to shared memory" << std::endl;

            for (springai::Unit *unit :
                 callback_->GetEnemyUnitsInRadarAndLos()) {
                writeObservationToSharedMemory(unit);
            }
            std::cout << "enemy units written to shared memory" << std::endl;

            for (springai::Unit *unit : callback_->GetNeutralUnits()) {
                writeObservationToSharedMemory(unit);
            }
            std::cout << "neutral units written to shared memory" << std::endl;

            UnBARableAIClient client = UnBARableAIClient();
            if (!client.HandleEventUpdate()) {
                std::cerr << "Failed to send event update: "
                          << client.GetLastError() << std::endl;
            } else {
                std::cout << "Event update sent successfully" << std::endl;
            }

            auto sharedMemory = UnBARableAINS::memory::BarSharedMemory::open(
                "/unbarable_ai_read");
            std::vector<UnBARableAINS::Action> actions =
                sharedMemory.readAllActions();
            std::cout << "Reading action at Frame: " << event->frame
                      << std::endl;

            for (const UnBARableAINS::Action &action : actions) {
                int engineUnitId = getEngineUnitId(action.unit_id);
                springai::Unit *unit = springai::WrappUnit::GetInstance(
                    action.team_id, engineUnitId);
                if (action.action_id == UnBARableAINS::ActionId::Attack) {
                    const int engineTargetUnitId =
                        getEngineUnitId(action.target_unit_id);
                    springai::Unit *enemyUnit =
                        springai::WrappUnit::GetInstance(action.team_id,
                                                         engineTargetUnitId);
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
                                  << " moving right to position ("
                                  << targetPos.x << ", " << targetPos.y << ", "
                                  << targetPos.z << ")" << std::endl;
                        break;
                    }
                    case UnBARableAINS::ActionId::MoveLeft: {
                        targetPos.x -= moveDistance;
                        std::cout << "Unit " << action.unit_id
                                  << " moving left to position (" << targetPos.x
                                  << ", " << targetPos.y << ", " << targetPos.z
                                  << ")" << std::endl;
                        break;
                    }
                    case UnBARableAINS::ActionId::MoveUp: {
                        targetPos.z += moveDistance;
                        std::cout << "Unit " << action.unit_id
                                  << " moving up to position (" << targetPos.x
                                  << ", " << targetPos.y << ", " << targetPos.z
                                  << ")" << std::endl;
                        break;
                    }
                    case UnBARableAINS::ActionId::MoveDown: {
                        targetPos.z -= moveDistance;
                        std::cout << "Unit " << action.unit_id
                                  << " moving down to position (" << targetPos.x
                                  << ", " << targetPos.y << ", " << targetPos.z
                                  << ")" << std::endl;
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
        std::cerr
            << "UnBARableAI::writeObservationToSharedMemory failed. Reason: "
            << e.what() << std::endl;
    }

    std::cout << "UnBARableAI::writeObservationToSharedMemory: Writing Unit"
              << std::endl;
    std::cout << "\tEngine Unit ID: " << unit->GetUnitId() << std::endl;
    UnBARableAINS::debug::dumpUnitData(std::cout, unitData, 1);

    auto sharedMemory =
        UnBARableAINS::memory::BarSharedMemory::open("/unbarable_ai_read");
    std::cout << "Shared memory opened for writing unit data." << std::endl;
    try {
        std::cout << "Calling writeUnitData..." << std::endl;

        const auto serializableId = sharedMemory.writeUnitData(unitData);

        std::cout << "UnitData successfully written. Serializable ID: "
                  << serializableId << std::endl;
    } catch (const std::exception &exception) {
        std::cerr << "writeUnitData failed: " << exception.what() << std::endl;

        throw;
    } catch (...) {
        std::cerr << "writeUnitData failed with an unknown exception."
                  << std::endl;

        throw;
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
