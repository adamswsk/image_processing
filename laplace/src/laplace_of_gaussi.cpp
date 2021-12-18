// C++
#include <cmath>
#include <assert.h>
#include <iomanip>
#include <iostream>
// self
#include "laplace_of_gaussi.h"


namespace {
    cv::Mat make_pad(const cv::Mat &one_image, const int pad_H, const int pad_W) {
        cv::Mat padded_image;
        cv::copyMakeBorder(one_image, padded_image, pad_H, pad_H, pad_W, pad_W, cv::BORDER_REPLICATE);
        return padded_image;
    }

    inline double fast_exp(const double y) {
        double d;
        *(reinterpret_cast<int*>(&d) + 0) = 0;
        *(reinterpret_cast<int*>(&d) + 1) = static_cast<int>(1512775 * y + 1072632447);
        return d;
    }

    void cv_show(const cv::Mat& one_image, const char* info="") {
        cv::imshow(info, one_image);
        cv::waitKey(0);
        cv::destroyAllWindows();
    }
}


cv::Mat laplace_extract_edges(const cv::Mat& source) {
    // 获取信息
    const int H = source.rows;
    const int W = source.cols;
    // padding
    const auto padded_image = make_pad(source, 1, 1);
    const int W2 = padded_image.cols;
    // 准备结果
    auto result = source.clone();
    // 开始滤波
    for(int i = 0;i < H; ++i) {
        const uchar* row_ptr = padded_image.data + (1 + i) * W2 + 1;
        uchar* const res_ptr = result.data + i * W;
        for(int j = 0;j < W; ++j) {
            // 每个点, 找周围几个点, 算一下
            const uchar u = row_ptr[j - W2], d = row_ptr[j + W2], l = row_ptr[j - 1], r = row_ptr[j + 1];
            res_ptr[j] = cv::saturate_cast<uchar>(std::abs(u + d + l + r - 4 * row_ptr[j]));
            /*
            const uchar u = row_ptr[j - W2], d = row_ptr[j + W2], l = row_ptr[j - 1], r = row_ptr[j + 1];
            const uchar u_1 = row_ptr[j - W2], u_2 = row_ptr[j - 1], d_1 = row_ptr[j + W2], d_2 = row_ptr[j + 1];
            double value = u + d + l + r + u_1 + u_2 + d_1 + d_2 - 8 * row_ptr[j];
            if(value < 0) value = -value;
            res_ptr[j] = cv::saturate_cast<uchar>(value);
            */
        }
    }
    cv::threshold(result, result, 50, 255, cv::THRESH_BINARY);
    return result;
}







std::vector<double> laplace_of_gaussi(const cv::Mat& source, const int radius, const double sigma) {
    // padding 处理边缘
    const auto padded_image = make_pad(source, radius, radius);
    const int W2 = padded_image.cols;
    // 准备一个 LOG 模板
    const int window_len = (radius << 1) + 1;
    const int window_size = window_len * window_len;
    const double sigma_2 = sigma * sigma;
    const double sigma_6 = sigma_2 * sigma_2 * sigma_2;
    double LOG[window_size];
    int LOG_offset[window_size];
    int offset = 0;
    for(int i = -radius; i <= radius; ++i) {
        for(int j = -radius; j <= radius; ++j) {
            const double distance = i * i + j * j;
            LOG[offset] = (distance - 2 * sigma_2) / sigma_6 * std::exp(-distance / (2 * sigma_2));
            LOG_offset[offset] = i * W2 + j;
            ++offset;
        }
    }
    for(int i = 0;i < offset; ++i) {
        std::cout << std::setprecision(3) << LOG[i] << " ";
        if((i + 1) % window_len == 0) std::cout << "\n";
    }
    /*
     *
     0.001 0.011  0.044  0.067  0.044 0.011 0.001
     0.011 0.100  0.272  0.326  0.272 0.100 0.011
     0.044 0.272  0.088 -0.629  0.088 0.272 0.044
     0.067 0.326 -0.629 -2.460 -0.629 0.326 0.067
     0.043 0.272  0.088 -0.629  0.088 0.272 0.044
     0.011 0.100  0.272  0.326  0.272 0.100 0.011
     0.001 0.011  0.044  0.067  0.044 0.011 0.001
     */
    // 平坦区域, LOG 响应为 0
    double sum_value = 0.0;
    for(int i = 0;i < offset; ++i) sum_value += LOG[i];
    for(int i = 0;i < offset; ++i) LOG[i] -= sum_value / offset;
    // 收集原始图像信息
    const int H = source.rows;
    const int W = source.cols;
    const int length = H * W;
    // LOG 模板扫过
    std::vector<double> LOG_result(length, 0);
    for(int i = 0;i < H; ++i) {
        const uchar* const row_ptr = padded_image.data + i * W2;
        double* const res_ptr = LOG_result.data() + i * W;
        for(int j = 0;j < W; ++j) {
            // 开始卷积
            double conv_sum = 0;
            for(int k = 0;k < offset; ++k)
                conv_sum += LOG[k] * row_ptr[j + LOG_offset[k]];
            res_ptr[j] = conv_sum;
        }
    }
    return LOG_result;
}


