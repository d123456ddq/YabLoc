#include "vmvl_imgproc/segment_filter.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<vmvl_imgproc::SegmentFilter>());
  rclcpp::shutdown();
  return 0;
}
