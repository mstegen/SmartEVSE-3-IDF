# =============================================================================
# web_assets � drives packfs.py to (re)generate src/packed_fs.c whenever
# any file in ../data/ changes.
#
# In IDF v6, add_custom_command / add_custom_target are not allowed from
# inside a component CMakeLists.txt (they must live at project level).
# We use execute_process() to run packfs.py at *configure* time, and a
# small CONFIGURE_DEPENDS to retrigger the project reconfigure when a
# data file changes.
# =============================================================================

set(DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../data")
set(OUT_FILE "${CMAKE_CURRENT_LIST_DIR}/../../src/packed_fs.c")
set(PACKFS   "${CMAKE_CURRENT_LIST_DIR}/../../packfs.py")
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(NOT EXISTS "${DATA_DIR}")
    message(FATAL_ERROR "data/ directory not found: ${DATA_DIR}")
endif()

# In component-script mode, ${Python3_EXECUTABLE} is not set. Find Python
# the same way the IDF build does. Prefer the IDF_PYTHON_ENV_PATH (the
# venv) if set, otherwise fall back to the system python.
if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
    set(_PYTHON "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python.exe")
    if(NOT EXISTS "${_PYTHON}")
        set(_PYTHON "$ENV{IDF_PYTHON_ENV_PATH}/bin/python")
    endif()
else()
    set(_PYTHON "python")
endif()

# List input files (so the configure step aborts cleanly if data/ is empty).
file(GLOB DATA_FILES "${DATA_DIR}/*")

# packfs.py uses hardcoded relative paths ("data/...", "pack.tmp/...", "../pack.py",
# "../src/packed_fs.c"). The cleanest way to make it work is to invoke it from the
# project root with the relative `data` path. We ignore the ${OUT_FILE} argument
# the script nominally accepts; it always writes to ../src/packed_fs.c anyway.
execute_process(
    COMMAND "${_PYTHON}" "packfs.py" "data"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    RESULT_VARIABLE PACKFS_RESULT
)

if(NOT PACKFS_RESULT EQUAL 0)
    message(FATAL_ERROR
        "packfs.py failed (exit ${PACKFS_RESULT}). "
        "Make sure Python 3 is available and packfs.py is correct.")
endif()

# Component itself only provides INCLUDE_DIRS so the app component can
# find packed_fs.c (which is a generated .c file living in ../../src; the
# app component already lists that in SRC_DIRS).
idf_component_register(
    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../src"
    REQUIRES arduino_compat
)