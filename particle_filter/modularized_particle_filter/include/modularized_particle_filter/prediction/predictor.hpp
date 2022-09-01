#ifndef MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_
#define MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_

#include "modularized_particle_filter/common/visualize.hpp"
#include "modularized_particle_filter/prediction/resampler.hpp"

#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/pose_array.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <geometry_msgs/msg/twist_with_covariance_stamped.hpp>
#include <modularized_particle_filter_msgs/msg/particle_array.hpp>
#include <std_msgs/msg/float32.hpp>

#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>

#include <optional>

namespace modularized_particle_filter
{
class Predictor : public rclcpp::Node
{
public:
  Predictor();

private:
  using ParticleArray = modularized_particle_filter_msgs::msg::ParticleArray;
  using PoseStamped = geometry_msgs::msg::PoseStamped;
  using PoseWithCovarianceStamped = geometry_msgs::msg::PoseWithCovarianceStamped;
  using TwistWithCovarianceStamped = geometry_msgs::msg::TwistWithCovarianceStamped;
  using TwistStamped = geometry_msgs::msg::TwistStamped;
  using OptParticleArray = std::optional<ParticleArray>;

  // Subscriber
  rclcpp::Subscription<PoseWithCovarianceStamped>::SharedPtr initialpose_sub_;
  rclcpp::Subscription<PoseStamped>::SharedPtr gnss_sub_;
  rclcpp::Subscription<TwistWithCovarianceStamped>::SharedPtr twist_cov_sub_;
  rclcpp::Subscription<TwistStamped>::SharedPtr twist_sub_;
  rclcpp::Subscription<ParticleArray>::SharedPtr particles_sub_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr height_sub_;

  // Publisher
  rclcpp::Publisher<ParticleArray>::SharedPtr predicted_particles_pub_;
  rclcpp::Publisher<PoseStamped>::SharedPtr pose_pub_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf2_broadcaster_;

  rclcpp::TimerBase::SharedPtr timer_;

  const bool visualize_;
  const int number_of_particles_;
  const float resampling_interval_seconds_;
  const float static_linear_covariance_;
  const float static_angular_covariance_;
  const bool use_dynamic_noise_;
  float ground_height_;

  std::shared_ptr<ParticleVisualizer> visualizer_{nullptr};
  std::shared_ptr<RetroactiveResampler> resampler_ptr_{nullptr};
  std::optional<ParticleArray> particle_array_opt_{std::nullopt};
  std::optional<TwistWithCovarianceStamped> twist_opt_{std::nullopt};

  // Callback
  void gnssposeCallback(const PoseStamped::ConstSharedPtr initialpose);
  void initialposeCallback(const PoseWithCovarianceStamped::ConstSharedPtr initialpose);

  void initializeParticles(const PoseWithCovarianceStamped & initialpose);

  void twistCallback(const TwistStamped::ConstSharedPtr twist);
  void weightedParticlesCallback(const ParticleArray::ConstSharedPtr weighted_particles);
  void timerCallback();

  void updateWithStaticNoise(
    ParticleArray & particle_array, const TwistWithCovarianceStamped & twist);
  void updateWithDynamicNoise(
    ParticleArray & particle_array, const TwistWithCovarianceStamped & twist);

  void publishMeanPose(const geometry_msgs::msg::Pose & mean_pose, const rclcpp::Time & stamp);
};

}  // namespace modularized_particle_filter
#endif  // MODULARIZED_PARTICLE_FILTER__PREDICTION__PREDICTOR_HPP_