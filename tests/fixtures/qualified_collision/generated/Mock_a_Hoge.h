#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "a/Hoge.h"

namespace a
{

	class MockHoge
	{
	  public:
		MockHoge() = default;
		~MockHoge() = default;

		MOCK_METHOD(bool, Ping, (), ());
	};

	using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;

} // namespace a
