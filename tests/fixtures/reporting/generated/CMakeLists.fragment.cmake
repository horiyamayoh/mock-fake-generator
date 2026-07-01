# Link generated FakeXXX.cpp files instead of the corresponding product .cpp files.
# Do not link both implementations in the same target.
# Use mockfakegen --validate link when you want a generated-fake link smoke.

set(MOCKFAKE_GENERATED_SOURCES
)

set(MOCKFAKE_GENERATED_INCLUDE_DIR
	"${CMAKE_CURRENT_LIST_DIR}"
)
