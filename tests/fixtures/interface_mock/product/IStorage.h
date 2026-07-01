#pragma once

#include <string>

namespace sample
{
	class IStorage
	{
	  public:
		virtual ~IStorage() = default;

		virtual bool Save(const std::string& key, std::string value) = 0;
		virtual int LoadCount() const noexcept = 0;
	};

	class ImplicitDtorIface
	{
	  public:
		virtual int Run() = 0;
	};
} // namespace sample
