#include <utility>

#include "Hoge.h"
#include "MockHoge.h"

bool Hoge::Open(const std::string& path, int flags)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Open(path, std::move(flags));
	}

	return ::mockfake::MissingMockReturn<bool>("Hoge::Open(const std::string&, int)");
}

int Hoge::Retry(int count, const char* label)
{
	if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
	{
		return mock->Retry(std::move(count), label);
	}

	return ::mockfake::MissingMockReturn<int>("Hoge::Retry(int, const char*)");
}
