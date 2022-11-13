set( target_id grpcfy-core )
add_library( ${target_id} INTERFACE )

target_include_directories( ${target_id} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include" )

target_sources( ${target_id} INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/core/Grpc.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/core/Logger.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/core/PointerTagging.h"
                )