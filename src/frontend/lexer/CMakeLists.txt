add_library(NarvalLexer lexer.cpp
    identifier_tokenizer.cpp
    operator_tokenizer.cpp
    number_tokenizer.cpp
    string_tokenizer.cpp)

target_link_libraries(NarvalLexer PUBLIC NarvalProjectIncludes NarvalFrontendUtils)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lexer.test.cpp")
    narval_add_test(NarvalLexerTest lexer.test.cpp ../module_manager.cpp)
    target_link_libraries(NarvalLexerTest PRIVATE NarvalLexer)
endif()
