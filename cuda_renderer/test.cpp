#include "renderer.h"
#include <chrono>

#include <cuda.h>
#include <cuda_runtime.h>
#include <driver_functions.h>
using namespace cv;

static std::string prefix = "/home/meiqua/patch_linemod/public/datasets/hinterstoisser/";

namespace helper {
cv::Mat view_dep(cv::Mat dep){
    cv::Mat map = dep;
    double min;
    double max;
    cv::minMaxIdx(map, &min, &max);
    cv::Mat adjMap;
    map.convertTo(adjMap,CV_8UC1, 255 / (max-min), -min);
    cv::Mat falseColorsMap;
    applyColorMap(adjMap, falseColorsMap, cv::COLORMAP_HOT);
    return falseColorsMap;
};

class Timer
{
public:
    Timer() : beg_(clock_::now()) {}
    void reset() { beg_ = clock_::now(); }
    double elapsed() const {
        return std::chrono::duration_cast<second_>
            (clock_::now() - beg_).count(); }
    void out(std::string message = ""){
        double t = elapsed();
        std::cout << message << "\nelasped time:" << t << "s\n" << std::endl;
        reset();
    }
private:
    typedef std::chrono::high_resolution_clock clock_;
    typedef std::chrono::duration<double, std::ratio<1> > second_;
    std::chrono::time_point<clock_> beg_;
};
}

int main(int argc, char const *argv[])
{
    const int width = 640; const int height = 480;

    cuda_renderer::Model model(prefix+"models/obj_06.ply");

    Mat K = (Mat_<float>(3,3) << 572.4114, 0.0, 325.2611, 0.0, 573.57043, 242.04899, 0.0, 0.0, 1.0);
    auto proj = cuda_renderer::compute_proj(K, width, height);

    Mat R_ren = (Mat_<float>(3,3) << 0.34768538, 0.93761126, 0.00000000, 0.70540612,
                 -0.26157897, -0.65877056, -0.61767070, 0.22904489, -0.75234390);
    Mat t_ren = (Mat_<float>(3,1) << 0.0, 0.0, 300.0);

    cuda_renderer::Model::mat4x4 mat4;
    mat4.init_from_cv(R_ren, t_ren);

    std::vector<cuda_renderer::Model::mat4x4> mat4_v(100, mat4);
    std::cout << "test render nums: " << mat4_v.size() << std::endl;
    std::cout << "---------------------------------\n" << std::endl;

    {  // gpu need sometime to warm up
        cudaFree(0);
//        cudaSetDevice(0);
    }
    helper::Timer timer;

    std::vector<int> result_cpu = cuda_renderer::render_cpu(model.tris, mat4_v, width, height, proj);
    timer.out("cpu render");

    std::vector<int> result_gpu = cuda_renderer::render_cuda(model.tris, mat4_v, width, height, proj);
    timer.out("gpu render");

    thrust::device_vector<int> result_gpu_keep_in =
            cuda_renderer::render_cuda_keep_in_gpu(model.tris, mat4_v, width, height, proj);
    timer.out("gpu_keep_in render");

    std::vector<int> depth_int32(result_gpu_keep_in.size());
    thrust::copy(result_gpu_keep_in.begin(), result_gpu_keep_in.end(), depth_int32.begin());
    timer.out("gpu_keep_in back to host");

    std::vector<int> result_diff(result_cpu.size());
    for(size_t i=0; i<result_cpu.size(); i++){
        result_diff[i] = std::abs(result_cpu[i] - result_gpu[i]);
    }
    assert(std::accumulate(result_diff.begin(), result_diff.end(), 0) == 0 &&
           "rendering results, cpu should be same as gpu");

    // just show first 1
    cv::Mat depth = cv::Mat(height, width, CV_32SC1, result_cpu.data());

    cv::imshow("gpu_mask", depth>0);
    cv::imshow("gpu_depth", helper::view_dep(depth));
    cv::waitKey(0);

    return 0;
}
