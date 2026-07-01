#include <memory>
#include <optional>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MockSharedWorker.h"
#include "SharedWorker.h"

namespace
{
	TEST(SharedOwnerGeneratedSmoke, WorkerThreadDelegatesAndKeepsMockAliveDuringCall)
	{
		std::optional<ScopedMockSharedWorker> scoped_mock;
		auto mock = std::make_shared<MockSharedWorker>();
		std::weak_ptr<MockSharedWorker> weak_mock = mock;
		scoped_mock.emplace(mock);

		EXPECT_CALL(*mock, Run())
			.WillOnce(
				[&]
				{
					scoped_mock.reset();
					mock.reset();
					EXPECT_FALSE(weak_mock.expired());
					return 7;
				});

		int result = 0;
		std::thread worker(
			[&]
			{
				SharedWorker product;
				result = product.Run();
			});
		worker.join();

		EXPECT_EQ(result, 7);
		EXPECT_FALSE(mockfake::CurrentMock<MockSharedWorker>());
		EXPECT_TRUE(weak_mock.expired());
	}
} // namespace
