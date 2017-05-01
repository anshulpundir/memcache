cmake_minimum_required (VERSION 2.6)

file(GLOB TEST_SOURCES ./*.cpp)

foreach(testsourcefile ${TEST_SOURCES})
    get_filename_component(testname ${testsourcefile} NAME_WE)
    add_executable(${testname} ${testsourcefile})
    target_link_libraries(${testname} mclib pthread)
    add_test(NAME ${testname} COMMAND ${testname})
endforeach(testsourcefile ${TEST_SOURCES})