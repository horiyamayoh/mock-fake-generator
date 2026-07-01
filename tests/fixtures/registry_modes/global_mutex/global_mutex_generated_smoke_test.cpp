#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "GlobalWorker.h"
#include "MockGlobalWorker.h"

namespace
{
	TEST(GlobalMutexGeneratedSmoke, WorkerThreadDelegatesToScopedMock)
	{
		MockGlobalWorker mock;
		const ScopedMockGlobalWorker scoped_mock(mock);
		EXPECT_CALL(mock, Run()).WillOnce(::testing::Return(true));

		bool result = false;
		std::thread worker(
			[&]
			{
				GlobalWorker product;
				result = product.Run();
			});
		worker.join();

		EXPECT_TRUE(result);
	}
} // namespace
