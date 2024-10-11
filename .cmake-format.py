with section("format"):
    # How wide to allow formatted cmake files
    line_width = 120

    # How many spaces to tab for indent
    tab_size = 4

    # If a statement is wrapped to more than one line, than dangle the closing parenthesis on its own line.
    dangle_parens = False

    # If an argument group contains more than this many sub-groups (parg or kwarg groups) then force it to a vertical layout.
    max_subgroups_hwrap = 2

    # If a positional argument group contains more than this many arguments, then force it to a vertical layout.
    max_pargs_hwrap = 4

    # If a candidate layout is wrapped horizontally but it exceeds this many lines, then reject the layout.
    max_lines_hwrap = 2

    # Format keywords consistently as 'lower' or 'upper' case
    command_case = "unchanged"

    # A list of command names which should always be wrapped
    always_wrap = ["SOURCES", "HEADERS", "OPTIONS"]

    # If true, the argument lists which are known to be sortable will be sorted lexicographicall
    enable_sort = True

with section("parse"):
    additional_commands = {
        "krr_add_module": {
            "kwargs": {
                "HEADERS": "+",
                "SOURCES": "+",
                "HARD_DEPENDENCIES": "+",
                "CMAKE_SUBDIRS": "1",
            }
        },
        "krr_add_test": {
            "kwargs": {
                "SOURCES": "+",
                "HARD_DEPENDENCIES": "+",
            }
        },
        "krr_message": {"pargs": {"nargs": 1}},
        "cpmaddpackage": {
            "spelling": "CPMAddPackage",
            "kwargs": {
                "NAME": "1",
                "FORCE": "1",
                "VERSION": "1",
                "GIT_TAG": "1",
                "DOWNLOAD_ONLY": "1",
                "GITHUB_REPOSITORY": "1",
                "GITLAB_REPOSITORY": "1",
                "GIT_REPOSITORY": "1",
                "SVN_REPOSITORY": "1",
                "SVN_REVISION": "1",
                "SOURCE_DIR": "1",
                "DOWNLOAD_COMMAND": "1",
                "FIND_PACKAGE_ARGUMENTS": "1",
                "NO_CACHE": "1",
                "GIT_SHALLOW": "1",
                "URL": "1",
                "URL_HASH": "1",
                "URL_MD5": "1",
                "DOWNLOAD_NAME": "1",
                "DOWNLOAD_NO_EXTRACT": "1",
                "HTTP_USERNAME": "1",
                "HTTP_PASSWORD": "1",
                "EXCLUDE_FROM_ALL": "1",
                "SYSTEM": "1",
                "SOURCE_SUBDIR": "1",
                "PATCHES": "+",
                "OPTIONS": "+",
            },
            "pargs": {"nargs": "*"},
        },
    }

with section("encode"):
    emit_byteorder_mark = False
    input_encoding = "utf-8"
    output_encoding = "utf-8"
