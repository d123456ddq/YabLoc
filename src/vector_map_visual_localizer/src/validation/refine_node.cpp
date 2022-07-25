#include "common/util.hpp"
#include "validation/overlay.hpp"
#include "validation/refine.hpp"

#include <eigen3/Eigen/StdVector>
#include <opencv4/opencv2/calib3d.hpp>
#include <opencv4/opencv2/core/eigen.hpp>
#include <opencv4/opencv2/highgui.hpp>
#include <opencv4/opencv2/imgproc.hpp>
#include <sophus/geometry.hpp>

#include <pcl_conversions/pcl_conversions.h>

namespace validation
{
RefineOptimizer::RefineOptimizer() : Node("refine"), pose_buffer_{40}, tf_subscriber_(get_clock())
{
  using std::placeholders::_1, std::placeholders::_2;

  auto cb_synchro = std::bind(&RefineOptimizer::imageAndlsdCallback, this, _1, _2);
  sub_synchro_ =
    std::make_shared<SynchroSubscriber<Image, PointCloud2>>(this, "/src_image", "/lsd_cloud");
  sub_synchro_->setCallback(cb_synchro);

  // Subscriber
  auto cb_info = std::bind(&RefineOptimizer::infoCallback, this, _1);
  auto cb_pose = [this](const PoseStamped & msg) -> void { pose_buffer_.push_back(msg); };
  auto cb_ground = [this](const Float32Array & msg) -> void { ground_plane_.set(msg); };

  sub_ground_plane_ = create_subscription<Float32Array>("/ground", 10, cb_ground);
  sub_pose_ = create_subscription<PoseStamped>("/particle_pose", 10, cb_pose);
  sub_info_ = create_subscription<CameraInfo>("/src_info", 10, cb_info);
  sub_ll2_ = create_subscription<PointCloud2>(
    "/ll2_road_marking", 10,
    [this](const PointCloud2 & msg) -> void { pcl::fromROSMsg(msg, ll2_cloud_); });
}

void RefineOptimizer::infoCallback(const CameraInfo & msg)
{
  info_ = msg;
  camera_extrinsic_ = tf_subscriber_(info_->header.frame_id, "base_link");
}

void RefineOptimizer::imageAndlsdCallback(const Image & image_msg, const PointCloud2 & lsd_msg)
{
  const rclcpp::Time stamp = lsd_msg.header.stamp;

  // Search synchronized pose
  float min_dt = std::numeric_limits<float>::max();
  geometry_msgs::msg::PoseStamped synched_pose;
  for (auto pose : pose_buffer_) {
    auto dt = (rclcpp::Time(pose.header.stamp) - stamp);
    auto abs_dt = std::abs(dt.seconds());
    if (abs_dt < min_dt) {
      min_dt = abs_dt;
      synched_pose = pose;
    }
  }
  if (min_dt > 0.1) return;
  auto latest_pose_stamp = rclcpp::Time(pose_buffer_.back().header.stamp);

  cv::Mat image = util::decompress2CvMat(image_msg);
  drawOverlay(image, synched_pose.pose, latest_pose_stamp);
}

void RefineOptimizer::drawOverlay(
  const cv::Mat & image, const Pose & pose, const rclcpp::Time & stamp)
{
  if (ll2_cloud_.empty()) return;

  cv::Mat overlayed_image = cv::Mat::zeros(image.size(), CV_8UC3);
  drawOverlayLineSegments(overlayed_image, pose, extractNaerLineSegments(pose, ll2_cloud_));

  cv::Mat show_image;
  cv::addWeighted(image, 0.8, overlayed_image, 0.8, 1, show_image);
  // util::publishImage(*pub_image_, show_image, stamp);

  // TODO:
  cv::imshow("refine", show_image);
  cv::waitKey(5);
}

void RefineOptimizer::drawOverlayLineSegments(
  cv::Mat & image, const Pose & pose, const LineSegments & near_segments)
{
  Eigen::Matrix3f K =
    Eigen::Map<Eigen::Matrix<double, 3, 3>>(info_->k.data()).cast<float>().transpose();
  Eigen::Affine3f T = camera_extrinsic_.value();

  Eigen::Affine3f transform = ground_plane_.alineWithSlope(util::pose2Affine(pose));
  // Eigen::Affine3f transform = (util::pose2Affine(pose));

  auto project = [K, T, transform](const Eigen::Vector3f & xyz) -> std::optional<cv::Point2i> {
    Eigen::Vector3f from_camera = K * T.inverse() * transform.inverse() * xyz;
    if (from_camera.z() < 1e-3f) return std::nullopt;
    Eigen::Vector3f uv1 = from_camera /= from_camera.z();
    return cv::Point2i(uv1.x(), uv1.y());
  };

  for (const pcl::PointNormal & pn : near_segments) {
    auto p1 = project(pn.getArray3fMap()), p2 = project(pn.getNormalVector3fMap());
    if (!p1.has_value() || !p2.has_value()) continue;
    cv::line(image, p1.value(), p2.value(), cv::Scalar(0, 255, 255), 2);
  }
}

RefineOptimizer::LineSegments RefineOptimizer::extractNaerLineSegments(
  const Pose & pose, const LineSegments & linesegments)
{
  Eigen::Vector3f pose_vector;
  pose_vector << pose.position.x, pose.position.y, pose.position.z;

  // Compute distance between pose and linesegment of linestring
  auto checkIntersection = [this, pose_vector](const pcl::PointNormal & pn) -> bool {
    const float max_range = 40;

    const Eigen::Vector3f from = pn.getVector3fMap() - pose_vector;
    const Eigen::Vector3f to = pn.getNormalVector3fMap() - pose_vector;
    Eigen::Vector3f dir = to - from;
    float inner = from.dot(dir);
    if (std::abs(inner) < 1e-3f) {
      return from.norm() < 1.42 * max_range;
    }

    float mu = std::clamp(dir.squaredNorm() / inner, 0.f, 1.0f);
    Eigen::Vector3f nearest = from + dir * mu;
    return nearest.norm() < 2 * 1.42 * max_range;
  };

  LineSegments near_linestrings;
  for (const pcl::PointNormal & pn : linesegments) {
    if (checkIntersection(pn)) {
      near_linestrings.push_back(pn);
    }
  }
  return near_linestrings;
}
}  // namespace validation

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<validation::RefineOptimizer>());
  rclcpp::shutdown();
  return 0;
}