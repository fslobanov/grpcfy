set( target_id example-foobar-protocol )
add_library( ${target_id} STATIC )

set( hw_proto "${CMAKE_CURRENT_SOURCE_DIR}/FooBar.proto" )
set( hw_proto_path "${CMAKE_CURRENT_SOURCE_DIR}" )

set( hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/FooBar.pb.h" )
set( hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/FooBar.pb.cc" )

set( hw_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/FooBar.grpc.pb.h" )
set( hw_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/FooBar.grpc.pb.cc" )

find_program( protoc_executable protoc )
find_program( protoc_grpc_plugin grpc_cpp_plugin )

message( STATUS "example-foobar-protocol: Found protoc executable: ${protoc_executable}" )
message( STATUS "example-foobar-protocol: Found protoc gRPC plugin: ${protoc_grpc_plugin}" )

set( CMAKE_VERBOSE_MAKEFILE ON )
add_custom_command(
		OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
		COMMAND ${protoc_executable}
		ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
		--cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
		-I "${hw_proto_path}"
		--plugin=protoc-gen-grpc="${protoc_grpc_plugin}"
		"${hw_proto}"
		DEPENDS "${hw_proto}" )

target_include_directories( ${target_id} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" )

target_link_libraries( ${target_id} PUBLIC
                       grpcfy-core

                       protobuf::libprotobuf
                       gRPC::grpc++
                       gRPC::grpc++_reflection

                       fmt::fmt-header-only
                       )

target_compile_definitions( ${target_id} PUBLIC
                            -DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1
                            FMT_HEADER_ONLY
                            )

target_sources( ${target_id}
                PRIVATE ${hw_proto_hdrs} ${hw_grpc_hdrs}
                PRIVATE ${hw_proto_srcs} ${hw_grpc_srcs}
                )