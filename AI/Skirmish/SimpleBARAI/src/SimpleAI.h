#ifndef SIMPLEAI_H
#define SIMPLEAI_H

#include "OOAICallback.h"
#include <vector>

class CSimpleAI {
public:
	CSimpleAI(springai::OOAICallback* callback);
	~CSimpleAI();

	void HandleEvent(int topic, const void* data);

private:
	springai::OOAICallback* callback_;
	std::vector<int> myUnits_;
	int teamId_;
	bool comInUse_ = true;
};

#endif // SIMPLEAI_H