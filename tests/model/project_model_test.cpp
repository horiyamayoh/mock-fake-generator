#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "generator/CodeGenerator.h"
#include "model/ProjectModel.h"

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "EXPECTATION FAILED: " << message << '\n';
			std::exit(1);
		}
	}

	[[nodiscard]] mockfakegen::MethodModel Method(std::string name)
	{
		return mockfakegen::MethodModel{
			.name = name,
			.qualified_owner_name = "app::Hoge",
			.return_type_spelling = name == "Finalize" ? "void" : "bool",
			.gmock_return_type_spelling = name == "Finalize" ? "void" : "bool",
			.parameters = {},
			.signature_for_report = "app::Hoge::" + name + "()",
			.is_noexcept = name == "DoSomething",
			.access = mockfakegen::AccessKind::Public,
			.source_range = {},
		};
	}

	[[nodiscard]] mockfakegen::ClassModel HogeClass()
	{
		const mockfakegen::HeaderModel header{
			.absolute_path = "/repo/include/app/Hoge.h",
			.project_relative_path = "include/app/Hoge.h",
			.include_spelling = "include/app/Hoge.h",
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};

		return mockfakegen::ClassModel{
			.name = "Hoge",
			.qualified_name = mockfakegen::BuildQualifiedName({"app"}, "Hoge"),
			.namespaces = {"app"},
			.mock_name = mockfakegen::DefaultMockName("Hoge"),
			.mock_header_name = mockfakegen::DefaultMockHeaderName("Hoge"),
			.fake_source_name = mockfakegen::DefaultFakeSourceName("Hoge"),
			.source_header = header,
			.mock_methods = {Method("Finalize"), Method("DoSomething")},
			.fake_methods = {Method("Finalize"), Method("DoSomething")},
			.unsupported_items =
				{
					mockfakegen::UnsupportedItem{
						.reason_code = mockfakegen::UnsupportedReasonCode::FunctionTemplate,
						.kind = "function_template",
						.class_name = "app::Hoge",
						.name = "Get",
						.member_signature = "app::Hoge::Get()",
						.reason = "function template is not supported",
						.suggested_action = "write a hand-authored mock",
						.source_range = {},
					},
				},
		};
	}

	void ConstructsClassAndMethodModel()
	{
		const auto class_model = HogeClass();

		Expect(class_model.name == "Hoge", "class name should be stored");
		Expect(class_model.qualified_name == "app::Hoge", "qualified name should be stored");
		Expect(class_model.namespaces.size() == 1U && class_model.namespaces[0] == "app",
			   "namespace parts should be stored");
		Expect(class_model.mock_name == "MockHoge", "mock name should be stored");
		Expect(class_model.mock_header_name == "MockHoge.h", "mock header name should be stored");
		Expect(class_model.fake_source_name == "FakeHoge.cpp", "fake source name should be stored");
		Expect(class_model.source_header.parsed_by_synthetic_tu,
			   "source header should keep parse mode");
		Expect(class_model.mock_methods.size() == 2U, "mock methods should be stored separately");
		Expect(class_model.fake_methods.size() == 2U, "fake methods should be stored separately");
		Expect(class_model.unsupported_items.size() == 1U,
			   "unsupported items should be separate from generated methods");
		Expect(class_model.mock_methods[1].is_noexcept, "method qualifiers should be stored");
	}

	void SortsProjectModelDeterministically()
	{
		mockfakegen::HeaderModel z_header;
		z_header.project_relative_path = "include/z/Z.h";
		mockfakegen::HeaderModel a_header;
		a_header.project_relative_path = "include/a/A.h";
		mockfakegen::ClassModel zoo_class;
		zoo_class.qualified_name = "z::Zoo";

		mockfakegen::ProjectModel project{
			.headers =
				{
					z_header,
					a_header,
				},
			.classes =
				{
					zoo_class,
					HogeClass(),
				},
			.diagnostics =
				{
					mockfakegen::Diagnostic{
						.severity = mockfakegen::DiagnosticSeverity::Warning,
						.code = mockfakegen::DiagnosticCode::UnsupportedConstruct,
						.source_range = {},
						.message = "unsupported item",
					},
				},
		};

		mockfakegen::SortProjectModel(project);

		Expect(project.headers[0].project_relative_path == "include/a/A.h",
			   "headers should sort by project-relative path");
		Expect(project.classes[0].qualified_name == "app::Hoge",
			   "classes should sort by qualified name");
		Expect(project.classes[0].mock_methods[0].name == "DoSomething",
			   "methods should sort by signature");
		Expect(project.classes[0].unsupported_items[0].kind == "function_template",
			   "unsupported items should remain available after sorting");
		Expect(project.diagnostics[0].message == "unsupported item",
			   "diagnostics should stay attached to project");
	}

	void ClassModelCanFeedMinimalGenerator()
	{
		const auto files = mockfakegen::GenerateMinimalMockFake(HogeClass());

		Expect(files.size() == 3U, "ClassModel overload should produce minimal generated files");
		Expect(files[0].relative_path == "FakeHoge.cpp",
			   "ClassModel generator output should be sorted");
	}
} // namespace

int main()
{
	ConstructsClassAndMethodModel();
	SortsProjectModelDeterministically();
	ClassModelCanFeedMinimalGenerator();
	return 0;
}
