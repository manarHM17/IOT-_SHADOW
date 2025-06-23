cmake_minimum_required(VERSION 3.17)
project(iotshadow_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(protobuf_MODULE_COMPATIBLE TRUE)
set(Protobuf_PROTOC_EXECUTABLE "/usr/bin/protoc")
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${protobuf_VERSION}")
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# Find abseil (required by gRPC)
find_package(absl CONFIG REQUIRED)
message(STATUS "Using abseil ${absl_VERSION}")

# Find gRPC
find_package(gRPC CONFIG REQUIRED)
set(gRPC_CPP_PLUGIN_EXECUTABLE "/usr/local/bin/grpc_cpp_plugin")
message(STATUS "Using gRPC ${gRPC_VERSION}")
set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# MySQL
find_library(MYSQL_LIB mysqlclient REQUIRED)
include_directories(/usr/include/mysql)

# JWT
include_directories(/usr/local/include/jwt-cpp)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Boost
find_package(Boost REQUIRED)

# json-cpp
find_package(jsoncpp REQUIRED)
include_directories(/usr/include/jsoncpp)

# RabbitMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(RABBITMQ REQUIRED librabbitmq)
find_library(SIMPLE_AMQP_CLIENT SimpleAmqpClient REQUIRED)
link_directories(/usr/local/lib)
set(RABBITMQ_LIB ${SIMPLE_AMQP_CLIENT})

# Include directories
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${RABBITMQ_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/monitoring-service/include
    ${CMAKE_CURRENT_SOURCE_DIR}/provision-service/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ota-service/include
    ${CMAKE_CURRENT_BINARY_DIR}/provision-service
    ${CMAKE_CURRENT_BINARY_DIR}/monitoring-service
    ${CMAKE_CURRENT_BINARY_DIR}/ota-service
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

# Proto paths
set(PROTO_PATH "${CMAKE_SOURCE_DIR}/../proto")
set(GENERATED_PROTO_PATH_PROVISION "${CMAKE_CURRENT_BINARY_DIR}/provision-service")
set(GENERATED_PROTO_PATH_MONITORING "${CMAKE_CURRENT_BINARY_DIR}/monitoring-service")
set(GENERATED_PROTO_PATH_OTA "${CMAKE_CURRENT_BINARY_DIR}/ota-service")
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_PROVISION})
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_MONITORING})
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_OTA})

# === Proto compilation for all services ===
# Provisioning Service proto
set(PROVISION_PROTO "${PROTO_PATH}/provisioning.proto")
add_custom_command(
    OUTPUT ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.cc
           ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.h
           ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.cc
           ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.h
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out ${GENERATED_PROTO_PATH_PROVISION}
         --cpp_out ${GENERATED_PROTO_PATH_PROVISION}
         -I ${PROTO_PATH}
         --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
         ${PROVISION_PROTO}
    DEPENDS ${PROVISION_PROTO}
)

# Monitoring Service proto
set(MONITORING_PROTO "${PROTO_PATH}/monitoring.proto")
add_custom_command(
    OUTPUT ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.cc
           ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.h
           ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.cc
           ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.h
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out ${GENERATED_PROTO_PATH_MONITORING}
         --cpp_out ${GENERATED_PROTO_PATH_MONITORING}
         -I ${PROTO_PATH}
         --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
         ${MONITORING_PROTO}
    DEPENDS ${MONITORING_PROTO}
)

# OTA Service proto
set(OTA_PROTO "${PROTO_PATH}/ota_service.proto")
add_custom_command(
    OUTPUT ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.cc
           ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.h
           ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.cc
           ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.h
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --grpc_out ${GENERATED_PROTO_PATH_OTA}
         --cpp_out ${GENERATED_PROTO_PATH_OTA}
         -I ${PROTO_PATH}
         --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
         ${OTA_PROTO}
    DEPENDS ${OTA_PROTO}
)

# Custom target for proto generation
add_custom_target(generate_protos
    DEPENDS
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.cc
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.cc
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.cc
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.cc
        ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.cc
        ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.cc
)

# Gather all source files from each service
file(GLOB PROVISION_SRCS
    "provision-service/src/*.cpp"
)
file(GLOB MONITORING_SRCS
    "monitoring-service/src/*.cpp"
)
file(GLOB OTA_SRCS
    "ota-service/src/*.cpp"
)

# Add the unified db_handler.cpp to the server sources
file(GLOB COMMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/common/src/*.cpp)

# Add all generated sources and your main server sources
add_executable(iotshadow_server
    main.cpp
    ${PROVISION_SRCS}
    ${MONITORING_SRCS}
    ${OTA_SRCS}
    ${COMMON_SRC}
    ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.cc
    ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.cc
    ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.cc
    ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.cc
    ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.cc
    ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.cc
)

add_dependencies(iotshadow_server generate_protos)

target_link_libraries(iotshadow_server
    ${_GRPC_GRPCPP}
    protobuf::libprotobuf
    ${MYSQL_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    jsoncpp_lib
    ${RABBITMQ_LIB}
    ${RABBITMQ_LIBRARIES}
    pthread
    SimpleAmqpClient
)
