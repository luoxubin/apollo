/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/**
 * @file
 **/

#include "modules/planning/tasks/poly_st_speed/poly_st_graph.h"

#include <algorithm>
#include <unordered_map>

#include "modules/common/proto/error_code.pb.h"
#include "modules/common/proto/pnc_point.pb.h"
#include "modules/planning/proto/planning_internal.pb.h"

#include "modules/common/configs/vehicle_config_helper.h"
#include "modules/common/log.h"
#include "modules/common/util/util.h"
#include "modules/planning/common/planning_gflags.h"
#include "modules/planning/math/curve1d/quintic_polynomial_curve1d.h"
#include "modules/planning/tasks/poly_st_speed/speed_profile_cost.h"

namespace apollo {
namespace planning {

using apollo::common::ErrorCode;
using apollo::common::Status;

PolyStGraph::PolyStGraph(const PolyStSpeedConfig &config,
                         const ReferenceLineInfo *reference_line_info,
                         const SpeedLimit &speed_limit)
    : config_(config),
      reference_line_info_(reference_line_info),
      reference_line_(reference_line_info->reference_line()),
      speed_limit_(speed_limit) {}

bool PolyStGraph::FindStTunnel(
    const common::TrajectoryPoint &init_point,
    const std::vector<const PathObstacle *> &obstacles,
    SpeedData *const speed_data) {
  CHECK_NOTNULL(speed_data);

  init_point_ = init_point;
  std::vector<PolyStGraphNode> min_cost_path;
  if (!GenerateMinCostSpeedProfile(obstacles, &min_cost_path)) {
    AERROR << "Fail to generate graph!";
    return false;
  }

  std::vector<PolyStGraphNode> min_cost_speed_profile;
  if (!GenerateMinCostSpeedProfile(obstacles, &min_cost_speed_profile)) {
    AERROR << "Fail to search min cost speed profile.";
    return false;
  }
  constexpr double delta_t = 0.1;  // output resolution, in seconds
  const auto curve = min_cost_speed_profile.back().speed_profile;
  for (double t = 0.0; t < planning_time_; t += delta_t) {
    const double s = curve.Evaluate(0, t);
    const double v = curve.Evaluate(1, t);
    const double a = curve.Evaluate(2, t);
    const double da = curve.Evaluate(3, t);
    speed_data->AppendSpeedPoint(s, t, v, a, da);
  }
  return true;
}

bool PolyStGraph::GenerateMinCostSpeedProfile(
    const std::vector<const PathObstacle *> &obstacles,
    std::vector<PolyStGraphNode> *min_cost_speed_profile) {
  CHECK(min_cost_speed_profile != nullptr);
  std::vector<std::vector<STPoint>> points;
  if (!SampleStPoints(&points)) {
    AERROR << "Fail to sample st points.";
    return false;
  }
  PolyStGraphNode start_node = {STPoint(0.0, 0.0), init_point_.v(),
                                init_point_.a()};
  min_cost_speed_profile->resize(2);
  min_cost_speed_profile->front() = start_node;
  SpeedProfileCost cost(config_, obstacles);
  double min_cost = std::numeric_limits<double>::max();
  for (const auto &level : points) {
    for (const auto &st_point : level) {
      const double speed_limit = speed_limit_.GetSpeedLimitByS(st_point.s());
      constexpr int num_speed = 5;
      for (double v = 0; v < speed_limit;
           v = std::fmin(speed_limit, v + speed_limit / num_speed)) {
        PolyStGraphNode node = {st_point, v, 0.0};
        node.speed_profile = QuinticPolynomialCurve1d(
            0.0, start_node.speed, start_node.accel, node.st_point.s(),
            node.speed, node.accel, node.st_point.t());
        const double c = cost.Calculate(node.speed_profile);
        if (c < min_cost) {
          min_cost_speed_profile->back() = node;
          min_cost = c;
        }
      }
    }
  }
  return true;
}

bool PolyStGraph::SampleStPoints(
    std::vector<std::vector<STPoint>> *const points) {
  CHECK_NOTNULL(points);
  constexpr double start_t = 5.0;
  constexpr double start_s = 0.0;
  for (double t = start_t; t < planning_time_; t += unit_t_) {
    std::vector<STPoint> level_points;
    for (double s = start_s; s < planning_distance_; s += unit_s_) {
      level_points.emplace_back(s, t);
    }
    points->push_back(std::move(level_points));
  }
  return true;
}

}  // namespace planning
}  // namespace apollo
