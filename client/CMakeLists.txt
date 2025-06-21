cmake_minimum_required(VERSION 3.15)
project(iotshadow_client)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protobuf
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${protobuf_VERSION}")
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# Find abseil (required by gRPC)
find_package(absl CONFIG REQUIRED)
message(STATUS "Using abseil ${absl_VERSION}")

# Find gRPC
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")
set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# JWT
include_directories(/usr/local/include/jwt-cpp)

# OpenSSL (since jwt depends on it)
find_package(OpenSSL REQUIRED)

# Boost
find_package(Boost REQUIRED)

# json-cpp
find_package(jsoncpp REQUIRED)
include_directories(/usr/include/jsoncpp)

# RabbitMQ (for monitoring-service client)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RABBITMQ REQUIRED librabbitmq)
find_library(SIMPLE_AMQP_CLIENT SimpleAmqpClient REQUIRED)
link_directories(/usr/local/lib)
set(RABBITMQ_LIB ${SIMPLE_AMQP_CLIENT})

# Unified proto path
set(PROTO_PATH "${CMAKE_SOURCE_DIR}/../proto")
set(GENERATED_PROTO_PATH_PROVISION "${CMAKE_CURRENT_BINARY_DIR}/provision-service")
set(GENERATED_PROTO_PATH_MONITORING "${CMAKE_CURRENT_BINARY_DIR}/monitoring-service")
set(GENERATED_PROTO_PATH_OTA "${CMAKE_CURRENT_BINARY_DIR}/ota-service")
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_PROVISION})
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_MONITORING})
file(MAKE_DIRECTORY ${GENERATED_PROTO_PATH_OTA})

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


# Create a target for proto generation
add_custom_target(generate_protos
    DEPENDS
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.cc
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.h
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.cc
        ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.h
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.cc
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.h
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.cc
        ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.h
        ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.cc
        ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.h
        ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.cc
        ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.h
)


# List specific source files for each service to avoid multiple mains and duplicate implementations
set(CLIENT_SOURCES
    main.cpp
    # Provisioning service files
    provision-service/src/ProvisionClientImpl.cpp
    provision-service/src/ConfigManager.cpp
    # Monitoring service files
    monitoring-service/src/metrics_collector.cpp
    monitoring-service/src/rabbitmq_sender.cpp
   # Proto generated files
    ${GENERATED_PROTO_PATH_PROVISION}/provisioning.pb.cc
    ${GENERATED_PROTO_PATH_PROVISION}/provisioning.grpc.pb.cc
    ${GENERATED_PROTO_PATH_MONITORING}/monitoring.pb.cc
    ${GENERATED_PROTO_PATH_MONITORING}/monitoring.grpc.pb.cc
    ${GENERATED_PROTO_PATH_OTA}/ota_service.pb.cc
    ${GENERATED_PROTO_PATH_OTA}/ota_service.grpc.pb.cc
)


# Create the main client executable
add_executable(iotshadow_client ${CLIENT_SOURCES})

# Add include directories specifically for the main target
target_include_directories(iotshadow_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/provision-service/include
    ${CMAKE_CURRENT_SOURCE_DIR}/monitoring-service/include
    ${GENERATED_PROTO_PATH_PROVISION}
    ${GENERATED_PROTO_PATH_MONITORING}
    ${GENERATED_PROTO_PATH_OTA}
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated files
    ${Boost_INCLUDE_DIRS}
    ${RABBITMQ_INCLUDE_DIRS}
)

# Link the executable with required libraries
target_link_libraries(iotshadow_client
    PRIVATE
    ${_GRPC_GRPCPP}
    protobuf::libprotobuf
    gRPC::grpc++_reflection
    gRPC::grpc++
    gRPC::grpc
    protobuf::libprotobuf
    absl::base
    absl::synchronization
    absl::time
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    jsoncpp_lib
    ${RABBITMQ_LIBRARIES}
    ${RABBITMQ_LDFLAGS}
    pthread
)

add_dependencies(iotshadow_client generate_protos)
