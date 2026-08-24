#include "UnBARableAI.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Economy.h"
#include "Resource.h"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "Map.h"
#include "UnBARableAI/UnBARableAIClient.h"
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
			myUnits_.push_back(event->unit);
			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);
			std::cout << "Unit created: " << event->unit << std::endl;
			break;
		}
		case EVENT_UNIT_FINISHED: {
			const SUnitFinishedEvent* event = static_cast<const SUnitFinishedEvent*>(data);
			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);
			break;
		}
		case EVENT_UNIT_IDLE: {
			const SUnitIdleEvent* event = static_cast<const SUnitIdleEvent*>(data);

			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);

			break;

		}
		case EVENT_UPDATE: {
			const SUpdateEvent* event = static_cast<const SUpdateEvent*>(data);
			if (event->frame % 230 == 0) {
				for (springai::Unit* unit : callback_->GetFriendlyUnits()){
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
					// TODO: Observation in shared memory schreiben
				}
				UnBARableAIClient client = UnBARableAIClient();
				if (!client.HandleEventUpdate()) {
						std::cerr << "Failed to send event update: " << client.GetLastError() << std::endl;
					} else {
						std::cout << "Event update sent successfully" << std::endl;
					}
				// TODO: Action aus shared memory auslesen
				for (int i = 0; i < 5; ++i) {
					int unitId = 2; // TODO: Replace with actual unit ID from action
					springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, unitId);
					int actionId = 1; // TODO: Replace with actual action ID from action
					switch (actionId)
					{
						case 1: {//move right
							springai::AIFloat3 currentPos = unit->GetPos();
							springai::AIFloat3 targetPos = springai::AIFloat3(currentPos.x + 10.0f, currentPos.y, currentPos.z); 
							unit->MoveTo(targetPos);
							break;
						}
						
						case 2: {//move left
							springai::AIFloat3 currentPos = unit->GetPos();
							springai::AIFloat3 targetPos = springai::AIFloat3(currentPos.x - 10.0f, currentPos.y, currentPos.z); 
							unit->MoveTo(targetPos);
							break;
						}
						
						case 3: {//move up
							springai::AIFloat3 currentPos = unit->GetPos();
							springai::AIFloat3 targetPos = springai::AIFloat3(currentPos.x, currentPos.y, currentPos.z + 10.0f); 
							unit->MoveTo(targetPos);
							break;
						}

						case 4: {//move down
							springai::AIFloat3 currentPos = unit->GetPos();
							springai::AIFloat3 targetPos = springai::AIFloat3(currentPos.x, currentPos.y, currentPos.z - 10.0f); 
							unit->MoveTo(targetPos);
							break;
						}

						case 5: {//attack 
							int enemyUnitId = 3; // TODO: Replace with actual enemy unit ID from action
							springai::Unit* enemyUnit = springai::WrappUnit::GetInstance(teamId_, enemyUnitId);
							unit->Attack(enemyUnit);
						}
					}
				}
				
				
			}
			
			break;
		}
		default:
			break;
	}
}