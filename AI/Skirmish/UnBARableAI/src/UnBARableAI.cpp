#include "UnBARableAI.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Economy.h"
#include "Resource.h"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "Map.h"
#include "unit_data/unit_data.h"
#include "engine_bridge/engine_bridge.h"
#include <iostream>
#include <cstdlib>
#include <ctime>

UnBARableAI::UnBARableAI(springai::OOAICallback* callback) : 
	callback_(callback), 
	engineBridge_(UnBARableAINS::EngineBridge()) 
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
			if (event->frame % 30 == 0) {
				for (springai::Unit* unit : callback_->GetFriendlyUnits()){
					UnBARableAINS::unit::UnitData unitData{
						unit->GetHealth(),
						// #TODO: fill unitdata with more info
					};
					engineBridge_.writeUnitDataInMemory(unitData);
				}
				
			}
			
			break;
		}
		default:
			break;
	}
}