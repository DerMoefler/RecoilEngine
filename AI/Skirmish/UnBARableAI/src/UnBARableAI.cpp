#include "UnBARableAI.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Economy.h"
#include "Resource.h"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "Map.h"
#include "UnBARableAI/UnBARableAIClient.h"
#include "UnBARableAI/unit_data.h"
#include "UnBARableAI/action.h"
#include <iostream>
#include <cstdlib>
#include <ctime>

UnBARableAI::UnBARableAI(springai::OOAICallback* callback) : 
	callback_(callback)
{
	teamId_ = callback_->GetSkirmishAIId();
	srand(time(NULL));
	std::cout << "UnBARableAI initialized for team " << teamId_ << std::endl;
}

UnBARableAI::~UnBARableAI() {
	// Cleanup if needed
}

void UnBARableAI::HandleEvent(int topic, const void* data) {
	switch (topic) {
		case EVENT_INIT: {
			// Initialization event
			std::cout << "UnBARableAI received INIT event" << std::endl;
			break;
		}
		case EVENT_UNIT_CREATED: {
			const SUnitCreatedEvent* event = static_cast<const SUnitCreatedEvent*>(data);
			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);
			std::cout << "Unit created, name : " << unit->GetDef()->GetName() << ", ID: " << unit->GetUnitId() << std::endl;
			break;
		}
		case EVENT_UNIT_FINISHED: {
			const SUnitFinishedEvent* event = static_cast<const SUnitFinishedEvent*>(data);
			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);
			break;
		}
		case EVENT_UNIT_DESTROYED: {
			const SUnitDestroyedEvent* event = static_cast<const SUnitDestroyedEvent*>(data);
			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);
			std::cout << "Unit destroyed, name : " << unit->GetDef()->GetName() << ", ID: " << unit->GetUnitId() << ", Attacker: " << event->attacker << std::endl;
			break;
		}
		case EVENT_UNIT_IDLE: {
			const SUnitIdleEvent* event = static_cast<const SUnitIdleEvent*>(data);

			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);

			break;

		}
		case EVENT_UPDATE: {
			const SUpdateEvent* event = static_cast<const SUpdateEvent*>(data);
			if ((event->frame % 230 - 2) == 0 && event->frame > 0) {
				std::cout << "start of eventUpdate at frame: " << event->frame << " ---------------------------" << std::endl;
				for (springai::Unit* unit : callback_->GetFriendlyUnits()){
					writeObservationToSharedMemory(unit);
				}
				std::cout << "friendly units written to shared memory" << std::endl;
				
				for (springai::Unit* unit : callback_->GetEnemyUnitsInRadarAndLos()){
					writeObservationToSharedMemory(unit);
				}
				std::cout << "enemy units written to shared memory" << std::endl;

				for (springai::Unit* unit : callback_->GetNeutralUnits()){
					writeObservationToSharedMemory(unit);
				}
				std::cout << "neutral units written to shared memory" << std::endl;

				UnBARableAIClient client = UnBARableAIClient();
				if (!client.HandleEventUpdate()) {
						std::cerr << "Failed to send event update: " << client.GetLastError() << std::endl;
					} else {
						std::cout << "Event update sent successfully" << std::endl;
					}
				// TODO: Action aus shared memory auslesen
				std::cout << "Reading action at Frame: " << event->frame << std::endl;
				UnBARableAINS::Action action = {
					2, // unit_id
					teamId_, // team_id
					0, // ally_team_id
					UnBARableAINS::ActionId::MoveRight, // action_id
					3  // target_unit_id (only used for attack action)
				}; // TODO: Replace with actual action from shared memory
				
				while (false) { // TODO: Replace with something to go through all actions
					springai::Unit* unit = springai::WrappUnit::GetInstance(action.team_id, action.unit_id);
					if (action.action_id == UnBARableAINS::ActionId::Attack) {
						springai::Unit* enemyUnit = springai::WrappUnit::GetInstance(action.team_id, action.target_unit_id);
						unit->Attack(enemyUnit);
						std::cout << "Unit " << action.unit_id << " attacking unit " << action.target_unit_id << std::endl;
					}
					else {
						springai::AIFloat3 currentPos = unit->GetPos();
						springai::AIFloat3 targetPos = currentPos;
						float moveDistance = 10.0f; 
						switch (action.action_id) {
							case UnBARableAINS::ActionId::MoveRight: {
								targetPos.x += moveDistance;
								std::cout << "Unit " << action.unit_id << " moving right to position (" << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")" << std::endl;
								break;
							}
							case UnBARableAINS::ActionId::MoveLeft: {
								targetPos.x -= moveDistance;
								std::cout << "Unit " << action.unit_id << " moving left to position (" << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")" << std::endl;
								break;
							}
							case UnBARableAINS::ActionId::MoveUp: {
								targetPos.z += moveDistance;
								std::cout << "Unit " << action.unit_id << " moving up to position (" << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")" << std::endl;
								break;
							}
							case UnBARableAINS::ActionId::MoveDown: {
								targetPos.z -= moveDistance;
								std::cout << "Unit " << action.unit_id << " moving down to position (" << targetPos.x << ", " << targetPos.y << ", " << targetPos.z << ")" << std::endl;
								break;
							}
						}
						unit->MoveTo(targetPos);
					}
				}
				std::cout << "end of eventUpdate at frame: " << event->frame << " ---------------------------" << std::endl;
			}
			break;
		}
		default:
			break;
	}
}

void UnBARableAI::writeObservationToSharedMemory(springai::Unit* unit) {
	int unitId = unit->GetUnitId();
	int unitDefId = unit->GetDef()->GetUnitDefId();
	std::string unitName = unit->GetDef()->GetName();
	std::string humanName = unit->GetDef()->GetHumanName();
	int teamId = unit->GetTeam();
	int allyTeamId = unit->GetAllyTeam();
	float health = unit->GetHealth();
	float maxHealth = unit->GetMaxHealth();
	float posX = unit->GetPos().x;
	float posY = unit->GetPos().y;
	float posZ = unit->GetPos().z;
	float losRadius = unit->GetDef()->GetLosRadius();
	float airLosRadius = unit->GetDef()->GetAirLosRadius();
	bool isDead = (health <= 0.0f);
	bool beingBuilt = (unit->GetBuildProgress() < 1.0f);
	float buildProgress = unit->GetBuildProgress();
	float captureProgress = unit->GetCaptureProgress();
	float paralyzeDamage = unit->GetParalyzeDamage();
	std::cout << unitId << unitName << humanName << teamId << allyTeamId << health << maxHealth <<  posX << posY << posZ << "), LOS Radius: " << losRadius << ", Air LOS Radius: " << airLosRadius << ", Is Dead: " << isDead << ", Being Built: " << beingBuilt << ", Build Progress: " << buildProgress << ", Capture Progress: " << captureProgress << ", Paralyze Damage: " << paralyzeDamage << std::endl;
	UnBARableAINS::unit::UnitData unitData = {
		unitId,
		unitDefId,
		unitName,
		humanName,
		teamId,
		allyTeamId,
		health,
		maxHealth,
		posX,
		posY,
		posZ,
		losRadius,
		airLosRadius,
		isDead,
		beingBuilt,
		buildProgress,
		captureProgress,
		paralyzeDamage
	};
	std::cout << "UnitData for unit " << unitId << " created." << std::endl;
	// TODO: Observation in shared memory schreiben
}