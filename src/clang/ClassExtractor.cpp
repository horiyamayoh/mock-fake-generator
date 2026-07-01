#include "clang/ClassExtractor.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/Casting.h>

#include "clang/TypeSpellingService.h"

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

		[[nodiscard]] AccessKind ToAccessKind(clang::AccessSpecifier access)
		{
			switch (access)
			{
				case clang::AS_public:
					return AccessKind::Public;
				case clang::AS_protected:
					return AccessKind::Protected;
				case clang::AS_private:
					return AccessKind::Private;
				case clang::AS_none:
					return AccessKind::Unknown;
			}

			return AccessKind::Unknown;
		}

		[[nodiscard]] RefQualifierKind ToRefQualifierKind(clang::RefQualifierKind qualifier)
		{
			switch (qualifier)
			{
				case clang::RQ_None:
					return RefQualifierKind::None;
				case clang::RQ_LValue:
					return RefQualifierKind::LValue;
				case clang::RQ_RValue:
					return RefQualifierKind::RValue;
			}

			return RefQualifierKind::None;
		}

		[[nodiscard]] bool IsNoexcept(const clang::CXXMethodDecl& method)
		{
			const auto exception_spec = method.getExceptionSpecType();
			return exception_spec == clang::EST_BasicNoexcept ||
				exception_spec == clang::EST_NoexceptTrue || exception_spec == clang::EST_NoThrow;
		}

		[[nodiscard]] bool HasConditionalNoexcept(const clang::CXXMethodDecl& method)
		{
			const auto exception_spec = method.getExceptionSpecType();
			return exception_spec == clang::EST_DependentNoexcept ||
				exception_spec == clang::EST_NoexceptFalse ||
				exception_spec == clang::EST_NoexceptTrue;
		}

		[[nodiscard]] bool
		HasUnsupportedSpecialMemberExceptionSpec(const clang::CXXMethodDecl& method)
		{
			const auto exception_spec = method.getExceptionSpecType();
			return exception_spec == clang::EST_DependentNoexcept ||
				exception_spec == clang::EST_NoexceptFalse;
		}

		[[nodiscard]] bool HasAccessibleDefaultConstructor(const clang::CXXRecordDecl& record)
		{
			if (!record.hasDefinition())
			{
				return false;
			}

			for (const auto* constructor : record.ctors())
			{
				if (constructor->isDefaultConstructor() && !constructor->isDeleted() &&
					constructor->getAccess() == clang::AS_public)
				{
					return true;
				}
			}

			return record.needsImplicitDefaultConstructor();
		}

		[[nodiscard]] bool IsDefaultConstructibleReturn(clang::QualType type)
		{
			if (type->isVoidType() || type->isReferenceType())
			{
				return false;
			}
			if (type->isBuiltinType() || type->isPointerType() || type->isEnumeralType())
			{
				return true;
			}

			const auto* record = type->getAsCXXRecordDecl();
			if (record == nullptr)
			{
				return false;
			}

			return HasAccessibleDefaultConstructor(*record);
		}

		[[nodiscard]] bool IsDefaultConstructibleField(clang::QualType type)
		{
			if (type->isReferenceType() || type->isArrayType())
			{
				return false;
			}
			return IsDefaultConstructibleReturn(type);
		}

		[[nodiscard]] std::string UnsupportedMethodName(const clang::NamedDecl& declaration)
		{
			const auto name = declaration.getNameAsString();
			if (!name.empty())
			{
				return name;
			}

			return declaration.getQualifiedNameAsString();
		}

		[[nodiscard]] UnsupportedItem
		MakeUnsupportedMethod(const clang::NamedDecl& declaration,
							  const clang::SourceManager& source_manager,
							  UnsupportedReasonCode reason_code,
							  std::string class_name,
							  std::string kind,
							  std::string reason)
		{
			auto member_signature = class_name;
			if (!member_signature.empty())
			{
				member_signature += "::";
			}
			member_signature += UnsupportedMethodName(declaration);
			return UnsupportedItem{
				.reason_code = reason_code,
				.kind = std::move(kind),
				.class_name = std::move(class_name),
				.name = UnsupportedMethodName(declaration),
				.member_signature = std::move(member_signature),
				.reason = std::move(reason),
				.suggested_action = "exclude this member or provide a hand-authored mock",
				.source_range = ToSourceRange(source_manager, declaration.getSourceRange()),
			};
		}

		[[nodiscard]] Diagnostic UnsupportedDiagnostic(const UnsupportedItem& item)
		{
			return Diagnostic{
				.severity = DiagnosticSeverity::Warning,
				.code = DiagnosticCode::UnsupportedConstruct,
				.source_range = item.source_range,
				.message = item.kind + ": " + item.reason,
			};
		}

		[[nodiscard]] std::string SignatureForReport(const ClassModel& class_model,
													 const clang::CXXMethodDecl& method,
													 const std::vector<ParameterModel>& parameters)
		{
			std::string signature = class_model.qualified_name;
			signature += "::";
			signature += method.getNameAsString();
			signature += '(';
			for (std::size_t index = 0U; index < parameters.size(); ++index)
			{
				if (index != 0U)
				{
					signature += ", ";
				}
				signature += parameters[index].type_spelling;
			}
			signature += ')';
			return signature;
		}

		class ClassExtractorVisitor final : public clang::RecursiveASTVisitor<ClassExtractorVisitor>
		{
		  public:
			ClassExtractorVisitor(const HeaderModel& target_header,
								  const clang::ASTContext& ast_context,
								  const clang::SourceManager& source_manager,
								  ClassExtractionOptions options)
				: target_header_(target_header), ast_context_(ast_context),
				  type_spelling_(ast_context), source_manager_(source_manager),
				  target_path_(AbsoluteNormalized(target_header.absolute_path)), options_(options)
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

				auto item = UnsupportedItem{
					.reason_code = UnsupportedReasonCode::ClassTemplate,
					.kind = "class_template",
					.class_name = name,
					.name = name,
					.member_signature = name,
					.reason = "class template is not supported by link replacement fake generation",
					.suggested_action = "exclude it or provide a hand-authored mock",
					.source_range = ToSourceRange(source_manager_, declaration->getSourceRange()),
				};
				result_.diagnostics.push_back(UnsupportedDiagnostic(item));
				result_.unsupported_items.push_back(std::move(item));
				return true;
			}

			bool VisitClassTemplateSpecializationDecl(
				clang::ClassTemplateSpecializationDecl* declaration)
			{
				if (declaration == nullptr || declaration->isImplicit() ||
					!declaration->isThisDeclarationADefinition() || !IsInTargetHeader(declaration))
				{
					return true;
				}
				if (declaration->getSpecializationKind() == clang::TSK_ImplicitInstantiation)
				{
					return true;
				}

				const auto name = declaration->getNameAsString();
				if (name.empty())
				{
					return true;
				}

				const auto is_partial =
					llvm::isa<clang::ClassTemplatePartialSpecializationDecl>(declaration);
				auto item = UnsupportedItem{
					.reason_code = UnsupportedReasonCode::ClassTemplateSpecialization,
					.kind = is_partial ? "class_template_partial_specialization"
									   : "class_template_specialization",
					.class_name = name,
					.name = name,
					.member_signature = name,
					.reason = is_partial
						? "partial class template specialization is not supported"
						: "explicit class template specialization is not supported",
					.suggested_action = "exclude it or provide a hand-authored mock",
					.source_range = ToSourceRange(source_manager_, declaration->getSourceRange()),
				};
				result_.diagnostics.push_back(UnsupportedDiagnostic(item));
				result_.unsupported_items.push_back(std::move(item));
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
				auto class_model = ClassModel{
					.name = name,
					.qualified_name = BuildQualifiedName(namespaces, name),
					.namespaces = namespaces,
					.mock_name = DefaultMockName(name),
					.mock_header_name = DefaultMockHeaderName(name),
					.fake_source_name = DefaultFakeSourceName(name),
					.source_header = target_header_,
					.mock_methods = {},
					.fake_methods = {},
					.fake_constructors = {},
					.fake_destructors = {},
					.static_data_members = {},
					.unsupported_items = {},
					.interface_mock = options_.interface_mock,
				};
				if (options_.interface_mock)
				{
					ExtractInterfaceMembers(*declaration, class_model);
				}
				else
				{
					ExtractMethods(*declaration, class_model);
				}
				result_.classes.push_back(std::move(class_model));
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

			[[nodiscard]] bool IsMacroOrigin(const clang::Decl& declaration) const
			{
				const auto range = declaration.getSourceRange();
				return declaration.getLocation().isMacroID() || range.getBegin().isMacroID() ||
					range.getEnd().isMacroID();
			}

			[[nodiscard]] bool HasBodyInTargetHeader(const clang::FunctionDecl& function) const
			{
				const clang::FunctionDecl* definition = nullptr;
				if (!function.hasBody(definition) || definition == nullptr)
				{
					return false;
				}

				return IsInTargetHeader(definition);
			}

			[[nodiscard]] bool HasUnsupportedAttributes(const clang::Decl& declaration) const
			{
				for (const auto* attribute : declaration.attrs())
				{
					if (attribute == nullptr || attribute->isImplicit())
					{
						continue;
					}
					if (llvm::isa<clang::OverrideAttr>(attribute) ||
						llvm::isa<clang::FinalAttr>(attribute))
					{
						continue;
					}
					return true;
				}
				return false;
			}

			void ExtractMethods(const clang::CXXRecordDecl& declaration, ClassModel& class_model)
			{
				for (const auto* child : declaration.decls())
				{
					const auto* function_template =
						llvm::dyn_cast<clang::FunctionTemplateDecl>(child);
					if (function_template != nullptr)
					{
						const auto* templated = function_template->getTemplatedDecl();
						if (templated != nullptr)
						{
							RecordUnsupportedMethod(class_model,
													*templated,
													UnsupportedReasonCode::FunctionTemplate,
													"function_template",
													"function template member is not supported");
						}
						continue;
					}

					const auto* variable = llvm::dyn_cast<clang::VarDecl>(child);
					if (variable != nullptr && variable->isStaticDataMember() &&
						!variable->isImplicit())
					{
						RecordStaticDataMember(*variable, class_model);
						continue;
					}

					const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(child);
					if (method == nullptr || method->isImplicit())
					{
						continue;
					}

					if (IsMacroOrigin(*method))
					{
						RecordUnsupportedMethod(
							class_model,
							*method,
							UnsupportedReasonCode::MacroOrigin,
							"macro_origin",
							"macro-origin method declaration is not "
							"supported because source spelling may be unstable");
						continue;
					}
					if (llvm::isa<clang::CXXConstructorDecl>(method))
					{
						const auto* constructor = llvm::cast<clang::CXXConstructorDecl>(method);
						RecordConstructor(*constructor, declaration, class_model);
						continue;
					}
					if (llvm::isa<clang::CXXDestructorDecl>(method))
					{
						const auto* destructor = llvm::cast<clang::CXXDestructorDecl>(method);
						RecordDestructor(*destructor, class_model);
						continue;
					}
					if (method->isDeleted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DeletedMethod,
												"deleted_method",
												"deleted method is not supported");
						continue;
					}
					if (method->isDefaulted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DefaultedMethod,
												"defaulted_method",
												"defaulted method is not supported");
						continue;
					}
					if (llvm::isa<clang::CXXConversionDecl>(method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConversionOperator,
												"conversion_operator",
												"conversion operator is not supported");
						continue;
					}
					if (method->isOverloadedOperator())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::OverloadedOperator,
												"overloaded_operator",
												"overloaded operator is not supported");
						continue;
					}
					if (method->isPureVirtual())
					{
						RecordUnsupportedMethod(
							class_model,
							*method,
							UnsupportedReasonCode::PureVirtualMethod,
							"pure_virtual_method",
							"pure virtual method requires interface mock mode and is not faked in "
							"normal link replacement mode");
						continue;
					}
					if (method->getAccess() != clang::AS_public)
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::NonPublicMethod,
												"non_public_method",
												"only public methods are generated");
						continue;
					}
					if (method->isConsteval())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConstevalMethod,
												"consteval_method",
												"consteval method is not supported");
						continue;
					}
					if (method->isConstexpr())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConstexprMethod,
												"constexpr_method",
												"constexpr method is not supported");
						continue;
					}
					if (HasUnsupportedAttributes(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::UnsupportedAttribute,
												"unsupported_attribute",
												"method has attributes that are not supported");
						continue;
					}
					if (HasBodyInTargetHeader(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::InlineBody,
												"inline_body",
												"inline method body is not supported");
						continue;
					}
					if (HasConditionalNoexcept(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConditionalNoexcept,
												"conditional_noexcept",
												"conditional noexcept is not supported");
						continue;
					}
					if (method->isVolatile())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::VolatileMethod,
												"volatile_method",
												"volatile method is not supported");
						continue;
					}

					auto method_model = BuildMethodModel(*method, class_model);
					class_model.mock_methods.push_back(method_model);
					class_model.fake_methods.push_back(std::move(method_model));
				}
			}

			void ExtractInterfaceMembers(const clang::CXXRecordDecl& declaration,
										 ClassModel& class_model)
			{
				for (const auto* child : declaration.decls())
				{
					const auto* function_template =
						llvm::dyn_cast<clang::FunctionTemplateDecl>(child);
					if (function_template != nullptr)
					{
						const auto* templated = function_template->getTemplatedDecl();
						if (templated != nullptr)
						{
							RecordUnsupportedMethod(class_model,
													*templated,
													UnsupportedReasonCode::FunctionTemplate,
													"function_template",
													"function template member is not supported");
						}
						continue;
					}

					const auto* variable = llvm::dyn_cast<clang::VarDecl>(child);
					if (variable != nullptr && variable->isStaticDataMember() &&
						!variable->isImplicit())
					{
						RecordUnsupportedMethod(
							class_model,
							*variable,
							UnsupportedReasonCode::InterfaceConstruct,
							"interface_construct",
							"static data member is not part of a pure interface");
						continue;
					}

					const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(child);
					if (method == nullptr || method->isImplicit())
					{
						continue;
					}

					if (IsMacroOrigin(*method))
					{
						RecordUnsupportedMethod(
							class_model,
							*method,
							UnsupportedReasonCode::MacroOrigin,
							"macro_origin",
							"macro-origin method declaration is not "
							"supported because source spelling may be unstable");
						continue;
					}
					if (llvm::isa<clang::CXXConstructorDecl>(method))
					{
						const auto* constructor = llvm::cast<clang::CXXConstructorDecl>(method);
						RecordInterfaceConstructor(*constructor, class_model);
						continue;
					}
					if (llvm::isa<clang::CXXDestructorDecl>(method))
					{
						const auto* destructor = llvm::cast<clang::CXXDestructorDecl>(method);
						RecordInterfaceDestructor(*destructor, class_model);
						continue;
					}
					if (method->isDeleted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DeletedMethod,
												"deleted_method",
												"deleted method is not supported");
						continue;
					}
					if (method->isDefaulted())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::DefaultedMethod,
												"defaulted_method",
												"defaulted method is not supported");
						continue;
					}
					if (llvm::isa<clang::CXXConversionDecl>(method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConversionOperator,
												"conversion_operator",
												"conversion operator is not supported");
						continue;
					}
					if (method->isOverloadedOperator())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::OverloadedOperator,
												"overloaded_operator",
												"overloaded operator is not supported");
						continue;
					}
					if (method->getAccess() != clang::AS_public)
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::NonPublicMethod,
												"non_public_method",
												"only public interface methods are generated");
						continue;
					}
					if (method->isStatic())
					{
						RecordUnsupportedMethod(
							class_model,
							*method,
							UnsupportedReasonCode::InterfaceConstruct,
							"interface_construct",
							"static member function is not part of a pure interface");
						continue;
					}
					if (!method->isVirtual() || !method->isPureVirtual())
					{
						RecordUnsupportedMethod(
							class_model,
							*method,
							UnsupportedReasonCode::InterfaceConstruct,
							"interface_construct",
							"interface mock mode requires public pure virtual methods");
						continue;
					}
					if (method->isConsteval())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConstevalMethod,
												"consteval_method",
												"consteval method is not supported");
						continue;
					}
					if (method->isConstexpr())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConstexprMethod,
												"constexpr_method",
												"constexpr method is not supported");
						continue;
					}
					if (HasUnsupportedAttributes(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::UnsupportedAttribute,
												"unsupported_attribute",
												"method has attributes that are not supported");
						continue;
					}
					if (HasBodyInTargetHeader(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::InlineBody,
												"inline_body",
												"inline method body is not supported");
						continue;
					}
					if (HasConditionalNoexcept(*method))
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::ConditionalNoexcept,
												"conditional_noexcept",
												"conditional noexcept is not supported");
						continue;
					}
					if (method->isVolatile())
					{
						RecordUnsupportedMethod(class_model,
												*method,
												UnsupportedReasonCode::VolatileMethod,
												"volatile_method",
												"volatile method is not supported");
						continue;
					}

					class_model.mock_methods.push_back(BuildMethodModel(*method, class_model));
				}

				if (class_model.mock_methods.empty())
				{
					RecordUnsupportedMethod(
						class_model,
						declaration,
						UnsupportedReasonCode::InterfaceConstruct,
						"interface_construct",
						"interface mock mode requires at least one public pure virtual method");
				}
			}

			[[nodiscard]] std::vector<ParameterModel>
			BuildParameters(const clang::FunctionDecl& function) const
			{
				std::vector<ParameterModel> parameters;
				parameters.reserve(function.parameters().size());
				for (std::size_t index = 0U; index < function.parameters().size(); ++index)
				{
					const auto* parameter = function.parameters()[index];
					parameters.push_back(type_spelling_.SpellParameter(*parameter, index));
				}
				return parameters;
			}

			[[nodiscard]] std::string
			UnsupportedConstructorReason(const clang::CXXRecordDecl& declaration,
										 std::vector<std::string>& member_initializers)
			{
				for (const auto& base : declaration.bases())
				{
					const auto type = base.getType();
					const auto type_spelling = type_spelling_.SpellType(type).spelling;
					const auto* record = type->getAsCXXRecordDecl();
					if (record == nullptr)
					{
						return "base class '" + type_spelling + "' cannot be safely inspected";
					}
					if (!HasAccessibleDefaultConstructor(*record))
					{
						return "base class '" + type_spelling + "' is not default-constructible";
					}
				}

				for (const auto* field : declaration.fields())
				{
					if (field == nullptr || field->hasInClassInitializer())
					{
						continue;
					}

					const auto field_name = field->getNameAsString();
					if (field_name.empty())
					{
						return "anonymous data member cannot be safely initialized";
					}
					if (field->getType()->isReferenceType())
					{
						return "reference member '" + field_name +
							"' cannot be safely default-initialized";
					}
					if (!IsDefaultConstructibleField(field->getType()))
					{
						const auto type = type_spelling_.SpellType(field->getType());
						return "member '" + field_name + "' of type '" + type.spelling +
							"' is not default-constructible";
					}

					member_initializers.push_back(field_name + "{}");
				}
				return {};
			}

			void RecordConstructor(const clang::CXXConstructorDecl& constructor,
								   const clang::CXXRecordDecl& declaration,
								   ClassModel& class_model)
			{
				if (!options_.fake_special_members)
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::Constructor,
											"constructor",
											"constructor fake generation is not supported");
					return;
				}
				if (constructor.isDeleted())
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::Constructor,
											"constructor",
											"deleted constructor cannot be faked");
					return;
				}
				if (constructor.getAccess() != clang::AS_public)
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::Constructor,
											"constructor",
											"only public constructors are generated");
					return;
				}
				if (HasUnsupportedSpecialMemberExceptionSpec(constructor))
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::Constructor,
											"constructor",
											"constructor exception specification is not supported");
					return;
				}
				if (constructor.isDefaulted())
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::DefaultedMethod,
											"defaulted_method",
											"defaulted constructor is not generated as a fake");
					return;
				}
				if (HasBodyInTargetHeader(constructor))
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::InlineBody,
											"inline_body",
											"constructor with header-local body is not supported");
					return;
				}

				std::vector<std::string> member_initializers;
				const auto unsupported_reason =
					UnsupportedConstructorReason(declaration, member_initializers);
				if (!unsupported_reason.empty())
				{
					RecordUnsupportedMethod(class_model,
											constructor,
											UnsupportedReasonCode::Constructor,
											"constructor",
											unsupported_reason);
					return;
				}

				const auto parameters = BuildParameters(constructor);
				class_model.fake_constructors.push_back(ConstructorModel{
					.parameters = parameters,
					.member_initializers = std::move(member_initializers),
					.signature_for_report =
						SignatureForReport(class_model, constructor, parameters),
					.is_noexcept = IsNoexcept(constructor),
					.source_range = ToSourceRange(source_manager_, constructor.getSourceRange()),
				});
			}

			void RecordDestructor(const clang::CXXDestructorDecl& destructor,
								  ClassModel& class_model)
			{
				if (!options_.fake_special_members)
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::Destructor,
											"destructor",
											"destructor fake generation is not supported");
					return;
				}
				if (destructor.isDeleted())
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::Destructor,
											"destructor",
											"deleted destructor cannot be faked");
					return;
				}
				if (destructor.getAccess() != clang::AS_public)
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::Destructor,
											"destructor",
											"only public destructors are generated");
					return;
				}
				if (HasUnsupportedSpecialMemberExceptionSpec(destructor))
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::Destructor,
											"destructor",
											"destructor exception specification is not supported");
					return;
				}
				if (destructor.isDefaulted())
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::DefaultedMethod,
											"defaulted_method",
											"defaulted destructor is not generated as a fake");
					return;
				}
				if (HasBodyInTargetHeader(destructor))
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::InlineBody,
											"inline_body",
											"destructor with header-local body is not supported");
					return;
				}

				class_model.fake_destructors.push_back(DestructorModel{
					.signature_for_report = SignatureForReport(class_model, destructor, {}),
					.is_noexcept = IsNoexcept(destructor),
					.source_range = ToSourceRange(source_manager_, destructor.getSourceRange()),
				});
			}

			[[nodiscard]] bool ReferencesNestedType(clang::QualType type) const
			{
				while (type->isPointerType())
				{
					type = type->getPointeeType();
				}

				if (const auto* record = type->getAsCXXRecordDecl(); record != nullptr)
				{
					const auto* context = record->getDeclContext();
					return context != nullptr && context->isRecord();
				}

				if (const auto* enum_type = type->getAs<clang::EnumType>(); enum_type != nullptr)
				{
					const auto* enum_decl = enum_type->getDecl();
					const auto* context =
						enum_decl == nullptr ? nullptr : enum_decl->getDeclContext();
					return context != nullptr && context->isRecord();
				}

				return false;
			}

			[[nodiscard]] std::string
			UnsupportedStaticDataReason(const clang::VarDecl& variable) const
			{
				const auto type = variable.getType();
				const auto name = variable.getNameAsString();
				if (variable.hasAttr<clang::ConstInitAttr>())
				{
					return "constinit static data member requires an explicit initializer policy";
				}
				if (variable.getTLSKind() != clang::VarDecl::TLS_None)
				{
					return "thread-local static data member is not supported";
				}
				if (variable.hasInit())
				{
					return "static data member with in-class initializer is not supported";
				}
				if (type->isReferenceType())
				{
					return "reference static data member '" + name +
						"' cannot be safely default-initialized";
				}
				if (type->isArrayType())
				{
					return "array static data member '" + name +
						"' requires array declarator synthesis";
				}
				if (ReferencesNestedType(type))
				{
					return "static data member '" + name +
						"' uses a nested type that cannot be safely spelled outside the class";
				}
				if (!IsDefaultConstructibleField(type))
				{
					const auto type_spelling = type_spelling_.SpellType(type);
					return "static data member '" + name + "' of type '" + type_spelling.spelling +
						"' is not default-constructible";
				}
				return {};
			}

			void RecordStaticDataMember(const clang::VarDecl& variable, ClassModel& class_model)
			{
				if (variable.isInline() || variable.isConstexpr())
				{
					return;
				}
				if (!options_.fake_static_data)
				{
					RecordUnsupportedMethod(class_model,
											variable,
											UnsupportedReasonCode::StaticDataMember,
											"static_data_member",
											"static data member fake generation is not enabled");
					return;
				}

				const auto unsupported_reason = UnsupportedStaticDataReason(variable);
				if (!unsupported_reason.empty())
				{
					RecordUnsupportedMethod(class_model,
											variable,
											UnsupportedReasonCode::StaticDataMember,
											"static_data_member",
											unsupported_reason);
					return;
				}

				const auto type = type_spelling_.SpellType(variable.getType());
				const auto signature =
					class_model.qualified_name + "::" + variable.getNameAsString();
				class_model.static_data_members.push_back(StaticDataMemberModel{
					.name = variable.getNameAsString(),
					.type_spelling = type.spelling,
					.signature_for_report = signature,
					.source_range = ToSourceRange(source_manager_, variable.getSourceRange()),
				});
			}

			void RecordInterfaceConstructor(const clang::CXXConstructorDecl& constructor,
											ClassModel& class_model)
			{
				if (constructor.getAccess() == clang::AS_public &&
					constructor.isDefaultConstructor() &&
					(constructor.isDefaulted() || constructor.doesThisDeclarationHaveABody()))
				{
					return;
				}

				RecordUnsupportedMethod(
					class_model,
					constructor,
					UnsupportedReasonCode::InterfaceConstruct,
					"interface_construct",
					"interface mock mode only supports public defaulted constructors");
			}

			void RecordInterfaceDestructor(const clang::CXXDestructorDecl& destructor,
										   ClassModel& class_model)
			{
				if (destructor.getAccess() != clang::AS_public)
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::InterfaceConstruct,
											"interface_construct",
											"interface destructor must be public");
					return;
				}
				if (!destructor.isVirtual())
				{
					RecordUnsupportedMethod(class_model,
											destructor,
											UnsupportedReasonCode::InterfaceConstruct,
											"interface_construct",
											"interface destructor must be virtual");
					return;
				}
				if (destructor.isPureVirtual())
				{
					RecordUnsupportedMethod(
						class_model,
						destructor,
						UnsupportedReasonCode::InterfaceConstruct,
						"interface_construct",
						"pure virtual destructor requires a hand-authored definition");
					return;
				}
				if (destructor.isDefaulted() || destructor.doesThisDeclarationHaveABody())
				{
					return;
				}

				RecordUnsupportedMethod(class_model,
										destructor,
										UnsupportedReasonCode::InterfaceConstruct,
										"interface_construct",
										"out-of-line interface destructor is not supported");
			}

			void RecordUnsupportedMethod(ClassModel& class_model,
										 const clang::NamedDecl& declaration,
										 UnsupportedReasonCode reason_code,
										 std::string kind,
										 std::string reason)
			{
				auto item = MakeUnsupportedMethod(declaration,
												  source_manager_,
												  reason_code,
												  class_model.qualified_name,
												  std::move(kind),
												  std::move(reason));
				result_.diagnostics.push_back(UnsupportedDiagnostic(item));
				class_model.unsupported_items.push_back(std::move(item));
			}

			[[nodiscard]] MethodModel BuildMethodModel(const clang::CXXMethodDecl& method,
													   const ClassModel& class_model) const
			{
				auto parameters = BuildParameters(method);

				const auto return_type = type_spelling_.SpellType(method.getReturnType());
				const auto raw_return_type = method.getReturnType();
				return MethodModel{
					.name = method.getNameAsString(),
					.qualified_owner_name = class_model.qualified_name,
					.return_type_spelling = return_type.spelling,
					.gmock_return_type_spelling = return_type.gmock_spelling,
					.parameters = parameters,
					.signature_for_report = SignatureForReport(class_model, method, parameters),
					.is_static = method.isStatic(),
					.is_const = method.isConst(),
					.is_volatile = method.isVolatile(),
					.is_noexcept = IsNoexcept(method),
					.has_conditional_noexcept = HasConditionalNoexcept(method),
					.is_virtual = method.isVirtual(),
					.is_pure_virtual = method.isPureVirtual(),
					.is_inline = method.isInlined(),
					.is_deleted = method.isDeleted(),
					.is_defaulted = method.isDefaulted(),
					.return_type_is_void = raw_return_type->isVoidType(),
					.return_type_is_reference = raw_return_type->isReferenceType(),
					.return_type_is_default_constructible =
						IsDefaultConstructibleReturn(raw_return_type),
					.ref_qualifier = ToRefQualifierKind(method.getRefQualifier()),
					.access = ToAccessKind(method.getAccess()),
					.source_range = ToSourceRange(source_manager_, method.getSourceRange()),
				};
			}

			const HeaderModel& target_header_;
			const clang::ASTContext& ast_context_;
			TypeSpellingService type_spelling_;
			const clang::SourceManager& source_manager_;
			std::filesystem::path target_path_;
			ClassExtractionOptions options_;
			ClassExtractionResult result_;
		};
	} // namespace

	ClassExtractionResult ExtractClassDefinitionsFromAst(const clang::ASTUnit& ast,
														 const HeaderModel& target_header,
														 ClassExtractionOptions options)
	{
		ClassExtractorVisitor visitor(
			target_header, ast.getASTContext(), ast.getSourceManager(), options);
		visitor.TraverseDecl(ast.getASTContext().getTranslationUnitDecl());
		return visitor.TakeResult();
	}
} // namespace mockfakegen
