#include "Hoge.h"
#include "MockHoge.h"

namespace app::core
{

	bool Hoge::DoSomething()
	{
		if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mock->DoSomething();
		}

		return ::mockfake::MissingMockReturn<bool>("app::core::Hoge::DoSomething()");
	}

} // namespace app::core
