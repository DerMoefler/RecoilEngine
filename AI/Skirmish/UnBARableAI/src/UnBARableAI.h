#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <vector>
#include "engine_bridge/engine_bridge.h"

class UnBARableAI {
public:
	UnBARableAI(springai::OOAICallback* callback);
	~UnBARableAI();

	void HandleEvent(int topic, const void* data);

private:
	springai::OOAICallback* callback_;
	std::vector<int> myUnits_;
	int teamId_;
	bool comInUse_ = true;
	UnBARableAINS::EngineBridge engineBridge_;
};

#endif // UNBARABLEAI_H