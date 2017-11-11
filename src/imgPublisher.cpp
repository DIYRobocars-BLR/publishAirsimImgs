// #include "ros/ros.h"
#include "std_msgs/String.h"
#include <image_transport/image_transport.h>
#include <nav_msgs/Odometry.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <sstream>
#include "vehicles/multirotor/api/MultirotorRpcLibClient.hpp"
#include <iostream>
#include <chrono>
#include <math.h>
#include <iterator>
#include "common/Common.hpp"
#include <fstream>
#include "input_sampler.h"
#include "Callbacks/callbacks.h"
#include <signal.h>
#include "stereo_msgs/DisparityImage.h"

using namespace std;
string localization_method;
msr::airlib::MultirotorRpcLibClient * client;
void sigIntHandler(int sig)
{
    ros::shutdown();
    exit(0);
}

sensor_msgs::CameraInfo getCameraParams(){
    double Tx, Fx, Fy, cx, cy, width, height;
    sensor_msgs::CameraInfo CameraParam;

    // Read camera parameters from launch file
    ros::param::get("/airsim_imgPublisher/Tx",Tx);
    ros::param::get("/airsim_imgPublisher/Fx",Fx);
    ros::param::get("/airsim_imgPublisher/Fy",Fy);
    ros::param::get("/airsim_imgPublisher/cx",cx);
    ros::param::get("/airsim_imgPublisher/cy",cy);
    ros::param::get("/airsim_imgPublisher/scale_x",width);
    ros::param::get("/airsim_imgPublisher/scale_y",height);

    //CameraParam.header.frame_id = "camera";
    CameraParam.header.frame_id = localization_method;

    CameraParam.height = height;
    CameraParam.width = width;

    CameraParam.distortion_model = "plumb_bob";
    CameraParam.D = {0.0, 0.0, 0.0, 0.0, 0.0};

    CameraParam.K = {Fx,  0.0, cx, 
                     0.0, Fy,  cy, 
                     0.0, 0.0, 1};
    CameraParam.R = {1.0, 0.0, 0.0, 
                     0.0, 1.0, 0.0,
                     0.0, 0.0, 1.0};
    CameraParam.P = {Fx,  0.0, cx,  Tx, 
                     0.0, Fy,  cy,  0.0, 
                     0.0, 0.0, 1.0, 0.0};

    CameraParam.binning_x = 0;
    CameraParam.binning_y = 0;

    return CameraParam;
}

void CameraPosePublisher(geometry_msgs::Pose CamPose)
{
    static tf::TransformBroadcaster br;
    tf::Transform transformQuad, transformCamera;
   
     static tf::TransformBroadcaster br_gps;
     tf::Transform transformCamera_gps;
    

    const double sqrt_2 = 1.41421356237;

    transformCamera.setOrigin(tf::Vector3(CamPose.position.y,
                                        CamPose.position.x,
                                        -CamPose.position.z));

    geometry_msgs::Vector3 rpy =  quat2rpy(CamPose.orientation);
    rpy.y = -rpy.y;
    rpy.z = -rpy.z + M_PI/2.0;

    geometry_msgs::Quaternion q_body2cam = setQuat(0.5, -0.5, 0.5, -0.5);

    geometry_msgs::Quaternion q_cam = rpy2quat(rpy);
    q_cam = quatProd(q_body2cam, q_cam);
    transformCamera.setRotation(tf::Quaternion(q_cam.x,
                                             q_cam.y,
                                             q_cam.z, 
                                             q_cam.w));

    
    br.sendTransform(tf::StampedTransform(transformCamera, ros::Time::now(), "world", "ground_truth"));
    
     
    if(localization_method == "gps") {
        auto p = client->getPosition();
	    auto orien = client->getOrientation();
        transformCamera_gps.setOrigin(tf::Vector3(p.y(),
                    p.x(),
                    -p.z()));
        
        tf::Quaternion rotation(orien.x(), orien.y(), orien.z(), orien.w());
        
        //tf::Quaternion rotation = transform.getRotation();
        double roll, pitch, yaw;
        tf::Matrix3x3(rotation).getRPY(roll, pitch, yaw);
        rotation.setRPY(roll-M_PI/2, pitch, -yaw);
        transformCamera_gps.setRotation(rotation);

        /* 
        geometry_msgs::Quaternion q;
        q.x = orien.x();
        q.y = orien.y();
        q.z = orien.z();
        q.w = orien.w();

        
        geometry_msgs::Vector3 rpy_gps =  quat2rpy(q);
        rpy_gps.y = -rpy_gps.y;
        rpy_gps.z = -rpy_gps.z + M_PI/2.0;

        geometry_msgs::Quaternion q_body2cam_gps = setQuat(0.5, -0.5, 0.5, -0.5);

        geometry_msgs::Quaternion q_cam_gps = rpy2quat(rpy_gps);
        q_cam_gps = quatProd(q_body2cam_gps, q_cam_gps);
        transformCamera.setRotation(tf::Quaternion(q_cam_gps.x,
                    q_cam_gps.y,
                    q_cam_gps.z, 
                    q_cam_gps.w));

                    */
       /* 
        transformCamera_gps.setRotation(tf::Quaternion(q.x(),
                    q.y(),
                    q.z(), ////
                    q.w()));
    */
        br_gps.sendTransform(tf::StampedTransform(transformCamera_gps, ros::Time::now(), "world", "gps"));
    }
    //br.sendTransform(tf::StampedTransform(transformCamera, ros::Time::now(), "world", "camera"));
}

