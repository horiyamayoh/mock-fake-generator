#pragma once

#include <gmock/gmock.h>

#include "Hoge.h"
#include "MockFakeRuntime.h"

class MockHoge
{
  public:
	MockHoge() = default;
	~MockHoge() = default;

	MOCK_METHOD((std::pair<bool, int>), GetPair, (), ());
	MOCK_METHOD(void, SetMap, ((std::map<int, double>)), ());
	MOCK_METHOD((std::pair<int, std::pair<int, int>>), Nest, (), ());
};

using ScopedMockHoge = ::mockfake::ScopedMock<MockHoge>;
