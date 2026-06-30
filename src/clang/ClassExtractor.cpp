#include "clang/ClassExtractor.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/Casting.h>

namespace mockfakegen
{
	namespace
	{
		[[nodiscard]] std::filesystem::path AbsoluteNormalized(const std::filesystem::path& path)
		{
			std::error_code absolute_error;
			auto absolute = std::filesystem::absolute(path, absolute_error);
			if (absolute_error)
			{
				absolute = path;
			}

			std::error_code canonical_error;
			const auto canonical = std::filesystem::weakly_canonical(absolute, canonical_error);
			if (!canonical_error)
			{
				return canonical.lexically_normal();
			}

			return absolute.lexically_normal();
		}

		[[nodiscard]] SourceLocation ToSourceLocation(const clang::SourceManager& source_manager,
													  clang::SourceLocation location)
		{
			const auto expansion_location = source_manager.getExpansionLoc(location);
			const auto presumed = source_manager.getPresumedLoc(expansion_location);
			if (presumed.isInvalid())
			{
				return {};
			}

			return SourceLocation{
				.file = presumed.getFilename(),
				.line = presumed.getLine(),
				.column = presumed.getColumn(),
			};
		}

		[[nodiscard]] SourceRange ToSourceRange(const clang::SourceManager& source_manager,
												clang::SourceRange range)
		{
			return SourceRange{
				.begin = ToSourceLocation(source_manager, range.getBegin()),
				.end = ToSourceLocation(source_manager, range.getEnd()),
			};
		}

		[[nodiscard]] std::vector<std::string> NamespaceParts(const clang::DeclContext* context)
		{
			std::vector<std::string> reversed;
			for (const auto* current = context; current != nullptr; current = current->getParent())
			{
				const auto* namespace_decl = llvm::dyn_cast<clang::NamespaceDecl>(current);
				if (namespace_decl == nullptr || namespace_decl->isAnonymousNamespace())
				{
					continue;
				}

				const auto name = namespace_decl->getNameAsString();
				if (!name.empty())
				{
					reversed.push_back(name);
				}
			}

			return std::vector<std::string>(reversed.rbegin(), reversed.rend());
		}

		class ClassExtractorVisitor final : public clang::RecursiveASTVisitor<ClassExtractorVisitor>
		{
		  public:
			ClassExtractorVisitor(const HeaderModel& target_header,
								  const clang::SourceManager& source_manager)
				: target_header_(target_header), source_manager_(source_manager),
				  target_path_(AbsoluteNormalized(target_header.absolute_path))
			{
			}

			bool VisitClassTemplateDecl(clang::ClassTemplateDecl* declaration)
			{
				if (declaration == nullptr)
				{
					return true;
				}

				const auto* record = declaration->getTemplatedDecl();
				if (record == nullptr || !IsInTargetHeader(record) || record->isImplicit())
				{
					return true;
				}

				const auto name = declaration->getNameAsString();
				if (name.empty())
				{
					return true;
				}

				result_.unsupported_items.push_back(UnsupportedItem{
					.kind = "class_template",
					.name = name,
					.reason = "class template is not supported by link replacement fake generation",
					.suggested_action = "exclude it or provide a hand-authored mock",
					.source_range = ToSourceRange(source_manager_, declaration->getSourceRange()),
				});
				return true;
			}

			bool VisitCXXRecordDecl(clang::CXXRecordDecl* declaration)
			{
				if (declaration == nullptr || !ShouldExtract(declaration))
				{
					return true;
				}

				const auto namespaces = NamespaceParts(declaration->getDeclContext());
				const auto name = declaration->getNameAsString();
				result_.classes.push_back(ClassModel{
					.name = name,
					.qualified_name = BuildQualifiedName(namespaces, name),
					.namespaces = namespaces,
					.mock_name = DefaultMockName(name),
					.mock_header_name = DefaultMockHeaderName(name),
					.fake_source_name = DefaultFakeSourceName(name),
					.source_header = target_header_,
					.mock_methods = {},
					.fake_methods = {},
					.unsupported_items = {},
				});
				return true;
			}

			[[nodiscard]] ClassExtractionResult TakeResult()
			{
				std::stable_sort(result_.classes.begin(),
								 result_.classes.end(),
								 [](const auto& lhs, const auto& rhs)
								 {
									 return lhs.qualified_name < rhs.qualified_name;
								 });
				std::stable_sort(result_.unsupported_items.begin(),
								 result_.unsupported_items.end(),
								 [](const auto& lhs, const auto& rhs)
								 {
									 return lhs.name < rhs.name;
								 });
				return std::move(result_);
			}

		  private:
			[[nodiscard]] bool ShouldExtract(const clang::CXXRecordDecl* declaration) const
			{
				if (declaration->isImplicit() || !declaration->isThisDeclarationADefinition())
				{
					return false;
				}

				if (declaration->getDescribedClassTemplate() != nullptr ||
					llvm::isa<clang::ClassTemplateSpecializationDecl>(declaration))
				{
					return false;
				}

				if (!declaration->isClass())
				{
					return false;
				}

				if (declaration->getIdentifier() == nullptr ||
					declaration->getNameAsString().empty())
				{
					return false;
				}

				return IsInTargetHeader(declaration);
			}

			[[nodiscard]] bool IsInTargetHeader(const clang::Decl* declaration) const
			{
				const auto location = source_manager_.getExpansionLoc(declaration->getLocation());
				if (location.isInvalid() || source_manager_.isInSystemHeader(location))
				{
					return false;
				}

				const auto filename = source_manager_.getFilename(location);
				if (filename.empty())
				{
					return false;
				}

				return AbsoluteNormalized(filename.str()) == target_path_;
			}

			const HeaderModel& target_header_;
			const clang::SourceManager& source_manager_;
			std::filesystem::path target_path_;
			ClassExtractionResult result_;
		};
	} // namespace

	ClassExtractionResult ExtractClassDefinitionsFromAst(const clang::ASTUnit& ast,
														 const HeaderModel& target_header)
	{
		ClassExtractorVisitor visitor(target_header, ast.getSourceManager());
		visitor.TraverseDecl(ast.getASTContext().getTranslationUnitDecl());
		return visitor.TakeResult();
	}
} // namespace mockfakegen
