cmake_minimum_required(VERSION 3.25)

project(mockfakegen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(MOCKFAKEGEN_BUILD_TESTS "Build mockfakegen tests." ON)
option(MOCKFAKEGEN_ENABLE_FORMAT_TARGETS "Enable format and check-format targets." ON)
option(MOCKFAKEGEN_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers." OFF)
option(MOCKFAKEGEN_ENABLE_STRICT_WARNINGS "Enable strict warning options." ON)
option(MOCKFAKEGEN_WARNINGS_AS_ERRORS "Treat project warnings as errors." ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MockFakeGenOptions)

set(MOCKFAKEGEN_LLVM_ROOT "" CACHE PATH "LLVM/Clang installation root containing lib/cmake/llvm and lib/cmake/clang.")

if(MOCKFAKEGEN_LLVM_ROOT)
	set(MOCKFAKEGEN_LLVM_PACKAGE_ROOT "${MOCKFAKEGEN_LLVM_ROOT}")
elseif(NOT LLVM_DIR AND NOT Clang_DIR)
	foreach(candidate IN ITEMS /usr/lib/llvm-18 /usr/lib/llvm-19 /usr/lib/llvm-17 /usr/lib/llvm-20)
		if(EXISTS "${candidate}/lib/libclangTooling.a"
		   AND EXISTS "${candidate}/lib/cmake/llvm/LLVMConfig.cmake"
		   AND EXISTS "${candidate}/lib/cmake/clang/ClangConfig.cmake"
		)
			set(MOCKFAKEGEN_LLVM_PACKAGE_ROOT "${candidate}")
			break()
		endif()
	endforeach()
endif()

if(MOCKFAKEGEN_LLVM_PACKAGE_ROOT)
	find_package(
		LLVM
		CONFIG
		REQUIRED
		PATHS "${MOCKFAKEGEN_LLVM_PACKAGE_ROOT}/lib/cmake/llvm"
		NO_DEFAULT_PATH
	)
	find_package(
		Clang
		CONFIG
		REQUIRED
		PATHS "${MOCKFAKEGEN_LLVM_PACKAGE_ROOT}/lib/cmake/clang"
		NO_DEFAULT_PATH
	)
else()
	find_package(LLVM CONFIG REQUIRED)
	find_package(Clang CONFIG REQUIRED)
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
message(STATUS "Found Clang package: ${Clang_DIR}")

add_library(mockfakegen_clang INTERFACE)
target_include_directories(
	mockfakegen_clang
	SYSTEM
	INTERFACE
		${LLVM_INCLUDE_DIRS}
		${CLANG_INCLUDE_DIRS}
)
separate_arguments(MOCKFAKEGEN_LLVM_DEFINITION_OPTIONS UNIX_COMMAND "${LLVM_DEFINITIONS}")
list(REMOVE_ITEM MOCKFAKEGEN_LLVM_DEFINITION_OPTIONS -D_GNU_SOURCE)
target_compile_options(mockfakegen_clang INTERFACE ${MOCKFAKEGEN_LLVM_DEFINITION_OPTIONS})

set(
	MOCKFAKEGEN_CLANG_TARGETS
	clangTooling
	clangAST
	clangASTMatchers
	clangFrontend
	clangBasic
	clangLex
	clangSerialization
	clangDriver
	clangParse
	clangSema
	clangAnalysis
	clangEdit
	clangToolingCore
)

foreach(clang_target IN LISTS MOCKFAKEGEN_CLANG_TARGETS)
	if(NOT TARGET ${clang_target})
		message(FATAL_ERROR "Required Clang target ${clang_target} was not provided by ClangConfig.cmake.")
	endif()
	target_link_libraries(mockfakegen_clang INTERFACE ${clang_target})
endforeach()

set(MOCKFAKEGEN_KET_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ket/modules")

add_library(
	mockfakegen_ket STATIC
	"${MOCKFAKEGEN_KET_MODULE_DIR}/ascii/ket_ascii.cpp"
	"${MOCKFAKEGEN_KET_MODULE_DIR}/file/ket_file.cpp"
)
target_include_directories(
	mockfakegen_ket
	SYSTEM
	PUBLIC
		"${MOCKFAKEGEN_KET_MODULE_DIR}/ascii"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/cli"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/concurrency"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/contract"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/file"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/parse"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/scope"
		"${MOCKFAKEGEN_KET_MODULE_DIR}/string"
)
target_compile_features(mockfakegen_ket PUBLIC cxx_std_23)
set_target_properties(
	mockfakegen_ket
	PROPERTIES
		CXX_EXTENSIONS OFF
)
mockfakegen_apply_sanitizers(mockfakegen_ket)

add_library(
	mockfakegen_core
		src/Config.cpp
		src/clang/ClassExtractor.cpp
		src/clang/SyntheticTuParser.cpp
		src/generator/CodeGenerator.cpp
		src/HeaderScanner.cpp
		src/model/GeneratedFile.cpp
		src/model/ProjectModel.cpp
		src/output/OutputWriter.cpp
		src/runtime_template/RuntimeTemplate.cpp
		src/support/CliSupport.cpp
		src/validation/GeneratedOutputCheck.cpp
)
target_include_directories(mockfakegen_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mockfakegen_core PUBLIC mockfakegen_clang PRIVATE mockfakegen_ket)
target_compile_features(mockfakegen_core PUBLIC cxx_std_23)
set_target_properties(
	mockfakegen_core
	PROPERTIES
		CXX_EXTENSIONS OFF
)
mockfakegen_apply_strict_warnings(mockfakegen_core)
mockfakegen_apply_sanitizers(mockfakegen_core)

add_executable(mockfakegen src/main.cpp)
target_link_libraries(mockfakegen PRIVATE mockfakegen_core mockfakegen_clang)
target_compile_features(mockfakegen PRIVATE cxx_std_23)
set_target_properties(
	mockfakegen
	PROPERTIES
		CXX_EXTENSIONS OFF
)
mockfakegen_apply_strict_warnings(mockfakegen)
mockfakegen_apply_sanitizers(mockfakegen)

if(MOCKFAKEGEN_ENABLE_FORMAT_TARGETS)
	find_package(Python3 COMPONENTS Interpreter)

	if(Python3_Interpreter_FOUND)
		add_custom_target(
			format
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/format.py" --fix
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			COMMENT "Formatting mockfakegen sources"
		)

		add_custom_target(
			check-format
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/format.py" --check
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			COMMENT "Checking mockfakegen source formatting"
		)
	endif()
endif()

if(BUILD_TESTING AND MOCKFAKEGEN_BUILD_TESTS)
	find_package(GTest QUIET)
	if(NOT TARGET GTest::gmock_main AND NOT TARGET gmock_main)
		include(FetchContent)
		set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
		set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
		FetchContent_Declare(
			googletest
			GIT_REPOSITORY https://github.com/google/googletest.git
			GIT_TAG v1.14.0
		)
		FetchContent_MakeAvailable(googletest)
	endif()

	if(TARGET GTest::gmock_main)
		set(MOCKFAKEGEN_GMOCK_MAIN_TARGET GTest::gmock_main)
	elseif(TARGET gmock_main)
		set(MOCKFAKEGEN_GMOCK_MAIN_TARGET gmock_main)
	else()
		message(FATAL_ERROR "gMock is required for generated-output smoke tests.")
	endif()

	add_executable(mockfakegen_config_cli_test tests/config/config_cli_test.cpp)
	target_link_libraries(mockfakegen_config_cli_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_config_cli_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_config_cli_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_config_cli_test)
	mockfakegen_apply_sanitizers(mockfakegen_config_cli_test)

	add_test(NAME mockfakegen_config_cli_test COMMAND mockfakegen_config_cli_test)

	add_executable(mockfakegen_clang_cmake_smoke tests/clang/clang_cmake_smoke.cpp)
	target_link_libraries(mockfakegen_clang_cmake_smoke PRIVATE mockfakegen_clang)
	target_compile_features(mockfakegen_clang_cmake_smoke PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_clang_cmake_smoke
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_clang_cmake_smoke)
	mockfakegen_apply_sanitizers(mockfakegen_clang_cmake_smoke)

	add_test(NAME mockfakegen_clang_cmake_smoke COMMAND mockfakegen_clang_cmake_smoke)

	add_executable(mockfakegen_synthetic_tu_parser_test tests/clang/synthetic_tu_parser_test.cpp)
	target_link_libraries(mockfakegen_synthetic_tu_parser_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_synthetic_tu_parser_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_synthetic_tu_parser_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_synthetic_tu_parser_test)
	mockfakegen_apply_sanitizers(mockfakegen_synthetic_tu_parser_test)

	add_test(NAME mockfakegen_synthetic_tu_parser_test COMMAND mockfakegen_synthetic_tu_parser_test)

	add_executable(mockfakegen_class_extractor_test tests/clang/class_extractor_test.cpp)
	target_link_libraries(mockfakegen_class_extractor_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_class_extractor_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_class_extractor_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_class_extractor_test)
	mockfakegen_apply_sanitizers(mockfakegen_class_extractor_test)

	add_test(NAME mockfakegen_class_extractor_test COMMAND mockfakegen_class_extractor_test)

	add_executable(mockfakegen_header_scanner_test tests/scanner/header_scanner_test.cpp)
	target_link_libraries(mockfakegen_header_scanner_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_header_scanner_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_header_scanner_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_header_scanner_test)
	mockfakegen_apply_sanitizers(mockfakegen_header_scanner_test)

	add_test(NAME mockfakegen_header_scanner_test COMMAND mockfakegen_header_scanner_test)

	add_executable(mockfakegen_generated_file_test tests/model/generated_file_test.cpp)
	target_link_libraries(mockfakegen_generated_file_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_generated_file_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_generated_file_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_generated_file_test)
	mockfakegen_apply_sanitizers(mockfakegen_generated_file_test)

	add_test(NAME mockfakegen_generated_file_test COMMAND mockfakegen_generated_file_test)

	add_executable(mockfakegen_project_model_test tests/model/project_model_test.cpp)
	target_link_libraries(mockfakegen_project_model_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_project_model_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_project_model_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_project_model_test)
	mockfakegen_apply_sanitizers(mockfakegen_project_model_test)

	add_test(NAME mockfakegen_project_model_test COMMAND mockfakegen_project_model_test)

	add_executable(mockfakegen_code_generator_test tests/generator/code_generator_test.cpp)
	target_link_libraries(mockfakegen_code_generator_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_code_generator_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_code_generator_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_code_generator_test)
	mockfakegen_apply_sanitizers(mockfakegen_code_generator_test)

	add_test(NAME mockfakegen_code_generator_test COMMAND mockfakegen_code_generator_test)

	add_executable(mockfakegen_output_writer_test tests/output/output_writer_test.cpp)
	target_link_libraries(mockfakegen_output_writer_test PRIVATE mockfakegen_core)
	target_compile_features(mockfakegen_output_writer_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_output_writer_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_output_writer_test)
	mockfakegen_apply_sanitizers(mockfakegen_output_writer_test)

	add_test(NAME mockfakegen_output_writer_test COMMAND mockfakegen_output_writer_test)

	add_executable(
		mockfakegen_hoge_fixture_generation_test
		tests/fixtures/hoge/hoge_fixture_generation_test.cpp
	)
	target_link_libraries(mockfakegen_hoge_fixture_generation_test PRIVATE mockfakegen_core)
	target_compile_definitions(
		mockfakegen_hoge_fixture_generation_test
		PRIVATE
			MOCKFAKEGEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)
	target_compile_features(mockfakegen_hoge_fixture_generation_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_hoge_fixture_generation_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_hoge_fixture_generation_test)
	mockfakegen_apply_sanitizers(mockfakegen_hoge_fixture_generation_test)

	add_test(
		NAME mockfakegen_hoge_fixture_generation_test
		COMMAND mockfakegen_hoge_fixture_generation_test
	)

	add_executable(
		mockfakegen_hoge_generated_smoke
		tests/fixtures/hoge/hoge_generated_smoke_test.cpp
		tests/fixtures/hoge/generated/FakeHoge.cpp
	)
	target_include_directories(
		mockfakegen_hoge_generated_smoke
		PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hoge/product"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hoge/generated"
	)
	target_link_libraries(mockfakegen_hoge_generated_smoke PRIVATE ${MOCKFAKEGEN_GMOCK_MAIN_TARGET})
	target_compile_features(mockfakegen_hoge_generated_smoke PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_hoge_generated_smoke
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_hoge_generated_smoke)
	mockfakegen_apply_sanitizers(mockfakegen_hoge_generated_smoke)

	add_test(NAME mockfakegen_hoge_generated_smoke COMMAND mockfakegen_hoge_generated_smoke)

	add_executable(
		mockfakegen_generated_output_check_test
		tests/validation/generated_output_check_test.cpp
	)
	target_link_libraries(mockfakegen_generated_output_check_test PRIVATE mockfakegen_core)
	target_compile_definitions(
		mockfakegen_generated_output_check_test
		PRIVATE
			MOCKFAKEGEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)
	target_compile_features(mockfakegen_generated_output_check_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_generated_output_check_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_generated_output_check_test)
	mockfakegen_apply_sanitizers(mockfakegen_generated_output_check_test)

	add_test(NAME mockfakegen_generated_output_check_test COMMAND mockfakegen_generated_output_check_test)
	add_custom_target(
		check-generated-output
		COMMAND mockfakegen_generated_output_check_test
		DEPENDS mockfakegen_generated_output_check_test
		COMMENT "Checking generated-output fixtures for forbidden ket tokens"
	)

	add_executable(mockfakegen_runtime_template_test tests/runtime/runtime_template_test.cpp)
	target_link_libraries(mockfakegen_runtime_template_test PRIVATE mockfakegen_core)
	target_compile_definitions(
		mockfakegen_runtime_template_test
		PRIVATE
			MOCKFAKEGEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
	)
	target_compile_features(mockfakegen_runtime_template_test PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_runtime_template_test
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_runtime_template_test)
	mockfakegen_apply_sanitizers(mockfakegen_runtime_template_test)

	add_test(NAME mockfakegen_runtime_template_test COMMAND mockfakegen_runtime_template_test)

	add_executable(mockfakegen_runtime_header_smoke tests/runtime/mock_fake_runtime_header_smoke.cpp)
	target_include_directories(
		mockfakegen_runtime_header_smoke
		PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/generated_runtime"
	)
	target_compile_features(mockfakegen_runtime_header_smoke PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_runtime_header_smoke
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_runtime_header_smoke)
	mockfakegen_apply_sanitizers(mockfakegen_runtime_header_smoke)

	add_test(NAME mockfakegen_runtime_header_smoke COMMAND mockfakegen_runtime_header_smoke)

	add_executable(mockfakegen_foundation_smoke tests/foundation/foundation_smoke.cpp)
	target_link_libraries(mockfakegen_foundation_smoke PRIVATE mockfakegen_ket)
	target_compile_features(mockfakegen_foundation_smoke PRIVATE cxx_std_23)
	set_target_properties(
		mockfakegen_foundation_smoke
		PROPERTIES
			CXX_EXTENSIONS OFF
	)
	mockfakegen_apply_strict_warnings(mockfakegen_foundation_smoke)
	mockfakegen_apply_sanitizers(mockfakegen_foundation_smoke)

	add_test(NAME mockfakegen_foundation_smoke COMMAND mockfakegen_foundation_smoke)
endif()
