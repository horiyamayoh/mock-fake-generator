#include "validation/GeneratedCompileValidator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <sys/wait.h>

namespace mockfakegen
{
	namespace
	{
		constexpr std::size_t kStderrSummaryLimit = 4000U;

		struct TempTree
		{
			std::filesystem::path root;

			TempTree()
				: root(std::filesystem::temp_directory_path() /
					   ("mockfakegen_compile_validation_" + std::to_string(UniqueSuffix())))
			{
				std::filesystem::remove_all(root);
				std::filesystem::create_directories(root);
			}

			TempTree(const TempTree&) = delete;
			TempTree& operator=(const TempTree&) = delete;

			~TempTree()
			{
				std::error_code error;
				std::filesystem::remove_all(root, error);
			}

		  private:
			[[nodiscard]] static long long UniqueSuffix()
			{
				return std::chrono::steady_clock::now().time_since_epoch().count();
			}
		};

		struct ProcessResult
		{
			int exit_code = 1;
			std::string output;
		};

		[[nodiscard]] bool IsCxxValidationInput(GeneratedFileKind kind) noexcept
		{
			switch (kind)
			{
				case GeneratedFileKind::RuntimeHeader:
				case GeneratedFileKind::MockHeader:
				case GeneratedFileKind::FakeSource:
				case GeneratedFileKind::AllMocksHeader:
					return true;
				case GeneratedFileKind::CMakeFragment:
				case GeneratedFileKind::Manifest:
				case GeneratedFileKind::Report:
					return false;
			}

			return false;
		}

		[[nodiscard]] std::string ShellQuote(std::string_view value)
		{
			std::string quoted;
			quoted.reserve(value.size() + 2U);
			quoted += '\'';
			for (const char character : value)
			{
				if (character == '\'')
				{
					quoted += "'\\''";
				}
				else
				{
					quoted += character;
				}
			}
			quoted += '\'';
			return quoted;
		}

		[[nodiscard]] std::string BuildCommand(std::span<const std::string> arguments)
		{
			std::string command;
			for (const auto& argument : arguments)
			{
				if (!command.empty())
				{
					command += ' ';
				}
				command += ShellQuote(argument);
			}
			return command;
		}

		[[nodiscard]] int DecodeExitStatus(int status) noexcept
		{
			if (WIFEXITED(status))
			{
				return WEXITSTATUS(status);
			}
			if (WIFSIGNALED(status))
			{
				return 128 + WTERMSIG(status);
			}
			return 1;
		}

		[[nodiscard]] ProcessResult RunCommand(const std::string& command)
		{
			ProcessResult result;
			std::array<char, 4096U> buffer{};
			const auto shell_command = command + " 2>&1";
			FILE* pipe = ::popen(shell_command.c_str(), "r");
			if (pipe == nullptr)
			{
				result.output = "failed to start compiler process";
				return result;
			}

			while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
			{
				result.output += buffer.data();
			}

			const auto status = ::pclose(pipe);
			result.exit_code = DecodeExitStatus(status);
			return result;
		}

		[[nodiscard]] std::string StderrSummary(std::string output)
		{
			if (output.size() > kStderrSummaryLimit)
			{
				output.resize(kStderrSummaryLimit);
				output += "\n... truncated ...";
			}
			return output;
		}

		[[nodiscard]] bool WriteText(const std::filesystem::path& path, const std::string& content)
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return false;
			}