#include "faster_gaussi_filter.h"
std::vector<double> difference_of_gaussi(const cv::Mat& source, const int radius, const double sigma, const double k) {
    // 两个高斯卷积
    const auto lhs = faster_2_gaussi_filter_channel(source, 2 * radius + 1, k * sigma, k * sigma);
    const auto rhs = faster_2_gaussi_filter_channel(source, 2 * radius + 1, sigma, sigma);
    // 准备结果
    const int length = source.rows * source.cols;
    std::vector<double> result_double(length, 0);
    for(int i = 0;i < length; ++i) result_double[i] = lhs.data[i] - rhs.data[i];  //
    return  result_double;
}


cv::Mat laplace_of_gaussi_edge_detection(const cv::Mat& source, const int radius, const double sigma, const double threshold) {
    // LOG 卷积一遍
    // const auto LOG_result = laplace_of_gaussi(source, radius, sigma);
    const auto LOG_result = difference_of_gaussi(source, radius, sigma, 1.1);
    const int H = source.rows;
    const int W = source.cols;
    // 准备结果
    auto result_image = source.clone();
    // 现在 LOG_result 里面是结果, 开始找过 0 点
    for(int i = 1;i < H - 1; ++i) {
        const double* const row_ptr = LOG_result.data() + i * W;
        uchar* const res_ptr = result_image.data + i * W;
        for(int j = 1;j < W - 1; ++j) {
            // 开始找四个方向
            if((row_ptr[j - W] * row_ptr[j + W] < 0 and std::abs(row_ptr[j - W] - row_ptr[j + W]) > threshold) or
               (row_ptr[j - 1] * row_ptr[j + 1] < 0 and std::abs(row_ptr[j - 1] - row_ptr[j + 1]) > threshold) or
               (row_ptr[j - W - 1] * row_ptr[j + W + 1] < 0 and std::abs(row_ptr[j - W - 1] - row_ptr[j + W + 1]) > threshold) or
               (row_ptr[j - W + 1] * row_ptr[j + W - 1] < 0 and std::abs(row_ptr[j - W + 1] - row_ptr[j + W - 1]) > threshold)) {
                res_ptr[j] = 255;
            }
            else res_ptr[j] = 0;
        }
    }
    return result_image;
}




