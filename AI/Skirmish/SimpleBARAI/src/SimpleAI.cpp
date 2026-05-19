#include "SimpleAI.h"
#include "ExternalAI/Interface/AISEvents.h"
#include "Economy.h"
#include "Resource.h"
#include "Unit.h"
#include "UnitDef.h"
#include "WrappUnit.h"
#include "Map.h"
#include <iostream>
#include <cstdlib>
#include <ctime>

CSimpleAI::CSimpleAI(springai::OOAICallback* callback) : callback_(callback) {
	teamId_ = callback_->GetSkirmishAIId();
	srand(time(NULL));
	std::cout << "SimpleBARAI initialized for team " << teamId_ << std::endl;
}

CSimpleAI::~CSimpleAI() {
	// Cleanup if needed
}

void CSimpleAI::HandleEvent(int topic, const void* data) {
	switch (topic) {
		case EVENT_INIT: {
			// Initialization event
			std::cout << "SimpleBARAI received INIT event" << std::endl;
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

			if (std::string(unit->GetDef()->GetName()) == "corcom")
			{
				const auto& buildOptions = unit->GetDef()->GetBuildOptions();
				std::cout << "Builder can build: " << buildOptions.size() << " units" << std::endl;
				for (springai::UnitDef* def : buildOptions) {
					if (def->IsBuilder()) {
						springai::AIFloat3 pos = unit->GetPos();
						pos.x += 200.0f;
						pos.z += 200.0f;
						unit->Build(def, pos, 0);
						std::cout << "Builder " << unit->GetDef()->GetName() << " is building unit " << def->GetName() << std::endl;
						break;
					}
				}
			}

			if (unit->GetDef()->IsBuilder() && std::string(unit->GetDef()->GetName()) != "corcom") {
				// If builder is idle, try to build 
				const auto& buildOptions = unit->GetDef()->GetBuildOptions();
				for (springai::UnitDef* def : buildOptions) {
					if (def->IsAbleToAttack() && !def->IsBuilder()) {
						springai::AIFloat3 pos = unit->GetPos();
						unit->Build(def, pos, 0);
						std::cout << "Builder(hopefully factory) " << unit->GetDef()->GetName() << " is building unit " << def->GetName() << std::endl;
						break;
					}
				}
			}
			if (unit->GetDef()->IsAbleToAttack() && unit->GetDef()->GetName() != "corcom" && unit->GetDef()->GetName() != "corlab") {
				// If combat unit is idle, try to attack a random enemy
				std::vector<springai::Unit*> enemies = callback_->GetEnemyUnits();
				if (!enemies.empty()) {
					springai::Unit* target = enemies[rand() % enemies.size()];
					unit->Attack(target);
					std::cout << "Combat unit " << unit->GetDef()->GetName() << " is attacking enemy unit " << target->GetDef()->GetName() << std::endl;
				}
			}

			
			break;
		}
		case EVENT_UNIT_IDLE: {
			const SUnitIdleEvent* event = static_cast<const SUnitIdleEvent*>(data);

			springai::Unit* unit = springai::WrappUnit::GetInstance(teamId_, event->unit);

			comInUse_ = false;
			
			
			if (unit->GetDef()->IsBuilder() && std::string(unit->GetDef()->GetName()) != "corcom") {
				// If builder is idle, try to build 
				const auto& buildOptions = unit->GetDef()->GetBuildOptions();
				for (springai::UnitDef* def : buildOptions) {
					if (def->IsAbleToAttack() && !def->IsBuilder()) {
						springai::AIFloat3 pos = unit->GetPos();
						unit->Build(def, pos, 0);
						std::cout << "Builder(hopefully factory) " << unit->GetDef()->GetName() << " is building unit " << def->GetName() << std::endl;
						break;
					}
				}
			}
			if (unit->GetDef()->IsAbleToAttack() && unit->GetDef()->GetName() != "corcom" && unit->GetDef()->GetName() != "corlab") {
				// If combat unit is idle, try to attack a random enemy
				std::vector<springai::Unit*> enemies = callback_->GetEnemyUnits();
				if (!enemies.empty()) {
					springai::Unit* target = enemies[rand() % enemies.size()];
					unit->Attack(target);
					std::cout << "Combat unit " << unit->GetDef()->GetName() << " is attacking enemy unit (idle)" << target->GetDef()->GetName() << std::endl;
				}
			}

			break;

		}
		case EVENT_UPDATE: {
			const SUpdateEvent* event = static_cast<const SUpdateEvent*>(data);
			for (springai::Unit* unit :  callback_->GetFriendlyUnits())
			{
				if (std::string(unit->GetDef()->GetName()) == "corcom" && unit->GetCurrentCommands().empty() && !comInUse_)
				{
					for (springai::Resource* resource : callback_->GetResources())
					{
						if (resource->GetResourceId() == 1 && callback_->GetEconomy()->GetCurrent(resource) < 500) {
							// If low on energy, try to build a solar collector
							const auto& buildOptions = unit->GetDef()->GetBuildOptions();
							std::cout << "low Engergy" << std::endl;
							for (springai::UnitDef* def : buildOptions) {
								if (std::string(def->GetName()) == "corwin") {
									springai::AIFloat3 pos = unit->GetPos();
									pos.x -= 200.0f;
									pos.z += 200.0f;
									unit->Build(def, pos, 0);
									comInUse_ = true;
									std::cout << "Builder " << unit->GetDef()->GetName() << " is building wind farm " << def->GetName() << std::endl;
									break;
								}
							}
						}

						if (resource->GetResourceId() == 0 && callback_->GetEconomy()->GetCurrent(resource) < 300) {
							// If low on metal, try to build a metal extractor
							std::cout << "low Metal" << std::endl;
							const auto& buildOptions = unit->GetDef()->GetBuildOptions();
							for (springai::UnitDef* def : buildOptions) {
								if (std::string(def->GetName()) == "cormex") {
									springai::AIFloat3 pos = callback_->GetMap()->GetResourceMapSpotsNearest(resource, unit->GetPos());
									unit->Build(def, pos, 0);
									comInUse_ = true;
									std::cout << "Builder " << unit->GetDef()->GetName() << " is building metal extractor " << def->GetName() << std::endl;
									break;
								}
							}
						}
					}
				}
				
			}
			
			// Periodic update
			break;
		}
		default:
			break;
	}
}