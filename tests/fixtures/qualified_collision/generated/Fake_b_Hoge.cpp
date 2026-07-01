#include "Mock_b_Hoge.h"
#include "b/Hoge.h"

namespace b
{

	bool Hoge::Ping()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mockfake_current_mock->Ping();
		}

		return ::mockfake::MissingMockReturn<bool>("b::Hoge::Ping()");
	}

} // namespace b
