// C++
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>
#include <functional>
// OpenCV
#include <opencv2/opencv.hpp>



namespace {
    void run(const std::function<void()>& work=[]{}, const std::string message="") {
        auto start = std::chrono::steady_clock::now();
        work();
        auto finish = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
        std::cout << message << " " << duration.count() << " ms" <<  std::endl;
    }

    void cv_show(const cv::Mat& one_image, const char* info="") {
        cv::imshow(info, one_image);
        cv::waitKey(0);
        cv::destroyAllWindows();
    }

    bool cv_write(const cv::Mat& source, const std::string save_path) {
        return cv::imwrite(save_path, source, std::vector<int>({cv::IMWRITE_PNG_COMPRESSION, 0}));
    }

    void cv_info(const cv::Mat& one_image) {
        std::cout << "高  :  " << one_image.rows << "\n宽  :  " << one_image.cols << "\n通道 :  " << one_image.channels() << std::endl;
        std::cout << "步长 :  " << one_image.step << std::endl;
        std::cout << "是否连续" << std::boolalpha << one_image.isContinuous() << std::endl;
    }


    cv::Mat cv_concat(const std::vector<cv::Mat> images, const bool v=false) {
        cv::Mat result;
        if(not v) cv::hconcat(images, result);
        else cv::vconcat(images, result);
        return result;
    }

    cv::Mat make_pad(const cv::Mat& one_image, const int pad_H, const int pad_W) {
        cv::Mat padded_image;
        cv::copyMakeBorder(one_image, padded_image, pad_H, pad_H, pad_W, pad_W, cv::BORDER_REPLICATE);
        return padded_image;
    }

    template<typename T>
    cv::Mat toint8(const std::vector<T>& source, const int H, const int W, const int C, const int _type, const double times=2) {
        cv::Mat result(H, W, _type);
        const int length = H * W * C;
        for(int i = 0;i < length; ++i) result.data[i] = cv::saturate_cast<uchar>(std::abs(source[i]) * times);
        return result;
    }
}







namespace {
    inline double fast_exp(const double y) {
        double d;
        *(reinterpret_cast<int*>(&d) + 0) = 0;
        *(reinterpret_cast<int*>(&d) + 1) = static_cast<int>(1512775 * y + 1072632447);
        return d;
    }

    inline double square(const double x) {
        return x * x;
    }

    std::vector<float> fast_gaussi_blur(
            const std::vector<float>& source,
            const int H,
            const int W,
            const int radius=2,
            const float sigma=0.83) {
        // 根据半径和方差构造一个高斯模板
        const int filter_size = 2 * radius + 1;
        std::vector<float> filter(filter_size, 0);
        float cur_sum = 0;
        for(int i = -radius; i <= radius; ++i) {
            filter[i + radius] = 1.f / sigma * std::exp(-float(i * i) / (2 * sigma * sigma));
            cur_sum += filter[i + radius];
        }
        for(int i = 0;i < filter_size; ++i) filter[i] = filter[i] / cur_sum;
        // 先做 x 方向的
        std::vector<float> temp(H * W);
        // std::memcpy(temp.data(), source.data(), sizeof(float) * H * W);
        std::copy(source.begin(), source.end(), temp.begin());
        for(int i = 0; i < H; ++i) {
            const float* const row_ptr = source.data() + i * W;
            float* const res_ptr = temp.data() + i * W;
            for(int j = radius; j < W - radius; ++j) {
                float intensity_sum = 0.0;
                for(int k = -radius; k <= radius; ++k)
                    intensity_sum += filter[k + radius] * row_ptr[j + k];
                res_ptr[j] = intensity_sum;
            }
        }
        // 再做 y 方向的
        std::vector<float> blurred(H * W);
        std::copy(source.begin(), source.end(), blurred.begin());
        for(int i = radius; i < H - radius; ++i) {
            for(int j = radius; j < W - radius; ++j) {
                float intensity_sum = 0.0;
                for(int k = -radius; k <= radius; ++k)
                    intensity_sum += filter[k + radius] * temp[(i + k) * W + j];
                blurred[i * W + j] = intensity_sum;
            }
        }
        return blurred;
    }
}


