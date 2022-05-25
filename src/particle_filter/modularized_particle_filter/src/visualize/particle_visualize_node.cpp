#include <rclcpp/rclcpp.hpp>

#include <modularized_particle_filter_msgs/msg/particle_array.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <visualization_msgs/msg/marker_array.hpp>

namespace mpf_msgs = modularized_particle_filter_msgs;

class ParticleVisualize : public rclcpp::Node
{
public:
  ParticleVisualize() : Node("particle_visualize")
  {
    // Subscriber
    sub_particles_ = this->create_subscription<mpf_msgs::msg::ParticleArray>(
      "/particle_array", 10,
      std::bind(&ParticleVisualize::particlesCallback, this, std::placeholders::_1));

    // Publisher
    pub_marker_array =
      this->create_publisher<visualization_msgs::msg::MarkerArray>("/marker_array", 10);
  }

private:
  rclcpp::Subscription<mpf_msgs::msg::ParticleArray>::SharedPtr sub_particles_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr pub_marker_array;

  std_msgs::msg::ColorRGBA computeColor(float value) const
  {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    // clang-format off
    value = std::clamp(value, 0.0f, 1.0f);
    if (value < 0.25f) {
      r = 0; g = 4 * (value);
    } else if (value < 0.5f) {
      r = 0; b = 1 + 4 * (0.25f - value);
    } else if (value < 0.75f) {
      r = 4 * (value - 0.5f); b = 0;
    } else {
      g = 1 + 4 * (0.75f - value); b = 0;
    }
    // clang-format on

    std_msgs::msg::ColorRGBA rgba;
    rgba.r = r;
    rgba.g = g;
    rgba.b = b;
    rgba.a = 1.0f;
    return rgba;
  }

  void particlesCallback(const mpf_msgs::msg::ParticleArray & msg)
  {
    visualization_msgs::msg::MarkerArray marker_array;
    auto minmax_weight = std::minmax_element(
      msg.particles.begin(), msg.particles.end(),
      [](const mpf_msgs::msg::Particle & a, const mpf_msgs::msg::Particle & b) -> bool {
        return a.weight < b.weight;
      });

    float min = minmax_weight.first->weight;
    float max = minmax_weight.second->weight;
    max = std::max(max, min + 1e-7f);
    auto boundWeight = [min, max](float raw) -> float { return (raw - min) / (max - min); };

    int id = 0;
    for (const mpf_msgs::msg::Particle & p : msg.particles) {
      visualization_msgs::msg::Marker marker;
      marker.frame_locked = true;
      marker.header.frame_id = "map";
      marker.id = id++;
      marker.type = visualization_msgs::msg::Marker::ARROW;
      marker.scale.x = 0.3;
      marker.scale.y = 0.1;
      marker.scale.z = 0.1;
      marker.color = computeColor(boundWeight(p.weight));
      marker.pose.orientation = p.pose.orientation;
      marker.pose.position.x = p.pose.position.x;
      marker.pose.position.y = p.pose.position.y;
      marker.pose.position.z = p.pose.position.z;
      marker_array.markers.push_back(marker);
    }
    pub_marker_array->publish(marker_array);
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  rclcpp::spin(std::make_shared<ParticleVisualize>());
  rclcpp::shutdown();
  return 0;
}