set( target_id example-grpcfy-server )
add_executable( ${target_id} )

add_dependencies( ${target_id} example-foobar-protocol )
target_link_libraries( ${target_id} example-foobar-protocol grpcfy-server )
target_sources( ${target_id} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" )

target_compile_options( ${target_id} PUBLIC -Wall -Wextra -pedantic )
target_compile_definitions( ${target_id} PRIVATE
                            -DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1
                            DFMT_HEADER_ONLY
                            )