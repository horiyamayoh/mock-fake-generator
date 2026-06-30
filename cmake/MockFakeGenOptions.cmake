function(mockfakegen_apply_sanitizers target)
	if(NOT MOCKFAKEGEN_ENABLE_SANITIZERS)
		return()
	endif()

	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		message(FATAL_ERROR "MOCKFAKEGEN_ENABLE_SANITIZERS is supported only with GNU or Clang.")
	endif()

	set(sanitizer_options
		-fsanitize=address,undefined
		-fno-omit-frame-pointer
	)

	target_compile_options(${target} PRIVATE ${sanitizer_options})

	get_target_property(target_type ${target} TYPE)
	if(NOT target_type STREQUAL "OBJECT_LIBRARY")
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endif()
endfunction()

function(mockfakegen_apply_strict_warnings target)
	if(NOT MOCKFAKEGEN_ENABLE_STRICT_WARNINGS)
		return()
	endif()

	set(common_warning_options
		-Wall
		-Wextra
		-Wpedantic
		-Wconversion
		-Wsign-conversion
		-Wshadow
		-Wold-style-cast
		-Wdouble-promotion
		-Wformat=2
		-Wundef
		-Wnon-virtual-dtor
		-Woverloaded-virtual
		-Wnull-dereference
	)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(
			${target}
			PRIVATE
				${common_warning_options}
				-Wduplicated-cond
				-Wduplicated-branches
				-Wlogical-op
				-Wuseless-cast
		)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		target_compile_options(
			${target}
			PRIVATE
				${common_warning_options}
				-Wextra-semi
				-Wimplicit-fallthrough
		)
	elseif(MSVC)
		target_compile_options(${target} PRIVATE /W4)
	endif()

	if(MOCKFAKEGEN_WARNINGS_AS_ERRORS)
		if(MSVC)
			target_compile_options(${target} PRIVATE /WX)
		else()
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()
