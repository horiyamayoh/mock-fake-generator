cmake_minimum_required(VERSION 3.25)

project(mockfakegen VERSION 0.1.0 LANGUAGES CXX)

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
		src/HeaderScanner.cpp
		src/support/CliSupport.cpp
)
target_include_directories(mockfakegen_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mockfakegen_core PRIVATE mockfakegen_ket)
target_compile_features(mockfakegen_core PUBLIC cxx_std_23)
set_target_properties(
	mockfakegen_core
	PROPERTIES
		CXX_EXTENSIONS OFF
)
mockfakegen_apply_strict_warnings(mockfakegen_core)
mockfakegen_apply_sanitizers(mockfakegen_core)

add_executable(mockfakegen src/main.cpp)
target_link_libraries(mockfakegen PRIVATE mockfakegen_core)
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
