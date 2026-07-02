#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Unsupported.h"

namespace negative
{

	class MockUnsafeSpecial
	{
	  public:
		MockUnsafeSpecial() = default;
		~MockUnsafeSpecial() = default;

		MOCK_METHOD(bool, Touch, (), ());
	};

	using ScopedMockUnsafeSpecial = ::mockfake::ScopedMock<MockUnsafeSpecial>;

} // namespace negative
