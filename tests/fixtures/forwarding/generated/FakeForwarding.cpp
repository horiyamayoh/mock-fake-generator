#include <utility>

#include "Forwarding.h"
#include "MockForwarding.h"

int Forwarding::Select()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->Select();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::Select()");
}

int Forwarding::Select() const
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return static_cast<const MockForwarding&>(*mockfake_current_mock).Select();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::Select() const");
}

int Forwarding::RefSelect() &
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->RefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::RefSelect() &");
}

int Forwarding::RefSelect() &&
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return std::move(*mockfake_current_mock).RefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::RefSelect() &&");
}

int Forwarding::ConstRefSelect() const&
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return static_cast<const MockForwarding&>(*mockfake_current_mock).ConstRefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::ConstRefSelect() const&");
}

int Forwarding::MoveUnique(std::unique_ptr<int> value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->MoveUnique(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveUnique(std::unique_ptr<int>)");
}

int Forwarding::MoveString(std::string value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->MoveString(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveString(std::string)");
}

int Forwarding::MoveRValue(std::string&& value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->MoveRValue(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveRValue(std::string&&)");
}

int Forwarding::KeepLValue(int& value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->KeepLValue(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepLValue(int&)");
}

int Forwarding::KeepConstRef(const std::string& value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->KeepConstRef(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepConstRef(const std::string&)");
}

int Forwarding::KeepPointer(int* value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->KeepPointer(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepPointer(int*)");
}

int Forwarding::KeepConstValue(const std::string value)
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mockfake_current_mock->KeepConstValue(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepConstValue(const std::string)");
}