			stream << content;
			stream.close();
			return stream.good();
		}

		void AddDiagnostic(GeneratedCompileValidationResult& result,
						   std::filesystem::path source_path,
						   std::string message,
						   std::string command = {},
						   std::string stderr_summary = {})
		{
			result.diagnostics.push_back(GeneratedCompileDiagnostic{
				.source_path = std::move(source_path),
				.message = std::move(message),
				.command = std::move(command),
				.stderr_summary = std::move(stderr_summary),
			});
		}

		[[nodiscard]] std::vector<GeneratedFile> CxxFiles(std::span<const GeneratedFile> files)
		{
			std::vector<GeneratedFile> cxx_files;
			for (const auto& file : files)
			{
				if (IsCxxValidationInput(file.kind))
				{
					cxx_files.push_back(file);
				}
			}
			SortGeneratedFiles(cxx_files);
			return cxx_files;
		}

		[[nodiscard]] std::vector<std::filesystem::path>
		MockHeaders(std::span<const GeneratedFile> files)
		{
			std::vector<std::filesystem::path> headers;
			for (const auto& file : files)
			{
				if (file.kind == GeneratedFileKind::MockHeader)
				{
					headers.push_back(file.relative_path);
				}
			}
			std::sort(headers.begin(),
					  headers.end(),
					  [](const auto& lhs, const auto& rhs)
					  {
						  return lhs.generic_string() < rhs.generic_string();
					  });
			return headers;
		}

		[[nodiscard]] bool HasAllMocksHeader(std::span<const GeneratedFile> files)
		{
			return std::any_of(files.begin(),
							   files.end(),
							   [](const auto& file)
							   {
								   return file.kind == GeneratedFileKind::AllMocksHeader;
							   });
		}

		[[nodiscard]] std::string BuildMockHeadersSmokeSource(std::span<const GeneratedFile> files)
		{
			std::ostringstream out;
			if (HasAllMocksHeader(files))
			{
				out << "#include \"AllMocks.h\"\n";
			}
			else
			{
				for (const auto& header : MockHeaders(files))
				{
					out << "#include \"" << header.generic_string() << "\"\n";
				}
			}
			out << "\nint main()\n"
				<< "{\n"
				<< "\treturn 0;\n"
				<< "}\n";
			return out.str();
		}

		[[nodiscard]] std::vector<std::string>
		BaseCompileArguments(const GeneratedCompileValidationOptions& options,
							 const std::filesystem::path& generated_root)
		{
			std::vector<std::string> arguments;
			arguments.push_back(options.compiler.empty() ? std::string("c++")
														 : options.compiler.string());
			arguments.push_back("-std=c++23");
			arguments.push_back("-I");
			arguments.push_back(generated_root.string());
			for (const auto& include_dir : options.include_dirs)
			{
				arguments.push_back("-I");
				arguments.push_back(include_dir.string());
			}
			for (const auto& extra_arg : options.extra_args)
			{
				arguments.push_back(extra_arg);
			}
			return arguments;
		}

		[[nodiscard]] std::vector<std::string>
		BuildCompileCommandArguments(const GeneratedCompileValidationOptions& options,
									 const std::filesystem::path& generated_root,
									 const std::filesystem::path& source_path,
									 const std::filesystem::path& object_path)
		{
			auto arguments = BaseCompileArguments(options, generated_root);
			if (options.mode == ValidationMode::Syntax)
			{
				arguments.push_back("-fsyntax-only");
				arguments.push_back(source_path.string());
				return arguments;
			}

			arguments.push_back("-c");
			arguments.push_back(source_path.string());
			arguments.push_back("-o");
			arguments.push_back(object_path.string());
			return arguments;
		}

		[[nodiscard]] std::string FailureMessage(std::string_view stderr_summary)
		{
			if (stderr_summary.find("gmock/gmock.h") != std::string_view::npos)
			{
				return "gMock include path is missing or invalid; compiler could not include "
					   "<gmock/gmock.h>.";
			}
			return "generated output compile validation failed.";
		}

		void RunCompileCommand(GeneratedCompileValidationResult& result,
							   const GeneratedCompileValidationOptions& options,
							   const std::filesystem::path& generated_root,
							   const std::filesystem::path& source_path,
							   const std::filesystem::path& object_path)
		{
			const auto arguments =
				BuildCompileCommandArguments(options, generated_root, source_path, object_path);
			const auto command = BuildCommand(arguments);
			const auto process = RunCommand(command);
			result.commands.push_back(GeneratedCompileCommandResult{
				.source_path = source_path,
				.command = command,
				.exit_code = process.exit_code,
			});
			if (process.exit_code != 0)
			{
				const auto summary = StderrSummary(process.output);
				AddDiagnostic(result, source_path, FailureMessage(summary), command, summary);
			}
		}
	} // namespace

	GeneratedCompileValidationResult
	ValidateGeneratedOutputCompile(const GeneratedCompileValidationOptions& options,
								   std::span<const GeneratedFile> files)
	{
		GeneratedCompileValidationResult result;
		if (options.mode == ValidationMode::None)
		{
			result.skipped = true;
			return result;
		}

		TempTree tree;
		const auto generated_root = tree.root / "generated";
		for (const auto& file : CxxFiles(files))
		{
			const auto output_path = generated_root / file.relative_path;
			if (!WriteText(output_path, file.content))
			{
				AddDiagnostic(
					result, output_path, "failed to write generated file for validation.");
				return result;
			}
		}

		const auto smoke_source = tree.root / "mock_headers_smoke.cpp";
		if (!WriteText(smoke_source, BuildMockHeadersSmokeSource(files)))
		{
			AddDiagnostic(result, smoke_source, "failed to write mock header smoke source.");
			return result;
		}

		RunCompileCommand(
			result, options, generated_root, smoke_source, tree.root / "mock_headers_smoke.o");

		const auto sorted_files = CxxFiles(files);
		for (const auto& file : sorted_files)
		{
			if (file.kind != GeneratedFileKind::FakeSource)
			{
				continue;
			}

			const auto source_path = generated_root / file.relative_path;
			const auto object_path = tree.root / (file.relative_path.stem().string() + ".o");
			RunCompileCommand(result, options, generated_root, source_path, object_path);
		}

		return result;
	}
} // namespace mockfakegen
