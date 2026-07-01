#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Supported.h"

namespace app::v1
{

	class MockSupported
	{
	  public:
		MockSupported() = default;
		~MockSupported() = default;

		MOCK_METHOD(Count, CountItems, (Mode), (const, noexcept));
		MOCK_METHOD(Count, Size, (), (const));
		MOCK_METHOD(Ratio, Scale, (Ratio), ());
		MOCK_METHOD(Mode, DefaultMode, (), ());
	};

	using ScopedMockSupported = ::mockfake::ScopedMock<MockSupported>;

} // namespace app::v1
