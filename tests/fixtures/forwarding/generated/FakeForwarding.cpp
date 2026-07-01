#include <utility>

#include "Forwarding.h"
#include "MockForwarding.h"

int Forwarding::Select()
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->Select();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::Select()");
}

int Forwarding::Select() const
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return static_cast<const MockForwarding&>(*mock).Select();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::Select() const");
}

int Forwarding::RefSelect() &
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->RefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::RefSelect() &");
}

int Forwarding::RefSelect() &&
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return std::move(*mock).RefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::RefSelect() &&");
}

int Forwarding::ConstRefSelect() const&
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return static_cast<const MockForwarding&>(*mock).ConstRefSelect();
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::ConstRefSelect() const&");
}

int Forwarding::MoveUnique(std::unique_ptr<int> value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->MoveUnique(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveUnique(std::unique_ptr<int>)");
}

int Forwarding::MoveString(std::string value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->MoveString(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveString(std::string)");
}

int Forwarding::MoveRValue(std::string&& value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->MoveRValue(std::move(value));
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::MoveRValue(std::string&&)");
}

int Forwarding::KeepLValue(int& value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->KeepLValue(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepLValue(int&)");
}

int Forwarding::KeepConstRef(const std::string& value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->KeepConstRef(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepConstRef(const std::string&)");
}

int Forwarding::KeepPointer(int* value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->KeepPointer(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepPointer(int*)");
}

int Forwarding::KeepConstValue(const std::string value)
{
	if (auto* mock = ::mockfake::CurrentMock<MockForwarding>())
	{
		return mock->KeepConstValue(value);
	}

	return ::mockfake::MissingMockReturn<int>("Forwarding::KeepConstValue(const std::string)");
}
