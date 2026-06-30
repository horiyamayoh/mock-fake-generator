#include "Mock_a_Hoge.h"
#include "a/Hoge.h"

namespace a
{

	bool Hoge::Ping()
	{
		if (auto* mock = ::mockfake::CurrentMock<MockHoge>())
		{
			return mock->Ping();
		}

		return ::mockfake::MissingMockReturn<bool>("a::Hoge::Ping()");
	}

} // namespace a
