#include <iostream>

#include "Config.h"

int main(int argc, const char* const* argv)
{
	return mockfakegen::RunCli(argc, argv, std::cout, std::cerr);
}
