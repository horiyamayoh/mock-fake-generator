if(NOT DEFINED MOCKFAKEGEN_BINARY_DIR)
	message(FATAL_ERROR "MOCKFAKEGEN_BINARY_DIR is required.")
endif()
if(NOT DEFINED MOCKFAKEGEN_INSTALL_PREFIX)
	message(FATAL_ERROR "MOCKFAKEGEN_INSTALL_PREFIX is required.")
endif()
if(NOT DEFINED MOCKFAKEGEN_INSTALL_BINDIR)
	message(FATAL_ERROR "MOCKFAKEGEN_INSTALL_BINDIR is required.")
endif()
if(NOT DEFINED MOCKFAKEGEN_EXECUTABLE_SUFFIX)
	set(MOCKFAKEGEN_EXECUTABLE_SUFFIX "")
endif()

file(REMOVE_RECURSE "${MOCKFAKEGEN_INSTALL_PREFIX}")

execute_process(
	COMMAND
		"${CMAKE_COMMAND}"
		--install
		"${MOCKFAKEGEN_BINARY_DIR}"
		--prefix
		"${MOCKFAKEGEN_INSTALL_PREFIX}"
	RESULT_VARIABLE install_result
	OUTPUT_VARIABLE install_stdout
	ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
	message(FATAL_ERROR "cmake --install failed:\n${install_stdout}\n${install_stderr}")
endif()

if(IS_ABSOLUTE "${MOCKFAKEGEN_INSTALL_BINDIR}")
	set(installed_executable "${MOCKFAKEGEN_INSTALL_BINDIR}/mockfakegen${MOCKFAKEGEN_EXECUTABLE_SUFFIX}")
else()
	set(installed_executable "${MOCKFAKEGEN_INSTALL_PREFIX}/${MOCKFAKEGEN_INSTALL_BINDIR}/mockfakegen${MOCKFAKEGEN_EXECUTABLE_SUFFIX}")
endif()

if(NOT EXISTS "${installed_executable}")
	message(FATAL_ERROR "installed mockfakegen was not found at ${installed_executable}")
endif()

execute_process(
	COMMAND "${installed_executable}" --help
	RESULT_VARIABLE help_result
	OUTPUT_VARIABLE help_stdout
	ERROR_VARIABLE help_stderr
)
if(NOT help_result EQUAL 0)
	message(FATAL_ERROR "installed mockfakegen --help failed:\n${help_stdout}\n${help_stderr}")
endif()
if(NOT help_stdout MATCHES "Usage:")
	message(FATAL_ERROR "installed mockfakegen --help did not print usage:\n${help_stdout}")
endif()
