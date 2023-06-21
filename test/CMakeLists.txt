set(TEST_FILES
    catch_main.cpp
    test_disruptor.cpp
)

add_executable(disruptor ${TEST_FILES})
target_include_directories(disruptor PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(disruptor PRIVATE pthread)
add_test(NAME test_disruptor COMMAND disruptor)
