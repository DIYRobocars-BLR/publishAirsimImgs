#include "input_sampler.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include "common/Common.hpp"

input_sampler::input_sampler() : client(0), localization_method("ground_truth")
{
	connect();
}

input_sampler::input_sampler(const std::string& ip_addr, uint16_t port) : client(0), localization_method("ground_truth")
{
	connect(ip_addr, port);
}


input_sampler::input_sampler(const std::string& ip_addr, uint16_t port, std::string localization_method) : client(0)
{
	connect(ip_addr, port);
    this->localization_method = localization_method;
}



input_sampler::~input_sampler()
{
	if (client != 0)
		delete client;
}

void input_sampler::connect()
{
	if (client != 0)
		delete client;
	client = new msr::airlib::MultirotorRpcLibClient();
}

void input_sampler::connect(const std::string& ip_addr, uint16_t port)
{
	if (client != 0)
		delete client;
	client = new msr::airlib::MultirotorRpcLibClient(ip_addr, port);
}


static void convertToPlanDepth(const cv::Mat& input, cv::Mat& output, float f = 128)
{
	int width = input.cols;
	int height = input.rows;

	float center_i = width / 2.0f - 1;
	float center_j = height / 2.0f - 1;

    // output = cv::Mat(height, width, CV_32FC1);

	for (int i = 0; i < width; ++i) {
		for (int j = 0; j < height; ++j) {
			float dist = std::sqrt((i - center_i)*(i - center_i) + (j - center_j)*(j - center_j));
			float denom = (dist / f);
			denom *= denom;
			denom = std::sqrt(1 + denom);
			output.at<float>(j, i) = input.at<float>(j, i) / denom;
		}
	}
}


static void convertToDisparity(const cv::Mat& input, cv::Mat& output, float f = 128, float baseline_meters = 0.14)
{
	int width = input.cols;
	int height = input.rows;
	int size = width * height;

	output = cv::Mat(height, width, CV_32FC1);

	for (int i = 0; i < width; ++i) {
		for (int j = 0; j < height; ++j) {
			output.at<float>(j, i) = f * baseline_meters / input.at<float>(j, i);
		}
	}
}


struct image_response input_sampler::poll_frame()
{
	using ImageRequest = msr::airlib::VehicleCameraBase::ImageRequest;
	using ImageResponse = msr::airlib::VehicleCameraBase::ImageResponse;
	using ImageType = msr::airlib::VehicleCameraBase::ImageType;

	struct image_response result;
	const int max_tries = 1000000;

	std::vector<ImageRequest> request = {
		// ImageRequest(0, ImageType::Scene),
		ImageRequest(0, ImageType::Scene),
	    ImageRequest(0, ImageType::DepthPlanner, true)
	};

    result.twist = twist();
	std::vector<ImageResponse> response = client->simGetImages(request);

	for (int i = 0; response.size() != request.size() && i < max_tries; i++) {
		response = client->simGetImages(request);
	}

	if (response.size() == request.size()) {
#if CV_MAJOR_VERSION==3
		// result.left = cv::imdecode(response.at(0).image_data_uint8, cv::IMREAD_COLOR);
		result.right = cv::imdecode(response.at(0).image_data_uint8, cv::IMREAD_COLOR);
		// result.depth = cv::imdecode(response.at(1).image_data_uint8, cv::IMREAD_GRAYSCALE);
#else
		// result.left = cv::imdecode(response.at(0).image_data_uint8, CV_LOAD_IMAGE_COLOR);
		result.right = cv::imdecode(response.at(0).image_data_uint8, CV_LOAD_IMAGE_COLOR);
		// result.depth = cv::imdecode(response.at(1).image_data_uint8, CV_LOAD_IMAGE_GRAYSCALE);
#endif

        int width = response.at(1).width;
        int height = response.at(1).height;
        std::vector<float>& floats = response.at(1).image_data_float;

        result.depth = cv::Mat(height, width, CV_32FC1);
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
                float dist = floats[i*width + j];
                result.depth.at<float>(i,j) = dist; 
            }
        }

        // result.depth.convertTo(result.depth, CV_32FC1);

        // result.planar_depth = cv::Mat(height, width, CV_32FC1);
        // convertToPlanDepth(result.depth, result.depth);

        // result.disparity = cv::Mat(height, width, CV_32FC1);
        // convertToDisparity(result.depth, result.disparity);
	} else {
		std::cerr << "Images not returned successfully" << std::endl;
	}

        
    //ground truth values
    static auto initial_pos_gt= response.back().camera_position;
    result.pose_gt.position.x = response.back().camera_position.x() - initial_pos_gt.x();
    result.pose_gt.position.y = response.back().camera_position.y() - initial_pos_gt.y();
    result.pose_gt.position.z = response.back().camera_position.z() - initial_pos_gt.z();

    result.pose_gt.orientation.x = response.back().camera_orientation.x();
    result.pose_gt.orientation.y = response.back().camera_orientation.y();
    result.pose_gt.orientation.z = response.back().camera_orientation.z();
    result.pose_gt.orientation.w = response.back().camera_orientation.w();
     
    if(this->localization_method == "gps") {
        static auto initial_pos_gps = client->getPosition();
        auto p = client->getPosition();
	    auto q = client->getOrientation();
        result.pose.position.x = p.x() - initial_pos_gps.x();
        result.pose.position.y = p.y() - initial_pos_gps.y();
        result.pose.position.z = p.z() - initial_pos_gps.z();

        result.pose.orientation.x = q.x();
        result.pose.orientation.y = q.y();
        result.pose.orientation.z = q.z();
        result.pose.orientation.w = q.w();
    }

	return result;
}

geometry_msgs::Twist input_sampler::twist()
{
    geometry_msgs::Twist result;
	auto lv = client->getVelocity();

    // Set linear velocities
    result.linear.x = lv.y();
	result.linear.y = lv.x();
	result.linear.z = -(lv.z());

    // Set angular velocities (we can't currently get them, so just assume they're all 0)
    result.angular.x = 0;
    result.angular.y = 0;
    result.angular.z = 0;

    return result;
}

