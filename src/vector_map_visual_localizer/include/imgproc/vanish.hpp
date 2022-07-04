#pragma once
#include "common/static_tf_subscriber.hpp"
#include "imgproc/ransac_vanish_point.hpp"

#include <eigen3/Eigen/StdVector>
#include <rclcpp/rclcpp.hpp>
#include <sophus/geometry.hpp>

#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>

namespace imgproc
{

class VanishPoint : public rclcpp::Node
{
public:
  using Imu = sensor_msgs::msg::Imu;
  using Image = sensor_msgs::msg::Image;
  using CameraInfo = sensor_msgs::msg::CameraInfo;

  VanishPoint();

private:
  common::StaticTfSubscriber tf_subscriber_;

  rclcpp::Subscription<Image>::SharedPtr sub_image_;
  rclcpp::Subscription<Imu>::SharedPtr sub_imu_;
  rclcpp::Subscription<CameraInfo>::SharedPtr sub_info_;

  std::optional<CameraInfo> info_{std::nullopt};
  RansacVanishPoint ransac_vanish_point_;

  Sophus::SO3f rotation_;
  std::list<Imu> imu_buffer_;
  std::optional<rclcpp::Time> last_imu_stamp_{std::nullopt};

  void integral(const rclcpp::Time & stamp);
  void callbackImu(const Imu & msg);
  void callbackImage(const Image & msg);
};
}  // namespace imgproc