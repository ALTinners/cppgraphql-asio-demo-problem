cmake_minimum_required(VERSION 3.15)

include(${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/cppgraphqlgen/cmake/cppgraphqlgen-functions.cmake)

update_graphql_schema_files(demo schema.graphqls demo demo)

add_graphql_schema_target(demo)