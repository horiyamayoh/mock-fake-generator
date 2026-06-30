#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace mockfakegen_fixture
{
	struct TextPosition
	{
		std::size_t line = 1U;
		std::size_t column = 1U;
		std::size_t line_begin = 0U;
		std::size_t line_end = 0U;
	};

	[[nodiscard]] inline std::size_t FirstMismatch(std::string_view actual,
												   std::string_view expected) noexcept
	{
		const auto common_size = std::min(actual.size(), expected.size());
		for (std::size_t index = 0U; index < common_size; ++index)
		{
			if (actual[index] != expected[index])
			{
				return index;
			}
		}
		return common_size;
	}

	[[nodiscard]] inline TextPosition Locate(std::string_view text, const std::size_t index)
	{
		TextPosition position;
		const auto clamped_index = std::min(index, text.size());
		for (std::size_t offset = 0U; offset < clamped_index; ++offset)
		{
			if (text[offset] == '\n')
			{
				++position.line;
				position.column = 1U;
				position.line_begin = offset + 1U;
			}
			else
			{
				++position.column;
			}
		}

		const auto newline = text.find('\n', position.line_begin);
		position.line_end = newline == std::string_view::npos ? text.size() : newline;
		return position;
	}

	[[nodiscard]] inline std::string_view LineAt(std::string_view text,
												 const TextPosition& position) noexcept
	{
		if (position.line_begin > text.size())
		{
			return {};
		}
		return text.substr(position.line_begin, position.line_end - position.line_begin);
	}

	inline void PrintCaret(const std::size_t column)
	{
		std::cerr << "          ";
		for (std::size_t index = 1U; index < column; ++index)
		{
			std::cerr << ' ';
		}
		std::cerr << "^\n";
	}

	inline void ExpectGoldenTextEqual(std::string_view actual,
									  std::string_view expected,
									  const std::filesystem::path& path)
	{
		if (actual == expected)
		{
			return;
		}

		const auto mismatch = FirstMismatch(actual, expected);
		const auto expected_position = Locate(expected, mismatch);
		const auto actual_position = Locate(actual, mismatch);

		std::cerr << "golden content mismatch: " << path.generic_string() << '\n'
				  << "first difference at expected line " << expected_position.line << ", column "
				  << expected_position.column << " (actual line " << actual_position.line
				  << ", column " << actual_position.column << ")\n"
				  << "expected: " << LineAt(expected, expected_position) << '\n'
				  << "actual:   " << LineAt(actual, actual_position) << '\n';
		PrintCaret(expected_position.column);
		std::cerr << "expected size: " << expected.size() << ", actual size: " << actual.size()
				  << '\n';
		std::exit(1);
	}
} // namespace mockfakegen_fixture
