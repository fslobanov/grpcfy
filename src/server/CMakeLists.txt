set( target_id grpcfy-server )
add_library( ${target_id} INTERFACE )

target_include_directories( ${target_id} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_link_libraries( ${target_id} INTERFACE grpcfy-core )

target_sources( ${target_id} INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/ServiceEngine.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/Configuration.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/SingularMethod.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/ServerStreamMethod.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/detail/MethodContext.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/detail/SingularMethodContext.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/detail/ServerStreamMethodContext.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/detail/SingularMethodMetadata.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/server/detail/ServerStreamMethodMetadata.h"
                )