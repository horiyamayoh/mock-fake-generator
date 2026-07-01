#pragma once

#include <gmock/gmock.h>

#include "IStorage.h"

namespace sample
{

	class MockConcreteVirtual : public ConcreteVirtual
	{
	  public:
		MockConcreteVirtual() = default;
		~MockConcreteVirtual() override = default;

		MOCK_METHOD(int, Run, (), (override));
		MOCK_METHOD(int, LoadCount, (), (const, override));
	};

} // namespace sample
