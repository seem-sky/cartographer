/*
 * Copyright 2017 The Cartographer Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cartographer_grpc/client/map_builder_stub.h"

#include "cartographer_grpc/internal/client/pose_graph_stub.h"
#include "cartographer_grpc/internal/client/trajectory_builder_stub.h"
#include "cartographer_grpc/internal/handlers/add_trajectory_handler.h"
#include "cartographer_grpc/internal/handlers/finish_trajectory_handler.h"
#include "cartographer_grpc/internal/handlers/get_submap_handler.h"
#include "cartographer_grpc/internal/handlers/load_state_handler.h"
#include "cartographer_grpc/internal/handlers/write_state_handler.h"
#include "cartographer_grpc/internal/sensor/serialization.h"
#include "cartographer_grpc/proto/map_builder_service.pb.h"
#include "glog/logging.h"

namespace cartographer {
namespace cloud {
namespace {

using common::make_unique;

}  // namespace

MapBuilderStub::MapBuilderStub(const std::string& server_address)
    : client_channel_(::grpc::CreateChannel(
          server_address, ::grpc::InsecureChannelCredentials())),
      pose_graph_stub_(make_unique<PoseGraphStub>(client_channel_)) {}

int MapBuilderStub::AddTrajectoryBuilder(
    const std::set<SensorId>& expected_sensor_ids,
    const mapping::proto::TrajectoryBuilderOptions& trajectory_options,
    LocalSlamResultCallback local_slam_result_callback) {
  proto::AddTrajectoryRequest request;
  *request.mutable_trajectory_builder_options() = trajectory_options;
  for (const auto& sensor_id : expected_sensor_ids) {
    *request.add_expected_sensor_ids() = cloud::ToProto(sensor_id);
  }
  framework::Client<handlers::AddTrajectoryHandler> client(
      client_channel_, framework::CreateLimitedBackoffStrategy(
                           common::FromMilliseconds(100), 2.f, 5));
  CHECK(client.Write(request));

  // Construct trajectory builder stub.
  trajectory_builder_stubs_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(client.response().trajectory_id()),
      std::forward_as_tuple(make_unique<TrajectoryBuilderStub>(
          client_channel_, client.response().trajectory_id(),
          local_slam_result_callback)));
  return client.response().trajectory_id();
}

int MapBuilderStub::AddTrajectoryForDeserialization(
    const mapping::proto::TrajectoryBuilderOptionsWithSensorIds&
        options_with_sensor_ids_proto) {
  LOG(FATAL) << "Not implemented";
}

mapping::TrajectoryBuilderInterface* MapBuilderStub::GetTrajectoryBuilder(
    int trajectory_id) const {
  return trajectory_builder_stubs_.at(trajectory_id).get();
}

void MapBuilderStub::FinishTrajectory(int trajectory_id) {
  proto::FinishTrajectoryRequest request;
  request.set_trajectory_id(trajectory_id);
  framework::Client<handlers::FinishTrajectoryHandler> client(client_channel_);
  CHECK(client.Write(request));
  trajectory_builder_stubs_.erase(trajectory_id);
}

std::string MapBuilderStub::SubmapToProto(
    const mapping::SubmapId& submap_id,
    mapping::proto::SubmapQuery::Response* submap_query_response) {
  proto::GetSubmapRequest request;
  submap_id.ToProto(request.mutable_submap_id());
  framework::Client<handlers::GetSubmapHandler> client(client_channel_);
  CHECK(client.Write(request));
  submap_query_response->CopyFrom(client.response().submap_query_response());
  return client.response().error_msg();
}

void MapBuilderStub::SerializeState(io::ProtoStreamWriterInterface* writer) {
  google::protobuf::Empty request;
  framework::Client<handlers::WriteStateHandler> client(client_channel_);
  CHECK(client.Write(request));
  proto::WriteStateResponse response;
  while (client.Read(&response)) {
    // writer->WriteProto(response);
    switch (response.state_chunk_case()) {
      case proto::WriteStateResponse::kPoseGraph:
        writer->WriteProto(response.pose_graph());
        break;
      case proto::WriteStateResponse::kAllTrajectoryBuilderOptions:
        writer->WriteProto(response.all_trajectory_builder_options());
        break;
      case proto::WriteStateResponse::kSerializedData:
        writer->WriteProto(response.serialized_data());
        break;
      default:
        LOG(FATAL) << "Unhandled message type";
    }
  }
  CHECK(writer->Close());
}

void MapBuilderStub::LoadState(io::ProtoStreamReaderInterface* reader,
                               const bool load_frozen_state) {
  if (!load_frozen_state) {
    LOG(FATAL) << "Not implemented";
  }
  framework::Client<handlers::LoadStateHandler> client(client_channel_);
  // Request with a PoseGraph proto is sent first.
  {
    proto::LoadStateRequest request;
    CHECK(reader->ReadProto(request.mutable_pose_graph()));
    CHECK(client.Write(request));
  }
  // Request with an AllTrajectoryBuilderOptions should be second.
  {
    proto::LoadStateRequest request;
    CHECK(reader->ReadProto(request.mutable_all_trajectory_builder_options()));
    CHECK(client.Write(request));
  }
  // Multiple requests with SerializedData are sent after.
  proto::LoadStateRequest request;
  while (reader->ReadProto(request.mutable_serialized_data())) {
    CHECK(client.Write(request));
  }

  CHECK(reader->eof());
  CHECK(client.WritesDone());
  CHECK(client.Finish().ok());
}

int MapBuilderStub::num_trajectory_builders() const {
  return trajectory_builder_stubs_.size();
}

mapping::PoseGraphInterface* MapBuilderStub::pose_graph() {
  return pose_graph_stub_.get();
}

const std::vector<mapping::proto::TrajectoryBuilderOptionsWithSensorIds>&
MapBuilderStub::GetAllTrajectoryBuilderOptions() const {
  LOG(FATAL) << "Not implemented";
}

}  // namespace cloud
}  // namespace cartographer
