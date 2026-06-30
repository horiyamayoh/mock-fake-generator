#pragma once

#include <string>

#include "Config.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	[[nodiscard]] std::string BuildThreadLocalRuntimeHeaderContent();
	[[nodiscard]] std::string BuildGlobalMutexRuntimeHeaderContent();
	[[nodiscard]] GeneratedFile MakeThreadLocalRuntimeHeader();
	[[nodiscard]] GeneratedFile MakeGlobalMutexRuntimeHeader();
	[[nodiscard]] GeneratedFile MakeRuntimeHeader(RegistryMode registry_mode);
} // namespace mockfakegen
