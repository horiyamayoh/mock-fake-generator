#include "validation/GeneratedCompileValidator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mockfakegen
{
	namespace
	{
		constexpr std::size_t kStderrSummaryLimit = 4000U;
		constexpr std::string_view kReservedWriterStagingDirectoryName = ".mockfakegen-staging";

		struct PathValidationResult
		{
			bool ok = false;
			std::filesystem::path normalized_path;
			std::string message;
		};

		struct TempTree
		{
			std::filesystem::path root;
			std::string initialization_error;
			bool keep = false;

			explicit TempTree(std::filesystem::path requested_root)
				: root(std::move(requested_root))
			{
				std::error_code error;
				std::filesystem::remove_all(root, error);
				if (error)
				{
					initialization_error = "invalid validation artifact directory '" +
						root.generic_string() +
						"': failed to clear existing path: " + error.message();
					return;
				}

				std::filesystem::create_directories(root, error);
				if (error)
				{
					initialization_error = "invalid validation artifact directory '" +
						root.generic_string() + "': failed to create directory: " + error.message();
				}
			}

			TempTree(const TempTree&) = delete;
			TempTree& operator=(const TempTree&) = delete;

			~TempTree()
			{
				if (keep)
				{
					return;
				}

				std::error_code error;
				std::filesystem::remove_all(root, error);
			}

			[[nodiscard]] bool ok() const noexcept
			{
				return initialization_error.empty();
			}
		};

		struct ProcessResult
		{
			int exit_code = 1;
			std::string output;
			bool timed_out = false;
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

		[[nodiscard]] GeneratedCompileValidationStage StageForMode(ValidationMode mode) noexcept
		{
			switch (mode)
			{
				case ValidationMode::Syntax:
					return GeneratedCompileValidationStage::Syntax;
				case ValidationMode::None:
				case ValidationMode::Compile:
					return GeneratedCompileValidationStage::Compile;
				case ValidationMode::Link:
					return GeneratedCompileValidationStage::Link;
			}

			return GeneratedCompileValidationStage::Compile;
		}

		[[nodiscard]] bool IsParentReference(const std::filesystem::path& component)
		{
			return component == "..";
		}

		[[nodiscard]] bool IsCurrentReference(const std::filesystem::path& component)
		{
			return component == ".";
		}

		[[nodiscard]] bool HasRootEscape(const std::filesystem::path& path)
		{
			return path.is_absolute() || path.has_root_path() || path.has_root_name() ||
				path.has_root_directory();
		}

		[[nodiscard]] PathValidationResult
		ValidateGeneratedPath(const std::filesystem::path& raw_path)
		{
			if (raw_path.empty() || raw_path.generic_string().empty())
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "generated output path is empty.",
				};
			}

			if (HasRootEscape(raw_path))
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message =
						"generated output path must be relative and must not contain a root name.",
				};
			}

			for (const auto& component : raw_path)
			{
				if (IsParentReference(component))
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "generated output path must not contain '..' traversal.",
					};
				}
			}

			const auto normalized_path = raw_path.lexically_normal();
			if (normalized_path.empty() || IsCurrentReference(normalized_path) ||
				normalized_path.filename().empty())
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "generated output path must name a file.",
				};
			}

			if (HasRootEscape(normalized_path))
			{
				return PathValidationResult{
					.ok = false,
					.normalized_path = {},
					.message = "normalized generated output path escapes the output directory.",
				};
			}

			bool first_component = true;
			for (const auto& component : normalized_path)
			{
				if (IsParentReference(component))
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "normalized generated output path escapes the output directory.",
					};
				}
				if (first_component && component == kReservedWriterStagingDirectoryName)
				{
					return PathValidationResult{
						.ok = false,
						.normalized_path = {},
						.message = "generated output path uses a reserved writer staging path.",
					};
				}
				first_component = false;
			}

			return PathValidationResult{
				.ok = true,
				.normalized_path = normalized_path,
				.message = {},
			};
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

		[[nodiscard]] std::vector<char*> ArgvPointers(std::vector<std::string>& arguments)
		{
			std::vector<char*> argv;
			argv.reserve(arguments.size() + 1U);
			for (auto& argument : arguments)
			{
				argv.push_back(argument.data());
			}
			argv.push_back(nullptr);
			return argv;
		}

		void MoveToOwnProcessGroup(pid_t child) noexcept
		{
			if (::setpgid(child, child) != 0 && errno != EACCES && errno != EINVAL &&
				errno != ESRCH)
			{
				// Best effort: direct-child timeout handling remains as a fallback below.
			}
		}

		void KillProcessGroup(pid_t leader) noexcept
		{
			(void)::kill(-leader, SIGKILL);
			(void)::kill(leader, SIGKILL);
		}

		void WaitForChild(pid_t child, int& status) noexcept
		{
			while (::waitpid(child, &status, 0) < 0)
			{
				if (errno != EINTR)
				{
					break;
				}
			}
		}

		[[nodiscard]] bool ReadAvailableOutput(int fd, std::string& output)
		{
			std::array<char, 4096U> buffer{};
			bool pipe_open = true;
			while (pipe_open)
			{
				const auto count = ::read(fd, buffer.data(), buffer.size());
				if (count > 0)
				{
					output.append(buffer.data(), static_cast<std::size_t>(count));
					continue;
				}
				if (count == 0)
				{
					pipe_open = false;
					break;
				}
				if (errno == EINTR)
				{
					continue;
				}
				if (errno == EAGAIN)
				{
					break;
				}
				pipe_open = false;
			}
			return pipe_open;
		}

		[[nodiscard]] ProcessResult RunCommand(std::vector<std::string> arguments,
											   std::chrono::milliseconds timeout)
		{
			ProcessResult result;
			if (arguments.empty())
			{
				result.output = "compiler command is empty";
				return result;
			}

			int pipe_fds[2] = {-1, -1};
			if (::pipe(pipe_fds) != 0)
			{
				result.output = "failed to create compiler output pipe";
				return result;
			}

			const auto child = ::fork();
			if (child < 0)
			{
				::close(pipe_fds[0]);
				::close(pipe_fds[1]);
				result.output = "failed to start compiler process";
				return result;
			}

			if (child == 0)
			{
				MoveToOwnProcessGroup(0);
				::close(pipe_fds[0]);
				(void)::dup2(pipe_fds[1], STDOUT_FILENO);
				(void)::dup2(pipe_fds[1], STDERR_FILENO);
				::close(pipe_fds[1]);
				(void)::setenv("LC_ALL", "C", 1);
				auto argv = ArgvPointers(arguments);
				::execvp(argv[0], argv.data());
				::_exit(127);
			}

			MoveToOwnProcessGroup(child);
			::close(pipe_fds[1]);
			const auto current_flags = ::fcntl(pipe_fds[0], F_GETFL, 0);
			if (current_flags >= 0)
			{
				(void)::fcntl(pipe_fds[0], F_SETFL, current_flags | O_NONBLOCK);
			}
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			bool child_exited = false;
			bool pipe_open = true;
			int status = 0;

			while (pipe_open || !child_exited)
			{
				if (!child_exited)
				{
					const auto wait_result = ::waitpid(child, &status, WNOHANG);
					if (wait_result == child)
					{
						child_exited = true;
					}
				}

				if (!child_exited && timeout.count() > 0 &&
					std::chrono::steady_clock::now() >= deadline)
				{
					KillProcessGroup(child);
					WaitForChild(child, status);
					child_exited = true;
					result.timed_out = true;
					result.exit_code = 124;
				}

				if (pipe_open)
				{
					pollfd descriptor{
						.fd = pipe_fds[0],
						.events = POLLIN | POLLHUP,
						.revents = 0,
					};
					const int poll_timeout_ms = child_exited ? 0 : 50;
					const auto poll_result = ::poll(&descriptor, 1U, poll_timeout_ms);
					if (poll_result > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0)
					{
						pipe_open = ReadAvailableOutput(pipe_fds[0], result.output);
					}
					else if (poll_result < 0 && errno != EINTR)
					{
						pipe_open = false;
					}
				}
			}

			::close(pipe_fds[0]);
			if (!result.timed_out)
			{
				result.exit_code = DecodeExitStatus(status);
			}
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
						   GeneratedCompileValidationStage stage,
						   std::filesystem::path source_path,
						   std::filesystem::path validation_artifact_path,
						   std::string message,
						   std::string command = {},
						   std::string stderr_summary = {})
		{
			result.diagnostics.push_back(GeneratedCompileDiagnostic{
				.source_path = std::move(source_path),
				.validation_artifact_path = std::move(validation_artifact_path),
				.message = std::move(message),
				.command = std::move(command),
				.stderr_summary = std::move(stderr_summary),
				.stage = stage,
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

		[[nodiscard]] std::filesystem::path
		FakeObjectPath(const std::filesystem::path& tree_root,
					   const std::filesystem::path& fake_relative_path)
		{
			auto object_relative_path = std::filesystem::path("objects") / fake_relative_path;
			object_relative_path.replace_extension(".o");
			return tree_root / object_relative_path;
		}

		[[nodiscard]] std::filesystem::path
		MockHeaderSmokeSourcePath(const std::filesystem::path& tree_root,
								  const std::filesystem::path& mock_relative_path)
		{
			auto source_relative_path =
				std::filesystem::path("mock_header_smoke") / mock_relative_path;
			source_relative_path.replace_extension(".cpp");
			return tree_root / source_relative_path;
		}

		[[nodiscard]] std::filesystem::path
		MockHeaderObjectPath(const std::filesystem::path& tree_root,
							 const std::filesystem::path& mock_relative_path)
		{
			auto object_relative_path =
				std::filesystem::path("objects/mock_headers") / mock_relative_path;
			object_relative_path.replace_extension(".o");
			return tree_root / object_relative_path;
		}

		[[nodiscard]] std::filesystem::path
		AllMocksHeaderSmokeSourcePath(const std::filesystem::path& tree_root,
									  const std::filesystem::path& header_relative_path)
		{
			auto source_relative_path =
				std::filesystem::path("all_mocks_header_smoke") / header_relative_path;
			source_relative_path.replace_extension(".cpp");
			return tree_root / source_relative_path;
		}

		[[nodiscard]] std::filesystem::path
		AllMocksHeaderObjectPath(const std::filesystem::path& tree_root,
								 const std::filesystem::path& header_relative_path)
		{
			auto object_relative_path =
				std::filesystem::path("objects/all_mocks_header") / header_relative_path;
			object_relative_path.replace_extension(".o");
			return tree_root / object_relative_path;
		}

		[[nodiscard]] std::string BuildMockHeaderSmokeSource(const GeneratedFile& file)
		{
			std::ostringstream out;
			out << "#include \"" << file.relative_path.generic_string() << "\"\n"
				<< "\nnamespace\n"
				<< "{\n"
				<< "\tvoid mockfakegen_validate_mock_header() {}\n"
				<< "} // namespace\n";
			return out.str();
		}

		[[nodiscard]] std::string BuildAllMocksHeaderSmokeSource(const GeneratedFile& file)
		{
			std::ostringstream out;
			out << "#include \"" << file.relative_path.generic_string() << "\"\n"
				<< "\nnamespace\n"
				<< "{\n"
				<< "\tvoid mockfakegen_validate_all_mocks_header() {}\n"
				<< "} // namespace\n";
			return out.str();
		}

		[[nodiscard]] std::string BuildLinkMainSmokeSource()
		{
			return "int main()\n"
				   "{\n"
				   "\treturn 0;\n"
				   "}\n";
		}

		[[nodiscard]] bool IsSeparateStandardOption(std::string_view arg) noexcept
		{
			return arg == "-std" || arg == "--std";
		}

		[[nodiscard]] bool IsJoinedStandardOption(std::string_view arg) noexcept
		{
			return arg.starts_with("-std=") || arg.starts_with("--std=");
		}

		void NormalizeCxx23StandardArgs(std::vector<std::string>& args)
		{
			std::vector<std::string> normalized;
			normalized.reserve(args.size() + 1U);
			for (std::size_t index = 0U; index < args.size(); ++index)
			{
				const auto& arg = args[index];
				if (IsSeparateStandardOption(arg))
				{
					if (index + 1U < args.size())
					{
						++index;
					}
					continue;
				}
				if (IsJoinedStandardOption(arg))
				{
					continue;
				}
				normalized.push_back(arg);
			}
			normalized.push_back("-std=c++23");
			args = std::move(normalized);
		}

		[[nodiscard]] std::vector<std::string>
		BaseCompileArguments(const GeneratedCompileValidationOptions& options,
							 const std::filesystem::path& generated_root,
							 std::span<const std::string> extra_args)
		{
			std::vector<std::string> arguments;
			arguments.push_back(options.compiler.empty() ? std::string("c++")
														 : options.compiler.string());
			arguments.push_back("-I");
			arguments.push_back(generated_root.string());
			for (const auto& include_dir : options.include_dirs)
			{
				arguments.push_back("-I");
				arguments.push_back(include_dir.string());
			}
			for (const auto& extra_arg : extra_args)
			{
				arguments.push_back(extra_arg);
			}
			NormalizeCxx23StandardArgs(arguments);
			return arguments;
		}

		[[nodiscard]] std::span<const std::string>
		ValidationArgsForFile(const GeneratedCompileValidationOptions& options,
							  const GeneratedFile& file)
		{
			if (!file.source_class.has_value())
			{
				return options.extra_args;
			}
			for (const auto& source_args : options.source_args)
			{
				if (source_args.qualified_name == file.source_class->qualified_name &&
					source_args.source_header == file.source_class->source_header)
				{
					return source_args.args;
				}
			}
			return options.extra_args;
		}

		[[nodiscard]] std::filesystem::path
		ValidationCompilerForFile(const GeneratedCompileValidationOptions& options,
								  const GeneratedFile& file)
		{
			if (!file.source_class.has_value())
			{
				return options.compiler;
			}
			for (const auto& source_args : options.source_args)
			{
				if (source_args.qualified_name == file.source_class->qualified_name &&
					source_args.source_header == file.source_class->source_header &&
					!source_args.compiler.empty())
				{
					return source_args.compiler;
				}
			}
			return options.compiler;
		}

		[[nodiscard]] std::vector<std::string>
		BuildCompileCommandArguments(const GeneratedCompileValidationOptions& options,
									 const std::filesystem::path& generated_root,
									 const std::filesystem::path& source_path,
									 const std::filesystem::path& object_path,
									 std::span<const std::string> extra_args,
									 const std::filesystem::path& compiler)
		{
			auto command_options = options;
			command_options.compiler = compiler;
			auto arguments = BaseCompileArguments(command_options, generated_root, extra_args);
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

		[[nodiscard]] bool LooksLikeMissingGMockInclude(std::string_view stderr_summary)
		{
			return stderr_summary.find("fatal error: 'gmock/gmock.h' file not found") !=
				std::string_view::npos ||
				stderr_summary.find("fatal error: <gmock/gmock.h> file not found") !=
				std::string_view::npos ||
				stderr_summary.find("fatal error: gmock/gmock.h: No such file or directory") !=
				std::string_view::npos ||
				stderr_summary.find(
					"fatal error C1083: Cannot open include file: 'gmock/gmock.h'") !=
				std::string_view::npos ||
				stderr_summary.find(
					"fatal error C1083: Cannot open include file: \"gmock/gmock.h\"") !=
				std::string_view::npos;
		}

		[[nodiscard]] std::string FailureMessage(GeneratedCompileValidationStage stage,
												 const ProcessResult& process,
												 std::string_view stderr_summary)
		{
			if (process.timed_out)
			{
				return stage == GeneratedCompileValidationStage::Syntax
					? "generated output syntax validation timed out."
					: "generated output compile validation timed out.";
			}
			if (LooksLikeMissingGMockInclude(stderr_summary))
			{
				return "gMock include path is missing or invalid; compiler could not include "
					   "<gmock/gmock.h>.";
			}
			return stage == GeneratedCompileValidationStage::Syntax
				? "generated output syntax validation failed."
				: "generated output compile validation failed.";
		}

		[[nodiscard]] bool LooksLikeDuplicateSymbol(std::string_view stderr_summary)
		{
			return stderr_summary.find("multiple definition") != std::string_view::npos ||
				stderr_summary.find("duplicate symbol") != std::string_view::npos ||
				stderr_summary.find("already defined") != std::string_view::npos;
		}

		[[nodiscard]] std::string LinkFailureMessage(const ProcessResult& process,
													 std::string_view stderr_summary)
		{
			if (process.timed_out)
			{
				return "generated output link validation timed out.";
			}
			if (LooksLikeDuplicateSymbol(stderr_summary))
			{
				return "generated output link validation failed; do not link product .cpp files "
					   "together with generated FakeXXX.cpp files.";
			}
			return "generated output link validation failed.";
		}

		void RunCompileCommand(GeneratedCompileValidationResult& result,
							   const GeneratedCompileValidationOptions& options,
							   const std::filesystem::path& generated_root,
							   const std::filesystem::path& source_path,
							   const std::filesystem::path& object_path,
							   const std::filesystem::path& artifact_path,
							   std::span<const std::string> extra_args,
							   const std::filesystem::path& compiler)
		{
			if (options.mode != ValidationMode::Syntax)
			{
				std::error_code error;
				std::filesystem::create_directories(object_path.parent_path(), error);
				if (error)
				{
					AddDiagnostic(result,
								  GeneratedCompileValidationStage::Compile,
								  source_path,
								  artifact_path,
								  "failed to create validation object directory '" +
									  object_path.parent_path().generic_string() +
									  "': " + error.message());
					return;
				}
			}

			const auto arguments = BuildCompileCommandArguments(
				options, generated_root, source_path, object_path, extra_args, compiler);
			const auto command = BuildCommand(arguments);
			const auto process = RunCommand(arguments, options.command_timeout);
			result.commands.push_back(GeneratedCompileCommandResult{
				.source_path = source_path,
				.command = command,
				.exit_code = process.exit_code,
			});
			if (process.exit_code != 0)
			{
				const auto summary = StderrSummary(process.output);
				const auto stage = options.mode == ValidationMode::Syntax
					? GeneratedCompileValidationStage::Syntax
					: GeneratedCompileValidationStage::Compile;
				AddDiagnostic(result,
							  stage,
							  source_path,
							  artifact_path,
							  FailureMessage(stage, process, summary),
							  command,
							  summary);
			}
		}

		[[nodiscard]] bool HasPthreadArg(const std::vector<std::string>& args)
		{
			return std::any_of(args.begin(),
							   args.end(),
							   [](const auto& arg)
							   {
								   return arg == "-pthread" || arg == "-lpthread";
							   });
		}

		[[nodiscard]] std::vector<std::string>
		BuildLinkCommandArguments(const GeneratedCompileValidationOptions& options,
								  const std::filesystem::path& generated_root,
								  std::span<const std::filesystem::path> object_paths,
								  const std::filesystem::path& executable_path)
		{
			auto arguments = BaseCompileArguments(options, generated_root, options.extra_args);
			for (const auto& object_path : object_paths)
			{
				arguments.push_back(object_path.string());
			}
			for (const auto& link_file : options.link_files)
			{
				arguments.push_back(link_file.string());
			}
#if defined(__unix__)
			if (!HasPthreadArg(arguments))
			{
				arguments.push_back("-pthread");
			}
#endif
			arguments.push_back("-o");
			arguments.push_back(executable_path.string());
			return arguments;
		}

		void RunLinkCommand(GeneratedCompileValidationResult& result,
							const GeneratedCompileValidationOptions& options,
							const std::filesystem::path& generated_root,
							std::span<const std::filesystem::path> object_paths,
							const std::filesystem::path& executable_path,
							const std::filesystem::path& artifact_path)
		{
			const auto arguments =
				BuildLinkCommandArguments(options, generated_root, object_paths, executable_path);
			const auto command = BuildCommand(arguments);
			const auto process = RunCommand(arguments, options.command_timeout);
			result.commands.push_back(GeneratedCompileCommandResult{
				.source_path = executable_path,
				.command = command,
				.exit_code = process.exit_code,
			});
			if (process.exit_code != 0)
			{
				const auto summary = StderrSummary(process.output);
				AddDiagnostic(result,
							  GeneratedCompileValidationStage::Link,
							  executable_path,
							  artifact_path,
							  LinkFailureMessage(process, summary),
							  command,
							  summary);
			}
		}

		[[nodiscard]] std::filesystem::path
		ValidationRoot(const GeneratedCompileValidationOptions& options)
		{
			static std::atomic<unsigned long long> counter = 0U;
			const auto index = counter.fetch_add(1U, std::memory_order_relaxed);
			const auto base = options.artifact_dir.empty() ? std::filesystem::temp_directory_path()
														   : options.artifact_dir;
			return base /
				("mockfakegen_compile_validation_" + std::to_string(::getpid()) + "_" +
				 std::to_string(index));
		}
	} // namespace

	std::string_view ToString(GeneratedCompileValidationStage stage) noexcept
	{
		switch (stage)
		{
			case GeneratedCompileValidationStage::Syntax:
				return "syntax";
			case GeneratedCompileValidationStage::Compile:
				return "compile";
			case GeneratedCompileValidationStage::Link:
				return "link";
		}

		return "compile";
	}

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

		TempTree tree(ValidationRoot(options));
		if (!tree.ok())
		{
			AddDiagnostic(
				result, StageForMode(options.mode), tree.root, {}, tree.initialization_error);
			return result;
		}

		const auto generated_root = tree.root / "generated";
		const auto artifact_path =
			options.keep_failed_artifacts ? tree.root : std::filesystem::path{};
		std::vector<GeneratedFile> staged_files;
		std::map<std::string, std::filesystem::path> staged_sources_by_path;
		for (const auto& file : CxxFiles(files))
		{
			auto path_result = ValidateGeneratedPath(file.relative_path);
			if (!path_result.ok)
			{
				AddDiagnostic(result,
							  StageForMode(options.mode),
							  file.relative_path,
							  artifact_path,
							  "invalid generated validation path '" +
								  file.relative_path.generic_string() +
								  "': " + path_result.message);
				tree.keep = options.keep_failed_artifacts;
				return result;
			}

			const auto output_path =
				(generated_root / path_result.normalized_path).lexically_normal();
			const auto output_key = output_path.generic_string();
			const auto [source, inserted] =
				staged_sources_by_path.emplace(output_key, file.relative_path);
			if (!inserted)
			{
				AddDiagnostic(result,
							  StageForMode(options.mode),
							  output_path,
							  artifact_path,
							  "validation output path collision: generated files '" +
								  source->second.generic_string() + "' and '" +
								  file.relative_path.generic_string() + "' both map to '" +
								  output_key + "'.");
				tree.keep = options.keep_failed_artifacts;
				return result;
			}

			auto staged_file = file;
			staged_file.relative_path = std::move(path_result.normalized_path);
			staged_files.push_back(std::move(staged_file));
		}

		for (const auto& file : staged_files)
		{
			const auto output_path = generated_root / file.relative_path;
			if (!WriteText(output_path, file.content))
			{
				AddDiagnostic(result,
							  StageForMode(options.mode),
							  output_path,
							  artifact_path,
							  "failed to write generated file for validation.");
				tree.keep = options.keep_failed_artifacts;
				return result;
			}
		}

		const auto& sorted_files = staged_files;
		std::map<std::string, std::filesystem::path> fake_object_paths;
		std::map<std::string, std::filesystem::path> object_sources_by_path;
		if (options.mode != ValidationMode::Syntax)
		{
			for (const auto& file : sorted_files)
			{
				if (file.kind != GeneratedFileKind::FakeSource)
				{
					continue;
				}

				const auto object_path = FakeObjectPath(tree.root, file.relative_path);
				const auto object_key = object_path.lexically_normal().generic_string();
				const auto [source, inserted] =
					object_sources_by_path.emplace(object_key, file.relative_path);
				if (!inserted)
				{
					AddDiagnostic(result,
								  GeneratedCompileValidationStage::Compile,
								  generated_root / file.relative_path,
								  artifact_path,
								  "validation object path collision: fake sources '" +
									  source->second.generic_string() + "' and '" +
									  file.relative_path.generic_string() + "' both map to '" +
									  object_key + "'.");
					tree.keep = options.keep_failed_artifacts;
					return result;
				}
				fake_object_paths.emplace(file.relative_path.generic_string(), object_path);
			}
		}

		std::vector<std::filesystem::path> object_paths;
		for (const auto& file : sorted_files)
		{
			if (file.kind != GeneratedFileKind::MockHeader)
			{
				continue;
			}

			const auto smoke_source = MockHeaderSmokeSourcePath(tree.root, file.relative_path);
			if (!WriteText(smoke_source, BuildMockHeaderSmokeSource(file)))
			{
				AddDiagnostic(result,
							  StageForMode(options.mode),
							  smoke_source,
							  artifact_path,
							  "failed to write mock header smoke source.");
				tree.keep = options.keep_failed_artifacts;
				return result;
			}

			const auto object_path = MockHeaderObjectPath(tree.root, file.relative_path);
			RunCompileCommand(result,
							  options,
							  generated_root,
							  smoke_source,
							  object_path,
							  artifact_path,
							  ValidationArgsForFile(options, file),
							  ValidationCompilerForFile(options, file));
			if (options.mode != ValidationMode::Syntax)
			{
				object_paths.push_back(object_path);
			}
		}

		for (const auto& file : sorted_files)
		{
			if (file.kind != GeneratedFileKind::AllMocksHeader)
			{
				continue;
			}

			const auto smoke_source = AllMocksHeaderSmokeSourcePath(tree.root, file.relative_path);
			if (!WriteText(smoke_source, BuildAllMocksHeaderSmokeSource(file)))
			{
				AddDiagnostic(result,
							  StageForMode(options.mode),
							  smoke_source,
							  artifact_path,
							  "failed to write AllMocks header smoke source.");
				tree.keep = options.keep_failed_artifacts;
				return result;
			}

			const auto object_path = AllMocksHeaderObjectPath(tree.root, file.relative_path);
			RunCompileCommand(result,
							  options,
							  generated_root,
							  smoke_source,
							  object_path,
							  artifact_path,
							  ValidationArgsForFile(options, file),
							  ValidationCompilerForFile(options, file));
			if (options.mode != ValidationMode::Syntax)
			{
				object_paths.push_back(object_path);
			}
		}

		if (options.mode == ValidationMode::Link && options.link_files.empty())
		{
			const auto smoke_source = tree.root / "link_main_smoke.cpp";
			if (!WriteText(smoke_source, BuildLinkMainSmokeSource()))
			{
				AddDiagnostic(result,
							  GeneratedCompileValidationStage::Compile,
							  smoke_source,
							  artifact_path,
							  "failed to write link main smoke source.");
				tree.keep = options.keep_failed_artifacts;
				return result;
			}
			const auto object_path = tree.root / "objects/link_main_smoke.o";
			RunCompileCommand(result,
							  options,
							  generated_root,
							  smoke_source,
							  object_path,
							  artifact_path,
							  options.extra_args,
							  options.compiler);
			object_paths.push_back(object_path);
		}

		for (const auto& file : sorted_files)
		{
			if (file.kind != GeneratedFileKind::FakeSource)
			{
				continue;
			}

			const auto source_path = generated_root / file.relative_path;
			const auto object_path = options.mode == ValidationMode::Syntax
				? tree.root / (file.relative_path.stem().string() + ".o")
				: fake_object_paths.at(file.relative_path.generic_string());
			RunCompileCommand(result,
							  options,
							  generated_root,
							  source_path,
							  object_path,
							  artifact_path,
							  ValidationArgsForFile(options, file),
							  ValidationCompilerForFile(options, file));
			if (options.mode != ValidationMode::Syntax)
			{
				object_paths.push_back(object_path);
			}
		}

		if (options.mode == ValidationMode::Link && result.ok())
		{
			RunLinkCommand(result,
						   options,
						   generated_root,
						   object_paths,
						   tree.root / "generated_link_smoke",
						   artifact_path);
		}

		tree.keep = options.keep_failed_artifacts && !result.ok();
		return result;
	}
} // namespace mockfakegen
