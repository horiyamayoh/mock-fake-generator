#pragma once

#include <string>

#include "model/GeneratedFile.h"

namespace mockfakegen
{
	[[nodiscard]] std::string BuildThreadLocalRuntimeHeaderContent();
	[[nodiscard]] GeneratedFile MakeThreadLocalRuntimeHeader();
} // namespace mockfakegen
