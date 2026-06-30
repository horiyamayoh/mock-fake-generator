#include "Alpha.h"
#include "MockAlpha.h"

int Alpha::Get()
{
	if (auto* mock = ::mockfake::CurrentMock<MockAlpha>())
	{
		return mock->Get();
	}

	return ::mockfake::MissingMockReturn<int>("Alpha::Get()");
}
