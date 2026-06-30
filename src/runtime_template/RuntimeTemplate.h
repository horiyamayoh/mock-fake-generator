#pragma once

#include <string>

#include "Config.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	[[nodiscard]] std::string BuildThreadLocalRuntimeHeaderContent();
	[[nodiscard]] std::string BuildGlobalMutexRuntimeHeaderContent();
	[[nodiscard]] std::string BuildSharedOwnerRuntimeHeaderContent();
	[[nodiscard]] GeneratedFile MakeThreadLocalRuntimeHeader();
	[[nodiscard]] GeneratedFile MakeGlobalMutexRuntimeHeader();
	[[nodiscard]] GeneratedFile MakeSharedOwnerRuntimeHeader();
	[[nodiscard]] GeneratedFile MakeRuntimeHeader(RegistryMode registry_mode);
} // namespace mockfakegen
