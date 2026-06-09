#ifndef UNBARABLEAI_H
#define UNBARABLEAI_H

#include "OOAICallback.h"
#include <vector>

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
};

#endif // UNBARABLEAI_H