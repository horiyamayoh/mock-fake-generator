#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "MockFakeRuntime.h"

namespace
{
	TEST(MockFakeRuntimeThrowHeaderSmoke, MissingMockReturnThrows)
	{
		EXPECT_THROW(
			{
				try
				{
					(void)mockfake::MissingMockReturn<int>("MockThing::Run()");
				}
				catch (const std::runtime_error& error)
				{
					EXPECT_EQ(std::string(error.what()),
							  "mockfake: missing mock for MockThing::Run()");
					throw;
				}
			},
			std::runtime_error);

		EXPECT_THROW(mockfake::MissingMockReturn<void>("MockThing::Notify()"), std::runtime_error);
	}
} // namespace
