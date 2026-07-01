#pragma once

namespace app::v1
{
	using Count = int;
	typedef double Ratio;

	enum class Mode
	{
		Fast,
		Slow,
	};

	class Supported
	{
	  public:
		friend bool FriendProbe(const Supported&);

		Count CountItems(Mode mode) const noexcept;
		Ratio Scale(Ratio value);
		static Mode DefaultMode();
	};
} // namespace app::v1
