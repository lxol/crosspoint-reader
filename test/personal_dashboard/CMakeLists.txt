add_executable(personal_dashboard_tests test.cpp)
target_include_directories(personal_dashboard_tests PRIVATE ${REPO_ROOT}/lib)
add_test(NAME personal_dashboard COMMAND personal_dashboard_tests)
