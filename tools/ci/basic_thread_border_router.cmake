# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(ENV{EXTRA_CFLAGS} "-Werror -Werror=deprecated-declarations -Werror=unused-variable \
    -Wno-error=unused-but-set-variable -Werror=unused-function -Wstrict-prototypes")

set(ENV{EXTRA_CXXFLAGS} "-Werror -Werror=deprecated-declarations -Werror=unused-variable \
    -Wno-error=unused-but-set-variable -Werror=unused-function")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_ot_br)

# Keep strict unused-but-set-variable checks for local esp-thread-br components
# while not failing on external dependencies from ESP-IDF.
if(DEFINED ENV{ESP_THREAD_BR_PATH})
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(component ${build_components})
        idf_component_get_property(component_source ${component} COMPONENT_SOURCE)
        idf_component_get_property(component_lib ${component} COMPONENT_LIB)
        if((component_source STREQUAL "project_components" OR component_source STREQUAL "project_extra_components")
            AND TARGET ${component_lib})
            get_target_property(component_lib_type ${component_lib} TYPE)
            if(NOT component_lib_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${component_lib} PRIVATE -Werror=unused-but-set-variable)
            endif()
        endif()
    endforeach()
endif()
