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

	class ConcreteVirtual
	{
	  public:
		virtual ~ConcreteVirtual() = default;

		virtual int Run()
		{
			return -1;
		}

		virtual int LoadCount() const
		{
			return 0;
		}

		int Helper() const
		{
			return 42;
		}
	};
} // namespace sample
