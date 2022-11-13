add_subdirectory( core )
add_subdirectory( server )
add_subdirectory( client )

if( grpcfy_BUILD_EXAMPLES )
	add_subdirectory( example )
endif()