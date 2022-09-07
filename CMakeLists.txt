cmake_minimum_required (VERSION 3.16.3)

cmake_policy(SET CMP0048 NEW)

project(demo VERSION 1.0.0)

find_package(Boost
    REQUIRED
)

set(GRAPHQL_BUILD_TESTS OFF CACHE BOOL "Disable GraphQL test builds")
set(GRAPHQL_UPDATE_SAMPLES OFF CACHE BOOL "Disable GraphQL sample updates")
add_subdirectory(thirdparty/cppgraphqlgen EXCLUDE_FROM_ALL)

add_subdirectory(schema)

add_executable(demo
    src/main.cpp
)

target_link_libraries(demo
    graphqlservice
    graphqljson
    demo_schema
)