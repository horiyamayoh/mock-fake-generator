#include "MockStaticData.h"
#include "StaticData.h"

int StaticData::count{};

const int StaticData::limit{};

int StaticData::ReadCount()
{
	if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockStaticData>())
	{
		return mockfake_current_mock->ReadCount();
	}

	return ::mockfake::MissingMockReturn<int>("StaticData::ReadCount()");
}
