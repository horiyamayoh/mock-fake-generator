#include "Mock_a_Hoge.h"
#include "a/Hoge.h"

namespace a
{

	bool Hoge::Ping()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mockfake_current_mock->Ping();
		}

		return ::mockfake::MissingMockReturn<bool>("a::Hoge::Ping()");
	}

} // namespace a
