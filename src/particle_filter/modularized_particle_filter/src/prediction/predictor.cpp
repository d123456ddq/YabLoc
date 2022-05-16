#include "modularized_particle_filter/prediction/predictor.hpp"

#include "modularized_particle_filter/prediction/prediction_util.hpp"
#include "modularized_particle_filter/prediction/resampler.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "tf2_ros/transform_listener.h"

#include "geometry_msgs/msg/pose_array.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "geometry_msgs/msg/twist_with_covariance_stamped.hpp"
#include "modularized_particle_filter_msgs/msg/particle_array.hpp"

#include <tf2/utils.h>

#include <complex>
#include <iostream>
#include <numeric>

Predictor::Predictor()
: Node("predictor"),
  number_of_particles_(declare_parameter("num_of_particles", 500)),
  resampling_interval_seconds_(declare_parameter("resampling_interval_seconds", 1.0f))
{
  float prediction_rate{declare_parameter("prediction_rate", 50.0f)};

  tf2_broadcaster_ptr_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

  predicted_particles_pub_ =
    this->create_publisher<modularized_particle_filter_msgs::msg::ParticleArray>(
      "predicted_particles", 10);
  resampled_particles_pub_ =
    this->create_publisher<modularized_particle_filter_msgs::msg::ParticleArray>(
      "resampled_particles", 10);
  mean_pose_pub_ =
    this->create_publisher<geometry_msgs::msg::PoseStamped>("predicted_mean_pose", 10);

  initialpose_sub_ = this->create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
    "initialpose", 1, std::bind(&Predictor::initialposeCallback, this, std::placeholders::_1));
  twist_sub_ = this->create_subscription<geometry_msgs::msg::TwistWithCovarianceStamped>(
    "twist_with_covariance", 10, std::bind(&Predictor::twistCallback, this, std::placeholders::_1));
  weighted_particles_sub_ =
    this->create_subscription<modularized_particle_filter_msgs::msg::ParticleArray>(
      "weighted_particles", 10,
      std::bind(&Predictor::weightedParticlesCallback, this, std::placeholders::_1));

  auto timer_control_callback = std::bind(&Predictor::timerCallback, this);
  auto period_control = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::duration<double>(1.0f / prediction_rate));
  timer_ = std::make_shared<rclcpp::GenericTimer<decltype(timer_control_callback)>>(
    this->get_clock(), period_control, std::move(timer_control_callback),
    this->get_node_base_interface()->get_context());
  this->get_node_timers_interface()->add_timer(timer_, nullptr);
}

void Predictor::initialposeCallback(
  const geometry_msgs::msg::PoseWithCovarianceStamped::ConstSharedPtr initialpose)
{
  RCLCPP_INFO_STREAM(this->get_logger(), "initiposeCallback");
  modularized_particle_filter_msgs::msg::ParticleArray particle_array{};
  particle_array.header = initialpose->header;
  particle_array.id = 0;
  particle_array.particles.resize(number_of_particles_);

  const float roll{0.0f};
  const float pitch{0.0f};
  const float yaw{static_cast<float>(tf2::getYaw(initialpose->pose.pose.orientation))};
  for (int i{0}; i < particle_array.particles.size(); i++) {
    geometry_msgs::msg::Pose pose{initialpose->pose.pose};
    pose.position.x += prediction_util::nrand(sqrt(initialpose->pose.covariance[0]));
    pose.position.y += prediction_util::nrand(sqrt(initialpose->pose.covariance[6 * 1 + 1]));

    tf2::Quaternion q;
    q.setRPY(
      roll, pitch,
      prediction_util::normalizeRadian(
        yaw + prediction_util::nrand(sqrt(initialpose->pose.covariance[6 * 5 + 5]))));
    pose.orientation = tf2::toMsg(q);

    particle_array.particles[i].pose = pose;
    particle_array.particles[i].weight = 1.0;
  }
  particle_array_opt_ = particle_array;

  resampler_ptr_ = std::shared_ptr<RetroactiveResampler>(
    new RetroactiveResampler(resampling_interval_seconds_, number_of_particles_));
}

void Predictor::twistCallback(
  const geometry_msgs::msg::TwistWithCovarianceStamped::ConstSharedPtr twist)
{
  twist_opt_ = *twist;
}

void Predictor::timerCallback()
{
  if (!particle_array_opt_.has_value()) {
    return;
  }

  modularized_particle_filter_msgs::msg::ParticleArray particle_array = particle_array_opt_.value();

  if (!twist_opt_.has_value()) {
    return;
  }

  geometry_msgs::msg::TwistWithCovarianceStamped twist{twist_opt_.value()};

  rclcpp::Time current_time{this->now()};
  const float dt =
    static_cast<float>((current_time - rclcpp::Time(particle_array.header.stamp)).seconds());

  if (dt < 0.0f) {
    return;
  }

  particle_array.header.stamp = current_time;
  for (int i{0}; i < particle_array.particles.size(); i++) {
    geometry_msgs::msg::Pose pose{particle_array.particles[i].pose};

    const float roll{0.0f};
    const float pitch{0.0f};
    const float yaw{static_cast<float>(tf2::getYaw(pose.orientation))};
    const float vx{
      twist.twist.twist.linear.x + prediction_util::nrand(4 * sqrt(twist.twist.covariance[0]))};
    pose.position.x += vx * cos(yaw) * dt;
    pose.position.y += vx * sin(yaw) * dt;
    const float wz{
      twist.twist.twist.angular.z +
      prediction_util::nrand(4 * sqrt(twist.twist.covariance[5 * 6 + 5]))};
    tf2::Quaternion q;
    q.setRPY(roll, pitch, prediction_util::normalizeRadian(yaw + wz * dt));
    pose.orientation = tf2::toMsg(q);

    particle_array.particles[i].pose = pose;
  }

  predicted_particles_pub_->publish(particle_array);

  modularized_particle_filter_msgs::msg::Particle mean_state{calculateMeanState(particle_array)};

  outputMeanState(mean_state);

  particle_array_opt_ = particle_array;
}

