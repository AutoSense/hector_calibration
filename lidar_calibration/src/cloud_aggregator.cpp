#include <lidar_calibration/cloud_aggregator.h>

namespace hector_calibration {
  CalibrationCloudAggregator::CalibrationCloudAggregator() {
    prior_roll_angle_ = 0.0;
    captured_clouds_ = 0;

    scan_sub_ = nh_.subscribe("cloud", 10, &CalibrationCloudAggregator::cloudCallback, this);
    reset_sub_ = nh_.subscribe("reset_clouds", 10, &CalibrationCloudAggregator::resetCallback, this);
    point_cloud1_pub_ = nh_.advertise<sensor_msgs::PointCloud2>("half_scan_1",10,false);
    point_cloud2_pub_ = nh_.advertise<sensor_msgs::PointCloud2>("half_scan_2",10,false);

    reset_clouds_srv_ = nh_.advertiseService("reset_clouds", &CalibrationCloudAggregator::resetSrvCallback, this);

    ros::NodeHandle pnh_("~");
    pnh_.param("target_frame", p_target_frame_, std::string("base_link"));

    tfl_.reset(new tf::TransformListener());
    wait_duration_ = ros::Duration(0.5);
  }

  void CalibrationCloudAggregator::publishClouds() {
    if (captured_clouds_ < 3) {
      return;
    }
    publishCloud(point_cloud1_pub_, cloud1_);
    publishCloud(point_cloud2_pub_, cloud2_);
  }

  void CalibrationCloudAggregator::publishCloud(const ros::Publisher& pub, sensor_msgs::PointCloud2& cloud_msg) {
    cloud_msg.header.frame_id = p_target_frame_;
    cloud_msg.header.stamp = ros::Time::now();
    pub.publish(cloud_msg);
  }

  void CalibrationCloudAggregator::transformCloud(const std::vector<pc_roll_tuple>& cloud_agg, sensor_msgs::PointCloud2& cloud) {
    pcl::PointCloud<pcl::PointXYZ> tmp_agg_cloud;
    for (size_t i=0; i < cloud_agg.size(); ++i){
      Eigen::Affine3d sensor_to_actuator(Eigen::AngleAxisd(cloud_agg[i].second, Eigen::Vector3d::UnitX()));
      pcl::PointCloud<pcl::PointXYZ> pc_tmp;
      pcl::transformPointCloud(*cloud_agg[i].first, pc_tmp, sensor_to_actuator);

      if (tmp_agg_cloud.empty()){
        tmp_agg_cloud = pc_tmp;
      }else{
        tmp_agg_cloud += pc_tmp;
      }
    }
    pcl::toROSMsg(tmp_agg_cloud, cloud);
  }

  void CalibrationCloudAggregator::setPeriodicPublishing(bool status, double period) {
    if (status) {
      ROS_INFO_STREAM("[CloudAggregator] Enabled periodic cloud publishing.");
      timer_ = nh_.createTimer(ros::Duration(period), &CalibrationCloudAggregator::timerCallback, this, false);
    } else {
      ROS_INFO_STREAM("[CloudAggregator] Disabled periodic cloud publishing.");
      timer_.stop();
    }

  }

  void CalibrationCloudAggregator::timerCallback(const ros::TimerEvent&) {
    publishClouds();
  }

  void CalibrationCloudAggregator::resetCallback(const std_msgs::Empty::ConstPtr&) {
    resetClouds();
  }

  bool CalibrationCloudAggregator::resetSrvCallback(std_srvs::Empty::Request&, std_srvs::Empty::Response&) {
    resetClouds();
    return true;
  }

  void CalibrationCloudAggregator::resetClouds() {
    cloud_agg1_.clear();
    cloud_agg2_.clear();
    captured_clouds_ = 0;
    prior_roll_angle_ = 0.0;
    request_scans_srv_.shutdown();
    ROS_INFO_STREAM("[CloudAggregator] Resetted half scans.");
  }

  void CalibrationCloudAggregator::scanToMsg(const std::vector<pc_roll_tuple>& cloud_agg,
                                             sensor_msgs::PointCloud2& scan,
                                             std_msgs::Float64MultiArray& angles)
  {
    pcl::PointCloud<pcl::PointXYZ> tmp_scan_cloud;
    std::vector<double> angle_agg;
    for (size_t i=0; i < cloud_agg.size(); ++i){
      for (unsigned int j = 0; j < cloud_agg[i].first->size(); j++) {
        angle_agg.push_back(cloud_agg[i].second);
      }
      if (tmp_scan_cloud.empty()){
        tmp_scan_cloud = *cloud_agg[i].first;
      }else{
        tmp_scan_cloud += *cloud_agg[i].first;
      }
    }
    pcl::toROSMsg(tmp_scan_cloud, scan);
    angles.data = angle_agg;
  }


  bool CalibrationCloudAggregator::requestScansCallback(
      lidar_calibration::RequestScans::Request& request,
      lidar_calibration::RequestScans::Response& response) {
    scanToMsg(cloud_agg1_, response.scan_1, response.angles1);
    scanToMsg(cloud_agg2_, response.scan_2, response.angles2);
    return true;
  }

  void CalibrationCloudAggregator::cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud_in) {
    if (captured_clouds_ > 2) {
      // don't need more than 3 half scans (dump first)
      return;
    }
    if (tfl_->waitForTransform(p_target_frame_, cloud_in->header.frame_id, cloud_in->header.stamp, wait_duration_)){
      tf::StampedTransform transform;
      tfl_->lookupTransform(p_target_frame_, cloud_in->header.frame_id, cloud_in->header.stamp, transform);

      double roll, pitch, yaw;
      tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch, yaw);

      if (((prior_roll_angle_ < (-M_PI*0.5)) && (roll > (-M_PI*0.5))) ||
          ((prior_roll_angle_ < ( M_PI*0.5)) && (roll > ( M_PI*0.5)))){
        // mark cloud as complete
        captured_clouds_++;
        ROS_INFO_STREAM("[CloudAggregator] Captured half scan number: " << captured_clouds_);
        if (captured_clouds_ == 3) {
          request_scans_srv_ = nh_.advertiseService("request_scans", &CalibrationCloudAggregator::requestScansCallback, this);
          transformCloud(cloud_agg1_, cloud1_);
          transformCloud(cloud_agg2_, cloud2_);
          publishClouds();
        }
      } else {
        if (captured_clouds_ == 0) { // skip first half scan
          return;
        }
        // add point cloud to current aggregator
        boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ> > pc;
        pc.reset(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*cloud_in, *pc);

        double roll, pitch, yaw;
        tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch, yaw);

        // skip first half scan
        if (captured_clouds_ == 1) {
          cloud_agg1_.push_back(pc_roll_tuple(pc, roll));
        } else if (captured_clouds_ == 2) {
          cloud_agg2_.push_back(pc_roll_tuple(pc, roll));
        }
      }

      prior_roll_angle_ = roll;
    }else{
      ROS_ERROR_THROTTLE(5.0, "Cannot transform from sensor %s to target %s . This message is throttled.",
                         cloud_in->header.frame_id.c_str(),
                         p_target_frame_.c_str());
    }
  }
}
