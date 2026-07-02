#include <utility>

#include "MockUnsupportedSurface.h"
#include "Unsupported.h"

namespace negative
{

	bool UnsupportedSurface::Supported(int value)
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsupportedSurface>())
		{
			return mockfake_current_mock->Supported(std::move(value));
		}

		return ::mockfake::MissingMockReturn<bool>("negative::UnsupportedSurface::Supported(int)");
	}

	int UnsupportedSurface::Trailing()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsupportedSurface>())
		{
			return mockfake_current_mock->Trailing();
		}

		return ::mockfake::MissingMockReturn<int>("negative::UnsupportedSurface::Trailing()");
	}

	int UnsupportedSurface::Marked()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsupportedSurface>())
		{
			return mockfake_current_mock->Marked();
		}

		return ::mockfake::MissingMockReturn<int>("negative::UnsupportedSurface::Marked()");
	}

	int UnsupportedSurface::GnuMarked()
	{
		if (auto* mockfake_current_mock = ::mockfake::CurrentMock<MockUnsupportedSurface>())
		{
			return mockfake_current_mock->GnuMarked();
		}

		return ::mockfake::MissingMockReturn<int>("negative::UnsupportedSurface::GnuMarked()");
	}

} // namespace negative
