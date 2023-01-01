set( target_id grpcfy-client )
add_library( ${target_id} INTERFACE )

target_include_directories( ${target_id} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_link_libraries( ${target_id} INTERFACE grpcfy-core )

target_sources( ${target_id} INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/Common.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/ClientEngine.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/Options.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/SingularCall.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/ServerStreamCall.h"

                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/detail/CallContext.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/detail/ServerStreamEntry.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/detail/ServerStreamCallContext.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/grpcfy/client/detail/SingularCallContext.h"
                )