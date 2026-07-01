#include "Hoge.h"
#include "MockHoge.h"

namespace app::core
{

	bool Hoge::DoSomething()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mockfake_current_mock->DoSomething();
		}

		return ::mockfake::MissingMockReturn<bool>("app::core::Hoge::DoSomething()");
	}

} // namespace app::core
