#include "MockUnsafeSpecial.h"
#include "Unsupported.h"

namespace negative
{

	bool UnsafeSpecial::Touch()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsafeSpecial>())
		{
			return mockfake_current_mock->Touch();
		}

		return ::mockfake::MissingMockReturn<bool>("negative::UnsafeSpecial::Touch()");
	}

} // namespace negative
