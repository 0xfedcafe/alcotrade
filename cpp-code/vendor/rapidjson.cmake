# ---------------------------------------------------------------------------
# MODERNDBS
# ---------------------------------------------------------------------------

include(ExternalProject)
find_package(Git REQUIRED)

# Get rapidjson
ExternalProject_Add(
    rapidjson_src
    PREFIX "vendor/rapidjson"
    GIT_REPOSITORY "https://github.com/tencent/rapidjson"
    GIT_TAG master
    TIMEOUT 10
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    UPDATE_COMMAND ""
)

# Prepare json
ExternalProject_Get_Property(rapidjson_src source_dir)
set(RAPIDJSON_INCLUDE_DIR ${source_dir}/include)
file(MAKE_DIRECTORY ${RAPIDJSON_INCLUDE_DIR})
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE ${RAPIDJSON_INCLUDE_DIR})

# Dependencies
add_dependencies(rapidjson rapidjson_src)
