#pragma once

#include <gmock/gmock.h>

#include "IStorage.h"

namespace sample
{

	class MockImplicitDtorIface : public ImplicitDtorIface
	{
	  public:
		MockImplicitDtorIface() = default;
		~MockImplicitDtorIface() = default;

		MOCK_METHOD(int, Run, (), (override));
	};

} // namespace sample
