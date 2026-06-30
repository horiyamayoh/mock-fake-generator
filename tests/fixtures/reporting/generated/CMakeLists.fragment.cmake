# Link generated FakeXXX.cpp files instead of the corresponding product .cpp files.
# Do not link both implementations in the same target.

set(MOCKFAKE_GENERATED_SOURCES
	"${CMAKE_CURRENT_LIST_DIR}/FakeService.cpp"
)

set(MOCKFAKE_GENERATED_INCLUDE_DIR
	"${CMAKE_CURRENT_LIST_DIR}"
)
