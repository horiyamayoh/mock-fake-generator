# Link generated FakeXXX.cpp files instead of the corresponding product .cpp files.
# Do not link both implementations in the same target.

set(MOCKFAKE_GENERATED_SOURCES
	"${CMAKE_CURRENT_LIST_DIR}/Fake_a_Hoge.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/Fake_b_Hoge.cpp"
)

set(MOCKFAKE_GENERATED_INCLUDE_DIR
	"${CMAKE_CURRENT_LIST_DIR}"
)
