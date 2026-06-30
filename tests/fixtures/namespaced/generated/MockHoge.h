#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

namespace app::core
{

	class MockHoge
	{
	  public:
		MockHoge() = default;
		~MockHoge() = default;

		MOCK_METHOD(bool, DoSomething, (), ());
	};

	using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;

} // namespace app::core