// 单通道
std::pair< keypoints_type, keypoints_type > laplace_of_gaussi_keypoints_detection(const cv::Mat& source, const int radius, const std::vector< std::pair<double, double > > sigma_list, const double threshold) {
    const int C = source.channels();
    if(C != 1) {
        std::cout << "只支持单通道图像 !" << std::endl;
        return std::pair< keypoints_type, keypoints_type >();
    }
    // 四次高斯模糊, 3 张 DOG 图
    assert(sigma_list.size() == 3);

    // 把 source 转变成 double 类型
    cv::Mat source_double;
    source.convertTo(source_double, CV_64FC1);

    // 根据 sigma_list 的方差分别做高斯模糊
    cv::Mat gaussi_down_1, gaussi_down_2, gaussi_mid_1, gaussi_mid_2, gaussi_up_1, gaussi_up_2;
    cv::GaussianBlur(source_double, gaussi_down_1, cv::Size(5, 5), sigma_list[0].first, sigma_list[0].first);
    cv::GaussianBlur(source_double, gaussi_down_2, cv::Size(5, 5), sigma_list[0].second, sigma_list[0].second);
    cv::GaussianBlur(source_double, gaussi_mid_1, cv::Size(5, 5), sigma_list[1].first, sigma_list[1].first);
    cv::GaussianBlur(source_double, gaussi_mid_2, cv::Size(5, 5), sigma_list[1].second, sigma_list[1].second);
    cv::GaussianBlur(source_double, gaussi_up_1, cv::Size(5, 5), sigma_list[2].first, sigma_list[2].first);
    cv::GaussianBlur(source_double, gaussi_up_2, cv::Size(5, 5), sigma_list[2].second, sigma_list[2].second);

    // 分别求 DOG
    cv::Mat DOG_up, DOG_mid, DOG_down;
    DOG_down = gaussi_down_1 - gaussi_down_2;
    DOG_mid = gaussi_mid_1 - gaussi_mid_2;
    DOG_up = gaussi_up_1 - gaussi_up_2;

    // 准备一个结果
    keypoints_type max_points;
    keypoints_type min_points;

    // 三层之间找极值
    const int H = source.rows;
    const int W = source.cols;
    for(int i = 1;i < H - 1; ++i) {
        for(int j = 1;j < W - 1; ++j) {
            // 中间这个点的值, 和最近的 26 个点比较大小
            const auto center = DOG_mid.at<double>(i, j);
            // 是否最大值
            if(center > DOG_mid.at<double>(i, j - 1) and center > DOG_mid.at<double>(i, j + 1) and
               center > DOG_mid.at<double>(i - 1, j - 1) and center > DOG_mid.at<double>(i - 1, j) and center > DOG_mid.at<double>(i - 1, j + 1) and
               center > DOG_mid.at<double>(i + 1, j - 1) and center > DOG_mid.at<double>(i + 1, j) and center > DOG_mid.at<double>(i + 1, j + 1) and
               center > DOG_down.at<double>(i, j - 1) and center > DOG_down.at<double>(i, j) and center > DOG_down.at<double>(i, j + 1) and
               center > DOG_down.at<double>(i - 1, j - 1) and center > DOG_down.at<double>(i - 1, j) and center > DOG_down.at<double>(i - 1, j + 1) and
               center > DOG_down.at<double>(i + 1, j - 1) and center > DOG_down.at<double>(i + 1, j) and center > DOG_down.at<double>(i + 1, j + 1) and
               center > DOG_up.at<double>(i, j - 1) and center > DOG_up.at<double>(i, j) and center > DOG_up.at<double>(i, j + 1) and
               center > DOG_up.at<double>(i - 1, j - 1) and center > DOG_up.at<double>(i - 1, j) and center > DOG_up.at<double>(i - 1, j + 1) and
               center > DOG_up.at<double>(i + 1, j - 1) and center > DOG_up.at<double>(i + 1, j) and center > DOG_up.at<double>(i + 1, j + 1)) {
                if(center > 4)
                    max_points.emplace_back(i, j);
            }
            // 是否极小值
            if(center < DOG_mid.at<double>(i, j - 1) and center < DOG_mid.at<double>(i, j + 1) and
               center < DOG_mid.at<double>(i - 1, j - 1) and center < DOG_mid.at<double>(i - 1, j) and center < DOG_mid.at<double>(i - 1, j + 1) and
               center < DOG_mid.at<double>(i + 1, j - 1) and center < DOG_mid.at<double>(i + 1, j) and center < DOG_mid.at<double>(i + 1, j + 1) and
               center < DOG_down.at<double>(i, j - 1) and center < DOG_down.at<double>(i, j) and center < DOG_down.at<double>(i, j + 1) and
               center < DOG_down.at<double>(i - 1, j - 1) and center < DOG_down.at<double>(i - 1, j) and center < DOG_down.at<double>(i - 1, j + 1) and
               center < DOG_down.at<double>(i + 1, j - 1) and center < DOG_down.at<double>(i + 1, j) and center < DOG_down.at<double>(i + 1, j + 1) and
               center < DOG_up.at<double>(i, j - 1) and center < DOG_up.at<double>(i, j) and center < DOG_up.at<double>(i, j + 1) and
               center < DOG_up.at<double>(i - 1, j - 1) and center < DOG_up.at<double>(i - 1, j) and center < DOG_up.at<double>(i - 1, j + 1) and
               center < DOG_up.at<double>(i + 1, j - 1) and center < DOG_up.at<double>(i + 1, j) and center < DOG_up.at<double>(i + 1, j + 1)) {
                if(center < -4)
                    min_points.emplace_back(i, j);
            }
        }
    }
    return std::make_pair(max_points, min_points);
}









