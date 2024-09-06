# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(ENV{EXTRA_CFLAGS} "-Werror -Werror=deprecated-declarations -Werror=unused-variable \
    -Werror=unused-but-set-variable -Werror=unused-function -Wstrict-prototypes")

set(ENV{EXTRA_CXXFLAGS} "-Werror -Werror=deprecated-declarations -Werror=unused-variable \
    -Werror=unused-but-set-variable -Werror=unused-function")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_ot_br)
