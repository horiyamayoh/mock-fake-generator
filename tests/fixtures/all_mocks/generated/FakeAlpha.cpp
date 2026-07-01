#include "Alpha.h"
#include "MockAlpha.h"

int Alpha::Get()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockAlpha>())
	{
		return mockfake_current_mock->Get();
	}

	return ::mockfake::MissingMockReturn<int>("Alpha::Get()");
}
