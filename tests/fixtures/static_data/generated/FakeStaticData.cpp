#include "MockStaticData.h"
#include "StaticData.h"

int StaticData::count{};

const int StaticData::limit{};

int StaticData::ReadCount()
{
	if (auto* mock = ::mockfake::CurrentMock<MockStaticData>())
	{
		return mock->ReadCount();
	}

	return ::mockfake::MissingMockReturn<int>("StaticData::ReadCount()");
}
