#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Config.h"

namespace mockfakegen
{
	enum class AccessKind
	{
		Unknown,
		Public,
		Protected,
		Private,
	};

	enum class DiagnosticSeverity
	{
		Info,
		Warning,
		Error,
	};

	enum class DiagnosticCode
	{
		ParseError,
		UnsupportedConstruct,
		ExtractionError,
	};

	enum class UnsupportedReasonCode
	{
		Unknown,
		ClassTemplate,
		ClassTemplateSpecialization,
		FunctionTemplate,
		Constructor,
		Destructor,
		ConversionOperator,
		OverloadedOperator,
		AssignmentOperator,
		PureVirtualMethod,
		NonPublicMethod,
		DeletedMethod,
		DefaultedMethod,
		InlineBody,
		ConstexprMethod,
		ConstevalMethod,
		ConditionalNoexcept,
		VolatileMethod,
		UnsupportedAttribute,
		MacroOrigin,
		UnsupportedTypeSpelling,
		PrivateNestedType,
		NestedClass,
		AnonymousNamespace,
		StaticDataMember,
		InterfaceConstruct,
	};

	struct SourceLocation
	{
		std::filesystem::path file;
		std::uint32_t line = 0U;
		std::uint32_t column = 0U;
	};

	struct SourceRange
	{
		SourceLocation begin;
		SourceLocation end;
	};

	struct Diagnostic
	{
		DiagnosticSeverity severity = DiagnosticSeverity::Error;
		DiagnosticCode code = DiagnosticCode::ExtractionError;
		SourceRange source_range;
		std::string message;
	};

	struct HeaderModel
	{
		std::filesystem::path absolute_path;
		std::filesystem::path project_relative_path;
		std::string include_spelling;
		bool parsed_by_real_tu = false;
		bool parsed_by_synthetic_tu = false;
	};

	struct ParameterModel
	{
		std::string type_spelling;
		std::string gmock_type_spelling;
		std::string declaration_spelling;
		std::string original_name;
		std::string generated_name;
		bool has_default_argument = false;
		bool is_rvalue_ref = false;
		bool is_nonconst_by_value = false;
	};

	enum class RefQualifierKind
	{
		None,
		LValue,
		RValue,
	};

	struct MethodModel
	{
		std::string name;
		std::string qualified_owner_name;
		std::string return_type_spelling;
		std::string gmock_return_type_spelling;
		std::vector<ParameterModel> parameters;
		std::string signature_for_report;
		bool is_static = false;
		bool is_const = false;
		bool is_volatile = false;
		bool is_noexcept = false;
		bool has_conditional_noexcept = false;
		bool is_virtual = false;
		bool is_pure_virtual = false;
		bool is_inline = false;
		bool is_deleted = false;
		bool is_defaulted = false;
		bool return_type_is_void = false;
		bool return_type_is_reference = false;
		bool return_type_is_default_constructible = true;
		RefQualifierKind ref_qualifier = RefQualifierKind::None;
		AccessKind access = AccessKind::Unknown;
		SourceRange source_range;
	};

	struct ConstructorModel
	{
		std::vector<ParameterModel> parameters = {};
		std::vector<std::string> member_initializers = {};
		std::string signature_for_report;
		bool is_noexcept = false;
		SourceRange source_range = {};
	};

	struct DestructorModel
	{
		std::string signature_for_report;
		bool is_noexcept = false;
		SourceRange source_range = {};
	};

	struct StaticDataMemberModel
	{
		std::string name;
		std::string type_spelling;
		std::string signature_for_report;
		SourceRange source_range = {};
	};

	struct UnsupportedItem
	{
		UnsupportedReasonCode reason_code = UnsupportedReasonCode::Unknown;
		std::string kind;
		std::string class_name;
		std::string name;
		std::string member_signature;
		std::string reason;
		std::string suggested_action;
		SourceRange source_range;
	};

	struct ClassModel
	{
		std::string name;
		std::string qualified_name;
		std::vector<std::string> namespaces;
		std::string mock_name = {};
		std::string scoped_mock_name = {};
		std::string mock_header_name;
		std::string fake_source_name;
		HeaderModel source_header;
		std::vector<MethodModel> mock_methods;
		std::vector<MethodModel> fake_methods;
		std::vector<ConstructorModel> fake_constructors = {};
		std::vector<DestructorModel> fake_destructors = {};
		std::vector<StaticDataMemberModel> static_data_members = {};
		std::vector<UnsupportedItem> unsupported_items;
		bool interface_mock = false;
		bool mock_destructor_override = false;
		RegistryMode registry_mode = RegistryMode::ThreadLocal;
		bool link_ready = true;
		std::vector<std::string> link_readiness_reasons = {};
	};

	struct ProjectModel
	{
		std::vector<HeaderModel> headers;
		std::vector<ClassModel> classes;
		std::vector<UnsupportedItem> unsupported_items;
		std::vector<Diagnostic> diagnostics;
	};

	[[nodiscard]] std::string BuildQualifiedName(const std::vector<std::string>& namespaces,
												 const std::string& name);
	[[nodiscard]] std::string DefaultMockName(const std::string& class_name);
	[[nodiscard]] std::string DefaultScopedMockName(const std::string& class_name);
	[[nodiscard]] std::string DefaultMockHeaderName(const std::string& class_name);
	[[nodiscard]] std::string DefaultFakeSourceName(const std::string& class_name);
	void SortProjectModel(ProjectModel& project);
} // namespace mockfakegen
