#pragma once

class StaticData
{
  public:
	static int count;
	static const int limit;
	inline static int inline_count = 3;
	static constexpr int cached = 9;

	static int ReadCount();
};
