#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "../GoldenDiff.h"
#include "clang/ClassExtractor.h"
#include "clang/SyntheticTuParser.h"
#include "generator/CodeGenerator.h"

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

	[[nodiscard]] std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		Expect(stream.good(), "fixture file should be readable");
		return buffer.str();
	}

	[[nodiscard]] const mockfakegen::MethodModel&
	FindMethod(const mockfakegen::ClassModel& class_model, std::string_view signature)
	{
		for (const auto& method : class_model.mock_methods)
		{
			if (method.signature_for_report == signature)
			{
				return method;
			}
		}
		std::cerr << "missing method signature: " << signature << '\n';
		std::exit(1);
	}

	[[nodiscard]] const mockfakegen::MethodModel&
	FindQualifiedMethod(const mockfakegen::ClassModel& class_model,
						std::string_view name,
						bool is_const,
						mockfakegen::RefQualifierKind ref_qualifier)
	{
		for (const auto& method : class_model.mock_methods)
		{
			if (method.name == name && method.is_const == is_const &&
				method.ref_qualifier == ref_qualifier)
			{
				return method;
			}
		}
		std::cerr << "missing qualified method: " << name << '\n';
		std::exit(1);
	}

	void GeneratesForwardingMockAndFake()
	{
		const auto source_dir = std::filesystem::path(MOCKFAKEGEN_SOURCE_DIR);
		const auto product_dir = source_dir / "tests/fixtures/forwarding/product";
		const auto generated_dir = source_dir / "tests/fixtures/forwarding/generated";
		const auto parse_result = mockfakegen::ParseHeaderWithSyntheticTu({
			.header_path = product_dir / "Forwarding.h",
			.project_root = product_dir,
		});
		Expect(parse_result.success, "forwarding fixture should parse");

		const mockfakegen::HeaderModel header{
			.absolute_path = parse_result.header.header_path,
			.project_relative_path = parse_result.header.include_spelling,
			.include_spelling = parse_result.header.include_spelling,
			.parsed_by_real_tu = false,
			.parsed_by_synthetic_tu = true,
		};
		const auto extraction =
			mockfakegen::ExtractClassDefinitionsFromAst(*parse_result.ast, header);
		Expect(extraction.classes.size() == 1U, "forwarding class should be extracted");

		const auto& class_model = extraction.classes[0];
		Expect(class_model.mock_methods.size() == 12U,
			   "all forwarding methods should be generated");
		Expect(class_model.fake_methods.size() == 12U,
			   "all forwarding methods should be fake candidates");
		Expect(class_model.unsupported_items.size() == 1U,
			   "unsafe const by-value move-only method should be unsupported");
		Expect(class_model.unsupported_items[0].kind == "const_move_only_by_value_parameter",
			   "unsafe const by-value move-only parameter should have a specific kind");
		Expect(class_model.unsupported_items[0].member_signature.find("ConstMoveOnly") !=
				   std::string::npos,
			   "unsafe const by-value diagnostic should identify the method");

		const auto& const_select =
			FindQualifiedMethod(class_model, "Select", true, mockfakegen::RefQualifierKind::None);
		Expect(const_select.is_const, "const overload should be marked const");
		const auto& lvalue_ref = FindQualifiedMethod(
			class_model, "RefSelect", false, mockfakegen::RefQualifierKind::LValue);
		Expect(lvalue_ref.ref_qualifier == mockfakegen::RefQualifierKind::LValue,
			   "lvalue ref-qualified overload should be marked");
		const auto& rvalue_ref = FindQualifiedMethod(
			class_model, "RefSelect", false, mockfakegen::RefQualifierKind::RValue);
		Expect(rvalue_ref.ref_qualifier == mockfakegen::RefQualifierKind::RValue,
			   "rvalue ref-qualified overload should be marked");
		const auto& move_unique =
			FindMethod(class_model, "Forwarding::MoveUnique(std::unique_ptr<int>)");
		Expect(move_unique.parameters[0].is_nonconst_by_value,
			   "move-only by-value parameter should be moved");
		const auto& move_rvalue = FindMethod(class_model, "Forwarding::MoveRValue(std::string&&)");
		Expect(move_rvalue.parameters[0].is_rvalue_ref,
			   "rvalue reference parameter should be moved");
		const auto& keep_const_value =
			FindMethod(class_model, "Forwarding::KeepConstValue(const std::string)");
		Expect(!keep_const_value.parameters[0].is_nonconst_by_value,
			   "const by-value parameter should not be moved");

		const auto generated = mockfakegen::GenerateMinimalMockFake(class_model);
		for (const auto& file : generated)
		{
			if (file.relative_path == "MockFakeRuntime.h")
			{
				continue;
			}
			const auto path = generated_dir / file.relative_path;
			const auto expected = ReadText(path);
			mockfakegen_fixture::ExpectGoldenTextEqual(file.content, expected, path);
		}
	}
} // namespace

int main()
{
	GeneratesForwardingMockAndFake();
	return 0;
}
