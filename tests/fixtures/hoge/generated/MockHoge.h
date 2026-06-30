#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD(bool, Initialize, (int, char**), ());
	MOCK_METHOD(void, Finalize, (), ());
	MOCK_METHOD(bool, DoSomething, (), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
