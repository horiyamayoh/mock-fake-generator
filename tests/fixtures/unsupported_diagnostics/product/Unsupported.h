#pragma once

#include <compare>

#define MOCKFAKEGEN_UNSUPPORTED_METHOD(name) bool name();

namespace negative
{
	template <class T>
	concept Number = requires(T value) { value + 1; };

	template <class T>
	class Box
	{
	  public:
		T value;
	};

	template <>
	class Box<int>
	{
	  public:
		bool Specialized();
	};

	template <class T>
	class PartialBox
	{
	};

	template <class T>
	class PartialBox<T*>
	{
	  public:
		bool Partial();
	};

	class UnsupportedSurface
	{
	  public:
		bool Supported(int value);

		template <class T>
		T Convert(T value);

		void InlineBody() {}
		void OutOfClassInline();
		auto TrailingReturn() -> int;
		auto AutoValue();
		decltype(auto) Deduced();
		constexpr int ConstexprValue() const;
		consteval int ConstevalValue() const
		{
			return 1;
		}
		[[nodiscard]] int Marked();
		[[gnu::warn_unused_result]] int GnuMarked();
		int* _Nonnull NonNull();
		operator bool() const;
		auto operator<=>(const UnsupportedSurface&) const = default;
		UnsupportedSurface& operator+=(int delta);
		virtual int Abstract() = 0;
		bool Conditional() noexcept(sizeof(int) == 4);
		int Volatile() volatile;
		MOCKFAKEGEN_UNSUPPORTED_METHOD(FromMacro)

		template <Number T>
		T Constrained(T value)
			requires Number<T>;

		class PublicNested
		{
		};

	  private:
		struct PrivateToken
		{
		};

	  public:
		PrivateToken HiddenReturn();
		bool HiddenParam(PrivateToken token);

	  protected:
		bool ProtectedMethod();

	  private:
		bool PrivateMethod();
	};

	inline void UnsupportedSurface::OutOfClassInline() {}

	class UnsafeSpecial
	{
	  public:
		explicit UnsafeSpecial(int value);
		~UnsafeSpecial() noexcept(false);

		bool Touch();

	  private:
		int& ref_;
	};

	class UnsafeStaticData
	{
	  private:
		struct PrivateToken
		{
		};

	  public:
		static int& ref_value;
		static int values[2];
		static const int initialized = 7;
		static constinit int boot;
		static thread_local int tls_value;
		static PrivateToken private_token;

		bool Touch();
	};
} // namespace negative
