#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Unsupported.h"

namespace negative
{

	class MockUnsafeStaticData
	{
	  public:
		MockUnsafeStaticData() = default;
		~MockUnsafeStaticData() = default;

		MOCK_METHOD(bool, Touch, (), ());
	};

	using ScopedMockUnsafeStaticData = ::mockfake::ScopedMock<MockUnsafeStaticData>;

} // namespace negative
