#include <utility>

#include "MockService.h"
#include "Service.h"

namespace sample
{

	bool Service::Run(int value)
	{
		if (auto* mock = ::mockfake::CurrentMock<MockService>())
		{
			return mock->Run(std::move(value));
		}

		return ::mockfake::MissingMockReturn<bool>("sample::Service::Run(int)");
	}

} // namespace sample
