//=================================================================================================
// Copyright (c) 2016, Martin Oehler, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Simulation, Systems Optimization and Robotics
//       group, TU Darmstadt nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================

#ifndef LIDAR_CALIBRATION_H
#define LIDAR_CALIBRATION_H

#include <lidar_calibration/calibration.h>
#include <hector_calibration_msgs/RequestScans.h>

#include <boost/date_time.hpp>

// standard

// pcl
#include <pcl_ros/point_cloud.h>
#include <pcl/common/transforms.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/search/kdtree.h>
#include <pcl/filters/filter.h>
#include <pcl/filters/crop_box.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/passthrough.h>

// pcl segmentation
#include <pcl/ModelCoefficients.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/filters/extract_indices.h>

// pcl vis
#include <pcl/visualization/pcl_visualizer.h>

// ros
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_srvs/Empty.h>
#include <visualization_msgs/MarkerArray.h>

// tf
#include <tf/transform_listener.h>
#include <tf_conversions/tf_eigen.h>

// ceres solver
#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <lidar_calibration/point_plane_error.h>

namespace hector_calibration {

namespace lidar_calibration {

class LidarCalibration {
public:
  struct CalibrationOptions {
    CalibrationOptions() {
      max_iterations = 20;
      max_sqrt_neighbor_dist = 0.1;
      sqrt_convergence_diff_thres = 1e-6;
      normals_radius = 0.07;
      detect_ground_plane = false;
      detect_ceiling = false;
    }

    unsigned int max_iterations;
    double max_sqrt_neighbor_dist;
    double sqrt_convergence_diff_thres;
    double normals_radius;
    bool detect_ground_plane;
    bool detect_ceiling;
    Calibration init_calibration;
  };

  LidarCalibration(const ros::NodeHandle& nh);

  void setOptions(CalibrationOptions options);
  bool loadOptionsFromParamServer();
  void calibrate();
  void setManualMode(bool manual);
  void setPeriodicPublishing(bool status, double period);
  void enableNormalVisualization(bool normals);

protected:
  void publishResults();
  void publishCloud(const pcl::PointCloud<pcl::PointXYZ>& cloud, const ros::Publisher& pub);
  void publishCloud(sensor_msgs::PointCloud2& cloud, const ros::Publisher& pub);
  void timerCallback(const ros::TimerEvent&);
  void publishNeighbors(const pcl::PointCloud<pcl::PointXYZ>& cloud1,
                         const pcl::PointCloud<pcl::PointXYZ>& cloud2,
                         const std::map<unsigned int, unsigned int>& mapping) const;

  void requestScans(std::vector<LaserPoint<double> >& scan1,
                    std::vector<LaserPoint<double> >& scan2);
  std::vector<LaserPoint<double> > msgToLaserPoints(const sensor_msgs::PointCloud2& scan, const std_msgs::Float64MultiArray& angles);

  std::vector<LaserPoint<double> > cropCloud(const std::vector<LaserPoint<double> >& scan, double range);

  void applyCalibration(const std::vector<LaserPoint<double> >& scan1,
                        const std::vector<LaserPoint<double> >& scan2,
                        pcl::PointCloud<pcl::PointXYZ>& cloud1,
                        pcl::PointCloud<pcl::PointXYZ>& cloud2,
                        const Calibration& calibration);

  pcl::PointCloud<pcl::PointXYZ> laserToActuatorCloud(const std::vector<LaserPoint<double> >& laserpoints, const Calibration& calibration) const;
  std::vector<WeightedNormal> computeNormals(const pcl::PointCloud<pcl::PointXYZ>& cloud) const;
  void visualizeNormals(const pcl::PointCloud<pcl::PointXYZ>& cloud, const pcl::PointCloud<pcl::Normal>& normals) const;
  void visualizePlanarity(const pcl::PointCloud<pcl::PointXYZ> &cloud, const std::vector<WeightedNormal> &normals) const;

  std::map<unsigned int, unsigned int> findNeighbors(const pcl::PointCloud<pcl::PointXYZ> &cloud1,
                                                     const pcl::PointCloud<pcl::PointXYZ> &cloud2) const;

  Calibration optimizeCalibration(const std::vector<LaserPoint<double> >& scan1,
                                   const std::vector<LaserPoint<double> >& scan2,
                                   const Calibration& current_calibration,
                                   const std::vector<WeightedNormal> & normals,
                                   const std::map<unsigned int, unsigned int> neighbor_mapping) const;

  double detectGroundPlane(const pcl::PointCloud<pcl::PointXYZ> &cloud1, const pcl::PointCloud<pcl::PointXYZ> &cloud2) const;

  bool checkConvergence(const Calibration& prev_calibration, const Calibration& current_calibration) const;
  bool maxIterationsReached(unsigned int current_iterations) const;

  bool saveToDisk(std::string path, const Calibration& calibration) const;

  Eigen::Affine3d getTransform(std::string frame_base, std::string frame_target) const;

  tf::TransformListener tfl_;
  ros::Duration tf_wait_duration_;

  CalibrationOptions options_;
  bool save_calibration_;
  std::string save_path_;

  ros::NodeHandle nh_;
  ros::Publisher cloud1_pub_;
  ros::Publisher cloud2_pub_;
  ros::Publisher neighbor_pub_;
  ros::Publisher planarity_pub_;
  ros::Publisher ground_plane_pub_;

  sensor_msgs::PointCloud2 cloud1_msg_;
  sensor_msgs::PointCloud2 cloud2_msg_;

  std::string actuator_frame_;
  std::string laser_frame_;
  std::string ground_frame_;
  Eigen::Affine3d plane_transform_;

  std::string o_spin_frame_;
  std::string o_laser_frame_;
  Eigen::Affine3d laser_transform_;

  Eigen::Affine3d rotation_offset_;

  ros::ServiceClient request_scans_client_;
  ros::ServiceClient reset_clouds_client_;

  bool manual_mode_;
  bool vis_normals_;
  ros::Timer timer_;
};

}
}

#endif

/*** TODO ***/
/*
   1. write script to copy new calibration?
*/
