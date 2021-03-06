/*
 * Copyright 2018 The Cartographer Authors
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

#include "cartographer_grpc/internal/handlers/get_constraints_handler.h"

#include "cartographer/common/make_unique.h"
#include "cartographer/mapping/pose_graph.h"
#include "cartographer_grpc/internal/framework/rpc_handler.h"
#include "cartographer_grpc/internal/map_builder_context_interface.h"
#include "cartographer_grpc/proto/map_builder_service.pb.h"
#include "google/protobuf/empty.pb.h"

namespace cartographer {
namespace cloud {
namespace handlers {

void GetConstraintsHandler::OnRequest(const google::protobuf::Empty& request) {
  auto constraints = GetContext<MapBuilderContextInterface>()
                         ->map_builder()
                         .pose_graph()
                         ->constraints();
  auto response = common::make_unique<proto::GetConstraintsResponse>();
  response->mutable_constraints()->Reserve(constraints.size());
  for (const auto& constraint : constraints) {
    *response->add_constraints() = mapping::ToProto(constraint);
  }
  Send(std::move(response));
}

}  // namespace handlers
}  // namespace cloud
}  // namespace cartographer
