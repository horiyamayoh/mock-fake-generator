#include <utility>

#include "MockSupported.h"
#include "Supported.h"

namespace app::v1
{

	Count Supported::CountItems(Mode mode) const noexcept
	{
		if (auto* mock = ::mockfake::CurrentMock<MockSupported>())
		{
			return static_cast<const MockSupported&>(*mock).CountItems(std::move(mode));
		}

		return ::mockfake::MissingMockReturn<Count>(
			"app::v1::Supported::CountItems(Mode) const noexcept");
	}

	Ratio Supported::Scale(Ratio value)
	{
		if (auto* mock = ::mockfake::CurrentMock<MockSupported>())
		{
			return mock->Scale(std::move(value));
		}

		return ::mockfake::MissingMockReturn<Ratio>("app::v1::Supported::Scale(Ratio)");
	}

	Mode Supported::DefaultMode()
	{
		if (auto* mock = ::mockfake::CurrentMock<MockSupported>())
		{
			return mock->DefaultMode();
		}

		return ::mockfake::MissingMockReturn<Mode>("app::v1::Supported::DefaultMode()");
	}

} // namespace app::v1
