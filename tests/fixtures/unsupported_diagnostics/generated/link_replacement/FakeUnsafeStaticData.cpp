#include "MockUnsafeStaticData.h"
#include "Unsupported.h"

namespace negative
{

	bool UnsafeStaticData::Touch()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsafeStaticData>())
		{
			return mockfake_current_mock->Touch();
		}

		return ::mockfake::MissingMockReturn<bool>("negative::UnsafeStaticData::Touch()");
	}

} // namespace negative
