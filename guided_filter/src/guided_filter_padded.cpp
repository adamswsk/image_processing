// C++
#include <vector>
// self
#include "guided_filter.h"


namespace {

	void cv_show(const cv::Mat& one_image, const char* info="") {
		cv::imshow(info, one_image);
		cv::waitKey(0);
		cv::destroyAllWindows();
	}

	cv::Mat make_pad(const cv::Mat& one_image, const int pad_H, const int pad_W) {
		cv::Mat padded_image;
		cv::copyMakeBorder(one_image, padded_image, pad_H, pad_H, pad_W, pad_W, cv::BORDER_REPLICATE);
		return padded_image;
	}

	void cv_show_double(const double* const one_image, const int H, const int W, const char* info="") {
		cv::Mat src(H, W,CV_8UC1);
		for(int i = 0;i < H; ++i) {
			const double* const row_ptr = one_image + i * W;
			for(int j = 0;j < W; ++j)
				src.at<uchar>(i, j) = cv::saturate_cast<uchar>(row_ptr[j] * 255);
		}
		cv_show(src);
	}

    // 均值滤波
    std::vector<double> box_filter(const double* const new_source, const int radius_h, const int radius_w, const int H, const int W) {
        // 先对图像做 padding
        const int new_H = H + 2 * radius_h;
        const int new_W = W + 2 * radius_w;
        std::vector<double> padding_image(new_H * new_W, 0);
        double* const padding_ptr = padding_image.data();
        // 先把已有内容填上
        for(int i = 0;i < H; ++i) {
            double* const row_ptr = padding_ptr + (radius_h + i) * new_W + radius_w;
            const double* const src_row_ptr = new_source + i * W;
            std::memcpy(row_ptr, src_row_ptr, sizeof(double) * W);
        }
        // 填充上面的边界
        for(int i = 0;i < radius_h; ++i) {
            std::memcpy(padding_ptr + (radius_h - 1 - i) * new_W + radius_w, new_source + i * W, sizeof(double) * W);
            std::memcpy(padding_ptr + (new_H - radius_h + i) * new_W + radius_w, new_source + (H - i - 1) * W, sizeof(double) * W);
        }
        // 填充左右两边的边界, 这次没法 memcpy 了, 内存不是连续的
        for(int j = 0;j < radius_w; ++j) {
            double* const _beg = padding_ptr + radius_h * new_W + radius_w - 1 - j;
            for(int i = 0;i < H; ++i)
                _beg[i * new_W] = new_source[i * W + j];
        }
        for(int j = 0;j < radius_w; ++j) {
            double* const _beg = padding_ptr + radius_h * new_W + radius_w + W + j;
            for(int i = 0;i < H; ++i)
                _beg[i * new_W] = new_source[i * W + W - 1 - j];
        }
        // 填充四个角, 以后有时间再说 cv_show_double(padding_image.data(), new_H, new_W);
        // 复杂度分析, 彩色图的 guided_filter

        // 现在开始 box_filter, 注意有边界
        // 现在图像的高和宽分别是 new_H, new_W, 草稿画一下图就知道
        const int kernel_h = (radius_h << 1) + 1;
        const int kernel_w = (radius_w << 1) + 1;
        // 准备 buffer 和每一个点代表的 box 之和
        std::vector<double> buffer(new_W, 0.0);
        std::vector<double> sum(H * W, 0.0);
        double* const sum_ptr = sum.data();
        // 首先求目标(结果的)第一行的 buffer
        for(int i = 0;i < kernel_h; ++i) {
            const double* const row_ptr = padding_ptr + i * new_W;
            for(int j = 0;j < new_W; ++j) buffer[j] += row_ptr[j];
        }
        // 求每一行的每个点的 box 的和
        for(int i = 0;i < H; ++i) {
            // 当前 kernel_w 个 buffer 点的累加值
            double cur_sum = 0;
            // 这一行第一个 box 的 cur_sum, 前 kernel_w 个 buffer 点的累加值
            for(int j = 0;j < kernel_w; ++j) cur_sum += buffer[j];
            // 记录这第一个 box 的值
            const int _beg = i * W;
            sum_ptr[_beg] = cur_sum;
            // 向右边挪动, 减去最左边的值, 加上最右边要加进来的值
            for(int j = 1;j < W; ++j) {
                cur_sum = cur_sum - buffer[j - 1] + buffer[j - 1 + kernel_w];
                sum_ptr[_beg + j] = cur_sum;
            }
            // 这一行的点的 sum 都记下来了, 准备换行, 更新 buffer ==> 减去最上面的值, 加上新一行对应的值
            // 最后一次不需要更新......
            if(i != H - 1) {
                const double* const up_ptr = padding_ptr + i * new_W;
                const double* const down_ptr = padding_ptr + (i + kernel_h) * new_W;
                for(int j = 0;j < new_W; ++j) buffer[j] = buffer[j] - up_ptr[j] + down_ptr[j];
            }
        }
        // sum 其实就是最后的矩阵, 现在要除以 area, 每个 box 的面积
        const int area = kernel_h * kernel_w;
        const int length = H * W;
        for(int i = 0;i < length; ++i)
            sum_ptr[i] /= area;
		return sum;
	}

}

