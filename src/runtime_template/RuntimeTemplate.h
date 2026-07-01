#pragma once

#include <string>

#include "Config.h"
#include "model/GeneratedFile.h"

namespace mockfakegen
{
	[[nodiscard]] std::string
	BuildThreadLocalRuntimeHeaderContent(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] std::string
	BuildGlobalMutexRuntimeHeaderContent(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] std::string
	BuildSharedOwnerRuntimeHeaderContent(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] GeneratedFile
	MakeThreadLocalRuntimeHeader(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] GeneratedFile
	MakeGlobalMutexRuntimeHeader(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] GeneratedFile
	MakeSharedOwnerRuntimeHeader(FallbackPolicy fallback_policy = FallbackPolicy::Abort);
	[[nodiscard]] GeneratedFile
	MakeRuntimeHeader(RegistryMode registry_mode,
					  FallbackPolicy fallback_policy = FallbackPolicy::Abort);
} // namespace mockfakegen