int main(int argc, char **argv)
{
  
    
    //Start ROS ----------------------------------------------------------------
  ros::init(argc, argv, "airsim_imgPublisher");
  ros::NodeHandle n;
  ros::Rate loop_rate(50);
  signal(SIGINT, sigIntHandler);

    
  //Publishers ---------------------------------------------------------------
  image_transport::ImageTransport it(n);

  // image_transport::Publisher imgL_pub = it.advertise("/Airsim/left/image_raw", 1);
  image_transport::Publisher imgR_pub = it.advertise("/Airsim/right/image_raw", 1);
  image_transport::Publisher depth_pub = it.advertise("/Airsim/depth", 1);

   ros::Publisher imgParamL_pub = n.advertise<sensor_msgs::CameraInfo> ("/Airsim/left/camera_info", 1);
  ros::Publisher imgParamR_pub = n.advertise<sensor_msgs::CameraInfo> ("/Airsim/right/camera_info", 1);
  ros::Publisher imgParamDepth_pub = n.advertise<sensor_msgs::CameraInfo> ("/Airsim/camera_info", 1);
  ros::Publisher disparity_pub = n.advertise<stereo_msgs::DisparityImage> ("/Airsim/disparity", 1);
  //ROS Messages
  sensor_msgs::ImagePtr msgImgL, msgImgR, msgDepth;
  sensor_msgs::CameraInfo msgCameraInfo;

  //Parameters for communicating with Airsim
  string ip_addr;
  int portParam;
  ros::param::get("/airsim_imgPublisher/Airsim_ip",ip_addr);
  ros::param::get("/airsim_imgPublisher/Airsim_port", portParam);
  uint16_t port = portParam;

  // Parameter for localizing camera
  if(!ros::param::get("/airsim_imgPublisher/localization_method", localization_method)){
    ROS_FATAL_STREAM("you have not set the localization method");
    return -1;
  }

   //this connects us to the drone 
  client = new msr::airlib::MultirotorRpcLibClient(ip_addr, port);
  //client->enableApiControl(false);


  //Verbose
  ROS_INFO("Image publisher started! Connecting to:");
  ROS_INFO("IP: %s", ip_addr.c_str());
  ROS_INFO("Port: %d", port);
  
  //Local variables
  input_sampler input_sampler__obj(ip_addr.c_str(), port);
  msgCameraInfo = getCameraParams();

  // *** F:DN end of communication with simulator (Airsim)

  while (ros::ok())
  {
    auto imgs = input_sampler__obj.poll_frame();


    
    cv::Mat disparityImageMat;
    imgs.depth.convertTo(disparityImageMat, CV_8UC1);
    stereo_msgs::DisparityImage disparityImg;
    disparityImg.header.stamp = ros::Time::now();
    
    disparityImg.header.frame_id= localization_method;
    //disparityImg.header.frame_id= "camera";
    
    disparityImg.f = 128; //focal length, half of the image width
    disparityImg.T = .14; //baseline, half of the distance between the two cameras
    disparityImg.min_disparity = .44; // f.t/z(depth max)
    disparityImg.max_disparity = 179; // f.t/z(depth min)
    disparityImg.delta_d = .018; //possibly change
    disparityImg.image = *(cv_bridge::CvImage(std_msgs::Header(), "8UC1", disparityImageMat).toImageMsg());
    disparityImg.valid_window.x_offset = 0;
    disparityImg.valid_window.y_offset = 0;
    disparityImg.valid_window.height =  144;
    disparityImg.valid_window.width =  256;
    disparityImg.valid_window.do_rectify =  false; //possibly change
    


    // *** F:DN conversion of opencv images to ros images
    // msgImgL = cv_bridge::CvImage(std_msgs::Header(), "bgr8", imgs.left).toImageMsg();
    msgImgR = cv_bridge::CvImage(std_msgs::Header(), "bgr8", imgs.right).toImageMsg();
    msgDepth = cv_bridge::CvImage(std_msgs::Header(), "32FC1", imgs.depth).toImageMsg();

    //Stamp messages
    msgCameraInfo.header.stamp = ros::Time::now();
    // msgImgL->header.stamp = msgCameraInfo.header.stamp;
    msgImgR->header.stamp = msgCameraInfo.header.stamp;
    msgDepth->header.stamp =  msgCameraInfo.header.stamp;

    // Set the frame ids
    msgDepth->header.frame_id = localization_method;
    //msgDepth->header.frame_id = "camera";

    //Publish transforms into tf tree
    CameraPosePublisher(imgs.pose);

    //Publish images
    // imgL_pub.publish(msgImgL);
    imgR_pub.publish(msgImgR);
    depth_pub.publish(msgDepth);
    imgParamL_pub.publish(msgCameraInfo);
    imgParamR_pub.publish(msgCameraInfo);
    imgParamDepth_pub.publish(msgCameraInfo);
    disparity_pub.publish(disparityImg);
    
    ros::spinOnce();
    
    loop_rate.sleep();
  }

  return 0;
}

