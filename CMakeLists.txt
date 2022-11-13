cmake_minimum_required( VERSION 3.20 )

project( grpcfy VERSION 0.1
         DESCRIPTION "User-friendly async gRPC boilerplate code"
         HOMEPAGE_URL "https://github.com/fslobanov/grpcfy"
         LANGUAGES "CXX" )

set( CMAKE_CXX_STANDARD 17 )
set( CXX_STANDARD_REQUIRED ON )

option( grpcfy_BUILD_EXAMPLES "Build examples" ON )
option( grpcfy_USE_INTERNAL_FMT "Use libfmt library submodule" ON )

set( grpcfy_AVAILABLE_SANITIZERS none address thread leak undefined )
set( grpcfy_SANITIZER none CACHE STRING "Current grpcfy sanitizer" )
set_property( CACHE grpcfy_SANITIZER PROPERTY STRINGS ${grpcfy_AVAILABLE_SANITIZERS} )
if( NOT grpcfy_SANITIZER IN_LIST grpcfy_AVAILABLE_SANITIZERS )
	message( FATAL_ERROR "grpcfy_SANITIZER must be one of ${grpcfy_AVAILABLE_SANITIZERS}" )
endif()

if( none STREQUAL grpcfy_SANITIZER )
	message( STATUS "grpcfy: no sanitizer will be used, you can use one of {${grpcfy_AVAILABLE_SANITIZERS}}" )
else()
	message( STATUS "grpcfy: '${grpcfy_SANITIZER}' sanitizer will be used" )
	set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=${grpcfy_SANITIZER}" )
	set( CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=${grpcfy_SANITIZER}" )
endif()

find_package( Threads REQUIRED )
find_package( Protobuf REQUIRED )
find_package( gRPC REQUIRED )
find_package( Boost REQUIRED )

message( STATUS "grpcfy: Boost found - ${Boost_VERSION}, ${Boost_INCLUDE_DIRS}" )
message( STATUS "grpcfy: Protobuf found - ${Protobuf_VERSION}, ${Protobuf_INCLUDE_DIRS}, ${Protobuf_LIBRARIES}" )
message( STATUS "grpcfy: gRPC found - ${gRPC_VERSION}" )

if( grpcfy_USE_INTERNAL_FMT )
	add_subdirectory( "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/fmt" )
endif()

add_subdirectory( src )