void Predictor::weightedParticlesCallback(
  const modularized_particle_filter_msgs::msg::ParticleArray::ConstSharedPtr weighted_particles_ptr)
{
  modularized_particle_filter_msgs::msg::ParticleArray particle_array{particle_array_opt_.value()};

  std::optional<modularized_particle_filter_msgs::msg::ParticleArray>
    retroactive_weighted_particles{
      resampler_ptr_->retroactiveWeighting(particle_array, weighted_particles_ptr)};
  if (retroactive_weighted_particles.has_value()) {
    particle_array.particles = retroactive_weighted_particles.value().particles;
  }

  std::optional<modularized_particle_filter_msgs::msg::ParticleArray> resampled_particles{
    resampler_ptr_->resampling(particle_array)};
  if (resampled_particles.has_value()) {
    particle_array = resampled_particles.value();
    resampled_particles_pub_->publish(particle_array);
  }

  particle_array_opt_ = particle_array;
}

modularized_particle_filter_msgs::msg::Particle Predictor::calculateMeanState(
  const modularized_particle_filter_msgs::msg::ParticleArray particle_array)
{
  modularized_particle_filter_msgs::msg::ParticleArray normalized_particle_array{particle_array};
  modularized_particle_filter_msgs::msg::Particle mean_state{};

  auto minmax_weight = std::minmax_element(
    normalized_particle_array.particles.begin(), normalized_particle_array.particles.end(),
    [](auto p1, auto p2) { return p1.weight < p2.weight; });
  float num_of_particles_inv{1.0f / normalized_particle_array.particles.size()};
  float dif_weight{minmax_weight.second->weight - minmax_weight.first->weight};
  for (modularized_particle_filter_msgs::msg::Particle & particle :
       normalized_particle_array.particles) {
    if (dif_weight != 0.0f) {
      particle.weight = (particle.weight - minmax_weight.first->weight) / dif_weight;
    } else {
      particle.weight = num_of_particles_inv;
    }
  }

  double sum_weight{std::accumulate(
    normalized_particle_array.particles.begin(), normalized_particle_array.particles.end(), 0.0,
    [](double weight, modularized_particle_filter_msgs::msg::Particle & particle) {
      return weight + particle.weight;
    })};
  if (std::isinf(sum_weight)) {
    RCLCPP_WARN_STREAM(this->get_logger(), "sum_weight: " << sum_weight);
  }

  std::vector<double> rolls{};
  std::vector<double> pitches{};
  std::vector<double> yaws{};
  std::vector<double> weights{};
  for (modularized_particle_filter_msgs::msg::Particle particle :
       normalized_particle_array.particles) {
    double weight{1.0 / normalized_particle_array.particles.size()};
    if (0.0f < sum_weight) {
      weight = particle.weight / sum_weight;
    }
    mean_state.pose.position.x += particle.pose.position.x * weight;
    mean_state.pose.position.y += particle.pose.position.y * weight;
    mean_state.pose.position.z += particle.pose.position.z * weight;

    double yaw{0.0}, pitch{0.0}, roll{0.0};
    tf2::getEulerYPR(particle.pose.orientation, yaw, pitch, roll);

    rolls.push_back(roll);
    pitches.push_back(pitch);
    yaws.push_back(yaw);
    weights.push_back(weight);
  }

  const double mean_roll{prediction_util::meanRadian(rolls, weights)};
  const double mean_pitch{prediction_util::meanRadian(pitches, weights)};
  const double mean_yaw{prediction_util::meanRadian(yaws, weights)};
  tf2::Quaternion q;
  q.setRPY(mean_roll, mean_pitch, mean_yaw);
  mean_state.pose.orientation = tf2::toMsg(q);
  return mean_state;
}

void Predictor::outputMeanState(const modularized_particle_filter_msgs::msg::Particle mean_state)
{
  modularized_particle_filter_msgs::msg::ParticleArray particle_array{particle_array_opt_.value()};

  geometry_msgs::msg::PoseStamped meanPose{};
  meanPose.header = particle_array.header;
  meanPose.pose = mean_state.pose;
  mean_pose_pub_->publish(meanPose);

  geometry_msgs::msg::TransformStamped transform{};
  transform.header.stamp = particle_array.header.stamp;
  transform.header.frame_id = "map";
  transform.child_frame_id = "mpf";
  transform.transform.translation.x = mean_state.pose.position.x;
  transform.transform.translation.y = mean_state.pose.position.y;
  transform.transform.translation.z = mean_state.pose.position.z;
  transform.transform.rotation = mean_state.pose.orientation;
  tf2_broadcaster_ptr_->sendTransform(transform);
}