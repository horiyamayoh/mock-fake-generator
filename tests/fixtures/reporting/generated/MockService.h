#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Service.h"

namespace sample
{

	class MockService
	{
	  public:
		MockService() = default;
		~MockService() = default;

		MOCK_METHOD(bool, Run, (int), ());
	};

	using ScopedMockService = ::mockfake::ScopedMock<MockService>;

} // namespace sample
