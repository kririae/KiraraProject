# Each package is an archive of one install component. A project declares its packages with
# krr_add_package, and the root configures CPack for all of them with krr_configure_packages.

# Declares a package of the install component <component>, archived as <file-name>.zip on Windows
# and <file-name>.tar.gz elsewhere.
#
# krr_add_package(<component> FILE_NAME <file-name>)
function(krr_add_package component)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "FILE_NAME" "")
    if(NOT arg_FILE_NAME)
        message(FATAL_ERROR "krr_add_package(${component}) needs FILE_NAME")
    endif()

    set_property(GLOBAL APPEND PROPERTY KRR_PACKAGES ${component})
    string(TOUPPER "${component}" componentUpper)
    set_property(GLOBAL PROPERTY KRR_PACKAGE_FILE_NAME_${componentUpper} "${arg_FILE_NAME}")
endfunction()

# Configures CPack for the declared packages. It runs once, after every project is added.
macro(krr_configure_packages)
    get_property(_krr_packages GLOBAL PROPERTY KRR_PACKAGES)
    if(_krr_packages)
        set(CPACK_PACKAGE_VERSION "${KRR_VERSION}")
        set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
        set(CPACK_COMPONENTS_ALL ${_krr_packages})
        if(WIN32)
            set(CPACK_GENERATOR ZIP)
        else()
            set(CPACK_GENERATOR TGZ)
        endif()

        foreach(_krr_package IN LISTS _krr_packages)
            string(TOUPPER "${_krr_package}" _krr_package_upper)
            get_property(CPACK_ARCHIVE_${_krr_package_upper}_FILE_NAME GLOBAL
                         PROPERTY KRR_PACKAGE_FILE_NAME_${_krr_package_upper})
        endforeach()

        include(CPack)
    endif()
endmacro()
