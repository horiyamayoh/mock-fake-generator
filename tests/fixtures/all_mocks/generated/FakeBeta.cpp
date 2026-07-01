#include "Beta.h"
#include "MockBeta.h"

bool Beta::Save()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockBeta>())
	{
		return mockfake_current_mock->Save();
	}

	return ::mockfake::MissingMockReturn<bool>("Beta::Save()");
}