// 我可以先把它 padding, 之后在 Rect
cv::Mat guided_filter_channel_padded(const cv::Mat& noise_image, const cv::Mat& guide_image, const int radius_h, const int radius_w, const double eta) {
    const int H = noise_image.rows;
	const int W = noise_image.cols;
	const int length = H * W;
	// 准备一些 double 数组存储中间结果
	std::vector<double> noise_double_image(H * W, 0);
	std::vector<double> guide_double_image(H * W, 0);
	std::vector<double> I_P(H * W, 0);
	std::vector<double> I_I(H * W, 0);
	std::vector<double> cov_IP(H * W, 0);
	std::vector<double> var_I(H * W, 0);
	std::vector<double> a(H * W, 0);
	std::vector<double> b(H * W, 0);
	// 将输入图片和引导图都除以 255
	const uchar* const noise_ptr = noise_image.data;
	const uchar* const guide_ptr = guide_image.data;
	for(int i = 0;i < length; ++i) noise_double_image[i] = (double)noise_ptr[i] / 255;
	for(int i = 0;i < length; ++i) guide_double_image[i] = (double)guide_ptr[i] / 255;
	// mean(P) 和 mean(I)
	const auto mean_P = box_filter(noise_double_image.data(), radius_h, radius_w, H, W);
	const auto mean_I = box_filter(guide_double_image.data(), radius_h, radius_w, H, W);
	// mean(P * I)
	for(int i = 0;i < length; ++i) I_P[i] = noise_double_image[i] * guide_double_image[i];
	const auto mean_IP = box_filter(I_P.data(), radius_h, radius_w, H, W);
	// mean(I * I)
	for(int i = 0;i < length; ++i) I_I[i] = guide_double_image[i] * guide_double_image[i];
	const auto mean_II = box_filter(I_I.data(), radius_h, radius_w, H, W);
	// 准备求 a 的分子 cov_IP  跟分母 var_I
	for(int i = 0;i < length; ++i) cov_IP[i] = mean_IP[i] - mean_I[i] * mean_P[i];
	for(int i = 0;i < length; ++i) var_I[i] = mean_II[i] - mean_I[i] * mean_I[i];
	// a = cov(I, P) / (var(I) + eta)
	for(int i = 0;i < length; ++i) a[i] = cov_IP[i] / (var_I[i] + eta);
	// b = mean(P) - a * mean(I)
	for(int i = 0;i < length; ++i) b[i] = mean_P[i] - a[i] * mean_I[i];
	// mean(a) 和 mean(b), 因为一个 q 点可能存在于多个窗口内, 多个窗口内都有 q 的一个值
	const auto mean_a = box_filter(a.data(), radius_h, radius_w, H, W);
	const auto mean_b = box_filter(b.data(), radius_h, radius_w, H, W);
	// q = a * I + b
	cv::Mat q = noise_image.clone();
	for(int i = 0;i < length; ++i) q.data[i] = cv::saturate_cast<uchar>(255 * (mean_a[i] * noise_double_image[i] + mean_b[i]));
	// 截取
	return q;
}

