#include <utility>

#include "MockService.h"
#include "Service.h"

namespace sample
{

	bool Service::Run(int value)
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockService>())
		{
			return mockfake_current_mock->Run(std::move(value));
		}

		return ::mockfake::MissingMockReturn<bool>("sample::Service::Run(int)");
	}

} // namespace sample
