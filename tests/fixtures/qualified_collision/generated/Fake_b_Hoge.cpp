#include "Mock_b_Hoge.h"
#include "b/Hoge.h"

namespace b
{

	bool Hoge::Ping()
	{
		if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mock->Ping();
		}

		return ::mockfake::MissingMockReturn<bool>("b::Hoge::Ping()");
	}

} // namespace b
