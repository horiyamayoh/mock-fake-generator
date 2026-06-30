#pragma once

#include <map>
#include <utility>

class Hoge
{
  public:
	std::pair<bool, int> GetPair();
	void SetMap(std::map<int, double> value);
	std::pair<int, std::pair<int, int>> Nest();
};
