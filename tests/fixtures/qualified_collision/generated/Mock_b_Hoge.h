#pragma once

#include <gmock/gmock.h>

#include "MockFakeRuntime.h"
#include "b/Hoge.h"

namespace b
{

	class MockHoge
	{
	  public:
		MockHoge() = default;
		~MockHoge() = default;

		MOCK_METHOD(bool, Ping, (), ());
	};

	using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;

} // namespace b
