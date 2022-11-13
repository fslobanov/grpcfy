set( target_id example-grpcfy-client )
add_executable( ${target_id} )

add_dependencies( ${target_id} example-foobar-protocol )
target_link_libraries( ${target_id} example-foobar-protocol grpcfy-client )
target_sources( ${target_id} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" )

target_compile_options( ${target_id} PUBLIC -Wall -Wextra -pedantic )