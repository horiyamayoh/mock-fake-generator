#include "Beta.h"
#include "MockBeta.h"

bool Beta::Save()
{
	if (auto* mock = ::mockfake::CurrentMock<MockBeta>())
	{
		return mock->Save();
	}

	return ::mockfake::MissingMockReturn<bool>("Beta::Save()");
}
