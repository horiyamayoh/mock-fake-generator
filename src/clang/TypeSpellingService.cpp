#include "clang/TypeSpellingService.h"

#include <algorithm>

#include <clang/AST/ASTContext.h>

namespace mockfakegen
{
	namespace
	{
		void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
		{
			std::size_t position = 0U;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				text.replace(position, from.size(), to);
				position += to.size();
			}
		}

		[[nodiscard]] std::string NormalizePointerAndReferenceSpacing(std::string spelling)
		{
			ReplaceAll(spelling, " *", "*");
			ReplaceAll(spelling, "* ", "*");
			ReplaceAll(spelling, " &", "&");
			ReplaceAll(spelling, "& ", "&");
			return spelling;
		}

		[[nodiscard]] bool IsNonConstByValue(clang::QualType type)
		{
			return !type->isReferenceType() && !type->isPointerType() && !type.isConstQualified();
		}
	} // namespace

	TypeSpellingService::TypeSpellingService(const clang::ASTContext& ast_context)
		: ast_context_(ast_context)
	{
	}

	TypeSpelling TypeSpellingService::SpellType(clang::QualType type) const
	{
		auto spelling = PrintType(type);
		const auto needs_wrap = NeedsGMockParens(spelling);
		return TypeSpelling{
			.spelling = spelling,
			.gmock_spelling = needs_wrap ? WrapForGMockIfNeeded(std::move(spelling)) : spelling,
			.gmock_wrapped = needs_wrap,
		};
	}

	ParameterModel TypeSpellingService::SpellParameter(const clang::ParmVarDecl& parameter,
													   std::size_t parameter_index) const
	{
		const auto type = SpellType(parameter.getType());
		const auto original_name = parameter.getNameAsString();
		const auto generated_name =
			original_name.empty() ? "arg" + std::to_string(parameter_index) : original_name;
		return ParameterModel{
			.type_spelling = type.spelling,
			.gmock_type_spelling = type.gmock_spelling,
			.original_name = original_name,
			.generated_name = generated_name,
			.has_default_argument = parameter.hasDefaultArg(),
			.is_rvalue_ref = parameter.getType()->isRValueReferenceType(),
			.is_nonconst_by_value = IsNonConstByValue(parameter.getType()),
		};
	}

	bool TypeSpellingService::NeedsGMockParens(std::string_view spelling)
	{
		int paren_depth = 0;
		int bracket_depth = 0;
		for (const char character : spelling)
		{
			switch (character)
			{
				case '(':
					++paren_depth;
					break;
				case ')':
					paren_depth = std::max(0, paren_depth - 1);
					break;
				case '[':
					++bracket_depth;
					break;
				case ']':
					bracket_depth = std::max(0, bracket_depth - 1);
					break;
				case ',':
					if (paren_depth == 0 && bracket_depth == 0)
					{
						return true;
					}
					break;
				default:
					break;
			}
		}
		return false;
	}

	std::string TypeSpellingService::WrapForGMockIfNeeded(std::string spelling)
	{
		if (!NeedsGMockParens(spelling))
		{
			return spelling;
		}
		return '(' + spelling + ')';
	}

	std::string TypeSpellingService::PrintType(clang::QualType type) const
	{
		clang::PrintingPolicy policy(ast_context_.getLangOpts());
		policy.SuppressTagKeyword = true;
		return NormalizePointerAndReferenceSpacing(type.getAsString(policy));
	}
} // namespace mockfakegen
