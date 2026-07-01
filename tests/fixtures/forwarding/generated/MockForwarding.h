#pragma once

#include <gmock/gmock.h>

#include "Forwarding.h"
#include "MockFakeRuntime.h"

class MockForwarding
{
  public:
	MockForwarding() = default;
	~MockForwarding() = default;

	MOCK_METHOD(int, Select, (), ());
	MOCK_METHOD(int, Select, (), (const));
	MOCK_METHOD(int, RefSelect, (), (ref(&)));
	MOCK_METHOD(int, RefSelect, (), (ref(&&)));
	MOCK_METHOD(int, ConstRefSelect, (), (const, ref(&)));
	MOCK_METHOD(int, MoveUnique, (std::unique_ptr<int>), ());
	MOCK_METHOD(int, MoveString, (std::string), ());
	MOCK_METHOD(int, MoveRValue, (std::string&&), ());
	MOCK_METHOD(int, KeepLValue, (int&), ());
	MOCK_METHOD(int, KeepConstRef, (const std::string&), ());
	MOCK_METHOD(int, KeepPointer, (int*), ());
	MOCK_METHOD(int, KeepConstValue, (const std::string), ());
};

using ScopedMockForwarding = ::mockfake::ScopedMock<MockForwarding>;
