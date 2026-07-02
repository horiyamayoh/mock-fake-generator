#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "Unsupported.h"

namespace negative
{

	class MockUnsupportedSurface
	{
	  public:
		MockUnsupportedSurface() = default;
		~MockUnsupportedSurface() = default;

		MOCK_METHOD(bool, Supported, (int), ());
		MOCK_METHOD(int, Trailing, (), ());
		MOCK_METHOD(int, Marked, (), ());
		MOCK_METHOD(int, GnuMarked, (), ());
	};

	using ScopedMockUnsupportedSurface = ::mockfake::ScopedMock<MockUnsupportedSurface>;

} // namespace negative
