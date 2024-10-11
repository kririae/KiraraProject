include(KRR_Message)

function(krr_enable_clang_tidy target_name)
    find_program(
        KRR_CXX_CLANG_TIDY
        NAMES "clang-tidy"
        DOC "Path to clang-tidy executable")

    if(NOT KRR_CXX_CLANG_TIDY)
        krr_message(ERROR "clang-tidy not found, consider setting `KRR_ENABLE_CLANG_TIDY=OFF`.")
        return()
    endif()

    get_target_property(aliased_target ${target_name} ALIASED_TARGET)

    if(aliased_target)
        set(actual_target ${aliased_target})
        krr_message(INFO "Enable clang-tidy for ${target_name}")
    else()
        set(actual_target ${target_name})
        krr_message(INFO "Enable clang-tidy for ${actual_target}")
    endif()

    set_target_properties(${actual_target} PROPERTIES CXX_CLANG_TIDY ${KRR_CXX_CLANG_TIDY})
endfunction()