cv::Mat exposure_fusion(
        const std::vector<cv::Mat>& images_list,
        const std::tuple<float, float, float>& alphas={1.0, 1.0, 1.0},
        const float best_illumination=0.5,
        const double sigma=0.2) {
    const double sigma_inv = 1. / (2 * sigma * sigma);
    const float norm = 1.f / 255;
    // 获取图像信息
    const int H = images_list.front().rows;
    const int W = images_list.front().cols;
    const int C = images_list.front().channels();
    assert(C == 3 and "该算法只支持 RGB24 图像 !");
    for(const auto& image : images_list)
        assert(image.rows == H and image.cols == W and image.channels() == C and "图像序列的大小必须都一致!");
    const int length = H * W;
    // 看有几张图像
    const int sequence_len = images_list.size();
    // 根据先验知识, 计算对比度、饱和度、亮度, 求权重
    std::vector< std::vector<float> > weights;
    for(const auto& image : images_list) {
        // 【1】根据对比度求权重
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        const auto gray_padded = make_pad(gray, 1, 1);
        std::vector<float> contrast(length, 0);
        const int W_2 = W + 2;
        for(int i = 1; i < H + 1; ++i) {
            const uchar* const row_ptr = gray_padded.data + i * W_2;
            float* const res_ptr = contrast.data() + (i - 1) * W;
            for(int j = 1; j < W + 1; ++j)
                res_ptr[j - 1] = norm * std::abs(row_ptr[j - 1] + row_ptr[j + 1] + row_ptr[j - W_2] + row_ptr[j + W_2] - 4 * row_ptr[j]);
        }
        // 【2】根据饱和度求权重
        std::vector<float> saturation(length, 0);
        for(int i = 0; i < H; ++i) {
            const uchar* const row_ptr = image.data + i * W * 3;
            float* const res_ptr = saturation.data() + i * W;
            for(int j = 0; j < W; ++j) {
                const int p = 3 * j;
                const float mean = (row_ptr[p] + row_ptr[p + 1] + row_ptr[p + 2]) * norm / 3;
                res_ptr[j] = std::sqrt((square(row_ptr[p] * norm - mean) + square(row_ptr[p + 1] * norm - mean) + square(row_ptr[p + 2] * norm - mean)) * 1.f / 3);
            }
        }
        // 【3】根据亮度求权重
        std::vector<float> illumination(length, 0);
        for(int i = 0; i < H; ++i) {
            const uchar* const row_ptr = image.data + i * W * 3;
            float* const res_ptr = illumination.data() + i * W;
            for(int j = 0; j < W; ++j) {
                const int p = 3 * j;
                res_ptr[j] = std::exp(-square(row_ptr[p] * norm - best_illumination) * sigma_inv)
                           * std::exp(-square(row_ptr[p + 1] * norm - best_illumination) * sigma_inv)
                           * std::exp(-square(row_ptr[p + 2] * norm - best_illumination) * sigma_inv);
            }
        }
//        cv_show(image);
//        cv_show(cv_concat({
//            toint8(contrast, H, W, 1, CV_8UC1, 255.0),
//            toint8(saturation, H, W, 1, CV_8UC1, 255.0),
//            toint8(illumination, H, W, 1, CV_8UC1, 255.0)
//        }));
        // 三者结合, 求权重
        std::vector<float> cur_weight(length, 0);
        for(int i = 0; i < length; ++i)
            cur_weight[i] = (std::pow(contrast[i], std::get<0>(alphas)) + 1e-3) *
                            (std::pow(saturation[i], std::get<1>(alphas)) + 1e-3) *
                            (std::pow(illumination[i], std::get<2>(alphas)) + 1e-4);
        weights.emplace_back(cur_weight);
    }
    // 求归一化的权重
    for(int i = 0;i < length; ++i) {
        float weight_sum = 0.0;
        for(int k = 0;k < sequence_len; ++k) weight_sum += weights[k][i];
        for(int k = 0;k < sequence_len; ++k) weights[k][i] /= weight_sum;
    }
    // for(int k = 0;k < sequence_len; ++k) cv_show(toint8(weights[k], H, W, 1, CV_8UC1, 255));
    // 准备融合
    cv::Mat fused = cv::Mat::zeros(H, W, images_list.front().type());
    uchar* fused_ptr = fused.data;
    // 粗糙的融合
    if(true) {
        // 每张图像对应一个权重图
        for(int k = 0;k < sequence_len; ++k) {
            // 对权重图做高斯模糊
            // const auto w = fast_gaussi_blur(weights[k], H, W, 2, 0.83);
            const uchar* const cur_image = images_list[k].data;
            const auto& w = weights[k];
            for(int i = 0;i < length; ++i) {
                const int p = 3 * i;
                fused_ptr[p] += w[i] * cur_image[p];
                fused_ptr[p + 1] += w[i] * cur_image[p + 1];
                fused_ptr[p + 2] += w[i] * cur_image[p + 2];
            }
        }
        const int total_length = 3 * length;
        for(int i = 0;i < total_length; ++i)
            fused_ptr[i] = cv::saturate_cast<uchar>(fused_ptr[i]);
    }
    else {
    }
    return fused;
}


 #include <ghc/filesystem.hpp>


int main() {

    // 获取图像列表
    std::vector<cv::Mat> images_list;
    const std::string sequence_dir("./images/input/2/");
    auto sequence_list = ghc::filesystem::directory_iterator(sequence_dir);
    for(const auto& it : sequence_list) {
        cv::Mat current = cv::imread(sequence_dir + it.path().filename().string());
        images_list.emplace_back(current);
    }
//    std::vector<std::string> images_path({"venice_under.png", "venice_normal.png", "venice_over.png"});
//    for(const auto& it : images_path)
//        images_list.emplace_back(cv::imread(sequence_dir + it));
    const auto fusion_result = exposure_fusion(images_list, {1.0, 1.0, 1.0}, 0.5);
    cv_show(fusion_result);
    cv_write(fusion_result, "./images/output/2/fusion_result_failed.png");
    return 0;
}
