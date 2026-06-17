enable_testing()

add_executable(opendtp_tests
    test_protocol.cpp
    test_client.cpp
)

target_link_libraries(opendtp_tests
    PRIVATE
        opendtp
        gtest_main
)

target_include_directories(opendtp_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME opendtp_tests
    COMMAND opendtp_tests
)
