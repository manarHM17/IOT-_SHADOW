file(REMOVE_RECURSE
  "CMakeFiles/generate_protos"
  "monitoring-service/monitoring.grpc.pb.cc"
  "monitoring-service/monitoring.grpc.pb.h"
  "monitoring-service/monitoring.pb.cc"
  "monitoring-service/monitoring.pb.h"
  "ota-service/ota_service.grpc.pb.cc"
  "ota-service/ota_service.grpc.pb.h"
  "ota-service/ota_service.pb.cc"
  "ota-service/ota_service.pb.h"
  "provision-service/provisioning.grpc.pb.cc"
  "provision-service/provisioning.grpc.pb.h"
  "provision-service/provisioning.pb.cc"
  "provision-service/provisioning.pb.h"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/generate_protos.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
