include(CMakeParseArguments)

function(narval_collect_sources out_var)
    set(options)
    set(one_value_args)
    set(multi_value_args PATTERNS EXCLUDE_REGEXES)
    cmake_parse_arguments(NCS "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT NCS_PATTERNS)
        set(NCS_PATTERNS "*.cpp")
    endif()

    set(collected_sources)
    foreach(pattern IN LISTS NCS_PATTERNS)
        file(GLOB_RECURSE pattern_sources
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${pattern}"
        )
        list(APPEND collected_sources ${pattern_sources})
    endforeach()

    foreach(exclude_regex IN LISTS NCS_EXCLUDE_REGEXES)
        list(FILTER collected_sources EXCLUDE REGEX "${exclude_regex}")
    endforeach()

    list(REMOVE_DUPLICATES collected_sources)
    set(${out_var} ${collected_sources} PARENT_SCOPE)
endfunction()
