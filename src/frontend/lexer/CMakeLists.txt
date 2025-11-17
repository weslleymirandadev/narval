add_library(lexer lexer.cpp 
    identifier_tokenizer.cpp 
    operator_tokenizer.cpp 
    number_tokenizer.cpp
    string_tokenizer.cpp)

target_link_libraries(lexer PUBLIC ruphi_project_includes ruphi_all_common)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lexer.test.cpp")
    ruphi_add_test(lexer_test lexer.test.cpp ../module_manager.cpp)
    target_link_libraries(lexer_test PRIVATE lexer)
endif()