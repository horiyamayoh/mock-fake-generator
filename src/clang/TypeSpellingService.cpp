#include "clang/TypeSpellingService.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <llvm/Support/raw_ostream.h>

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

		[[nodiscard]] bool NeedsDeclaratorAwareSpelling(clang::QualType type)
		{
			if (type.isNull())
			{
				return false;
			}
			if (type->isArrayType() || type->isFunctionPointerType() ||
				type->isFunctionReferenceType() || type->isMemberPointerType())
			{
				return true;
			}
			if (type->isReferenceType() || type->isPointerType())
			{
				const auto pointee = type->getPointeeType();
				return !pointee.isNull() && (pointee->isArrayType() || pointee->isFunctionType());
			}
			return false;
		}

		[[nodiscard]] std::string JoinTypeAndName(std::string_view type, std::string_view name)
		{
			if (name.empty())
			{
				return std::string(type);
			}
			std::string declaration(type);
			declaration += ' ';
			declaration += name;
			return declaration;
		}

		struct NestedTagName
		{
			std::string unqualified;
			std::string qualified;
		};

		[[nodiscard]] clang::QualType PeelDeclaratorType(const clang::ASTContext& ast_context,
														 clang::QualType type)
		{
			type = type.getNonReferenceType();
			while (type->isPointerType() || type->isArrayType() || type->isMemberPointerType())
			{
				if (type->isPointerType())
				{
					type = type->getPointeeType();
				}
				else if (const auto* array_type = ast_context.getAsArrayType(type);
						 array_type != nullptr)
				{
					type = array_type->getElementType();
				}
				else if (const auto* member_pointer = type->getAs<clang::MemberPointerType>();
						 member_pointer != nullptr)
				{
					type = member_pointer->getPointeeType();
				}
				type = type.getNonReferenceType();
			}
			return type;
		}

		[[nodiscard]] std::optional<NestedTagName>
		FindPublicNestedTagName(const clang::ASTContext& ast_context, clang::QualType type)
		{
			type = PeelDeclaratorType(ast_context, type);
			if (const auto* record = type->getAsCXXRecordDecl(); record != nullptr)
			{
				const auto* context = record->getDeclContext();
				if (context != nullptr && context->isRecord() &&
					record->getAccess() == clang::AS_public)
				{
					return NestedTagName{
						.unqualified = record->getNameAsString(),
						.qualified = record->getQualifiedNameAsString(),
					};
				}
			}
			if (const auto* enum_type = type->getAs<clang::EnumType>(); enum_type != nullptr)
			{
				const auto* enum_decl = enum_type->getDecl();
				const auto* context = enum_decl == nullptr ? nullptr : enum_decl->getDeclContext();
				if (context != nullptr && context->isRecord() &&
					enum_decl->getAccess() == clang::AS_public)
				{
					return NestedTagName{
						.unqualified = enum_decl->getNameAsString(),
						.qualified = enum_decl->getQualifiedNameAsString(),
					};
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] bool IsIdentifierBoundary(char character)
		{
			return !std::isalnum(static_cast<unsigned char>(character)) && character != '_';
		}

		void ReplaceIdentifierToken(std::string& text, std::string_view from, std::string_view to)
		{
			std::size_t position = 0U;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				const auto before_ok = position == 0U || IsIdentifierBoundary(text[position - 1U]);
				const auto after = position + from.size();
				const auto after_ok = after == text.size() || IsIdentifierBoundary(text[after]);
				if (before_ok && after_ok)
				{
					text.replace(position, from.size(), to);
					position += to.size();
				}
				else
				{
					position = after;
				}
			}
		}

		[[nodiscard]] std::string QualifyPublicNestedType(const clang::ASTContext& ast_context,
														  clang::QualType type,
														  std::string spelling)
		{
			const auto nested = FindPublicNestedTagName(ast_context, type);
			if (!nested.has_value() || nested->unqualified.empty() || nested->qualified.empty())
			{
				return spelling;
			}
			if (spelling.find(nested->qualified) != std::string::npos)
			{
				return spelling;
			}
			ReplaceIdentifierToken(spelling, nested->unqualified, nested->qualified);
			return spelling;
		}
	} // namespace

	TypeSpellingService::TypeSpellingService(const clang::ASTContext& ast_context)
		: ast_context_(ast_context)
	{
	}

	TypeSpelling TypeSpellingService::SpellType(clang::QualType type) const
	{
		auto spelling = QualifyPublicNestedType(ast_context_, type, PrintType(type));
		const auto needs_wrap = NeedsGMockParens(spelling);
		return TypeSpelling{
			.spelling = spelling,
			.gmock_spelling = needs_wrap ? WrapForGMockIfNeeded(std::move(spelling)) : spelling,
			.gmock_wrapped = needs_wrap,
		};
	}

	std::string TypeSpellingService::SpellDeclaration(clang::QualType type,
													  std::string_view name) const
	{
		clang::PrintingPolicy policy(ast_context_.getLangOpts());
		policy.SuppressTagKeyword = true;
		std::string spelling;
		llvm::raw_string_ostream stream(spelling);
		type.print(stream, policy, std::string(name));
		stream.flush();
		return QualifyPublicNestedType(
			ast_context_, type, NormalizePointerAndReferenceSpacing(std::move(spelling)));
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
			.declaration_spelling = NeedsDeclaratorAwareSpelling(parameter.getType())
				? SpellDeclaration(parameter.getType(), generated_name)
				: JoinTypeAndName(type.spelling, generated_name),
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
