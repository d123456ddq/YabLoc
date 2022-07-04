#pragma once
#include <eigen3/Eigen/Geometry>
#include <opencv4/opencv2/core.hpp>
#include <rclcpp/publisher.hpp>

#include <geometry_msgs/msg/pose.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_msgs/msg/color_rgba.hpp>
#include <ublox_msgs/msg/nav_pvt.hpp>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

namespace util
{
std_msgs::msg::ColorRGBA toRgba(float value);
std_msgs::msg::ColorRGBA color(float red, float green, float blue, float alpha = 1.0f);

cv::Mat decompress2CvMat(const sensor_msgs::msg::Image & img);

sensor_msgs::msg::Image::ConstSharedPtr decompress2RosMsg(
  const sensor_msgs::msg::CompressedImage & compressed_img);

cv::Mat decompress2CvMat(const sensor_msgs::msg::CompressedImage & compressed_img);
sensor_msgs::msg::Image::ConstSharedPtr decompress2RosMsg(
  const sensor_msgs::msg::CompressedImage & compressed_img);

void publishImage(
  rclcpp::Publisher<sensor_msgs::msg::Image> & publisher, const cv::Mat & image,
  const rclcpp::Time & stamp);

Eigen::Affine3f pose2Affine(const geometry_msgs::msg::Pose & pose);

rclcpp::Time ubloxTime2Stamp(const ublox_msgs::msg::NavPVT & msg);

void publishCloud(
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2> & publisher,
  const pcl::PointCloud<pcl::PointNormal> & cloud, const rclcpp::Time & stamp);

}  // namespace util