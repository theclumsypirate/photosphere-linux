#include "core/tourist_filter.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/photo.hpp>

#include <algorithm>
#include <vector>

namespace ps::core {
namespace {

const char* methodName(TouristMethod method) {
    switch (method) {
        case TouristMethod::Cut: return "CUT";
        case TouristMethod::CutAndMedian: return "CUT_AND_MEDIAN";
        case TouristMethod::Median: return "MEDIAN";
        case TouristMethod::NoiseAndMedian: return "NOISE_AND_MEDIAN";
        case TouristMethod::InpaintAndMedian: return "INPAINT_AND_MEDIAN";
    }
    return "MEDIAN";
}

std::vector<cv::Rect> detectPeople(const cv::Mat& image) {
    cv::HOGDescriptor hog;
    hog.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    const double scale =
        std::min(1.0, 800.0 / static_cast<double>(std::max(image.cols,
                                                          image.rows)));
    cv::Mat small;
    cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);

    std::vector<cv::Rect> found;
    hog.detectMultiScale(small, found, 0.0, cv::Size(8, 8), cv::Size(32, 32),
                         1.05, 2, false);

    std::vector<cv::Rect> boxes;
    for (const cv::Rect& r : found) {
        cv::Rect big(cvRound(r.x / scale), cvRound(r.y / scale),
                     cvRound(r.width / scale), cvRound(r.height / scale));
        big &= cv::Rect(0, 0, image.cols, image.rows);
        if (!big.empty()) boxes.push_back(big);
    }
    return boxes;
}

cv::Rect enlarge(const cv::Rect& box, const TouristOptions& options,
                 const cv::Size& imageSize) {
    const int w = static_cast<int>(box.width * options.upscaleX);
    const int h = static_cast<int>(box.height * options.upscaleY);
    const int x = box.x - (w - box.width) / 2;
    const int y = box.y - (h - box.height) / 2;
    cv::Rect r(x, y, std::max(1, w), std::max(1, h));
    return r & cv::Rect(0, 0, imageSize.width, imageSize.height);
}

cv::Mat medianStack(const std::vector<cv::Mat>& stack) {
    if (stack.empty()) return {};
    const cv::Size size = stack.front().size();
    const int channels = stack.front().channels();
    cv::Mat result(size, CV_8UC(channels));
    const size_t n = stack.size();
    std::vector<unsigned char> values(n);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            for (int c = 0; c < channels; ++c) {
                for (size_t i = 0; i < n; ++i)
                    values[i] = stack[i].ptr<unsigned char>(y)[x * channels + c];
                const size_t mid = n / 2;
                std::nth_element(values.begin(), values.begin() + mid,
                                 values.end());
                if (n % 2 == 0) {
                    const unsigned char a = values[mid - 1];
                    const unsigned char b = values[mid];
                    result.ptr<unsigned char>(y)[x * channels + c] =
                        static_cast<unsigned char>((a + b) / 2);
                } else {
                    result.ptr<unsigned char>(y)[x * channels + c] = values[mid];
                }
            }
        }
    }
    return result;
}

cv::Mat composite(const cv::Mat& background, const cv::Mat& foreground,
                  const cv::Mat& mask) {
    cv::Mat alpha;
    mask.convertTo(alpha, CV_32F, 1.0 / 255.0);
    cv::Mat alpha3;
    cv::merge(std::vector<cv::Mat>{alpha, alpha, alpha}, alpha3);
    cv::Mat inverse = cv::Scalar::all(1.0) - alpha3;
    cv::Mat bgF, fgF, out;
    background.convertTo(bgF, CV_32F);
    foreground.convertTo(fgF, CV_32F);
    cv::multiply(bgF, inverse, out);
    cv::Mat tmp;
    cv::multiply(fgF, alpha3, tmp);
    out += tmp;
    cv::Mat result;
    out.convertTo(result, CV_8U);
    return result;
}

} // namespace

bool removeTourists(
    const std::string& folder, const TouristOptions& options,
    std::string& outputPath, std::string& error,
    const std::function<void(int, int, const std::string&)>& progress) {
    std::vector<cv::String> files;
    cv::glob(folder + "/*.jpg", files, false);
    if (files.size() < 2) {
        error = "Not enough JPG photos in the folder (need at least two).";
        return false;
    }
    std::sort(files.begin(), files.end());

    std::vector<cv::Mat> stack;
    cv::Mat last;
    const int total = static_cast<int>(files.size());
    for (int i = 0; i < total; ++i) {
        cv::Mat image = cv::imread(files[i], cv::IMREAD_COLOR);
        if (image.empty()) continue;
        if (progress)
            progress(i + 1, total, "Processing image " + std::to_string(i + 1) +
                                        " of " + std::to_string(total));

        if (options.method == TouristMethod::Median) {
            stack.push_back(image.clone());
            last = image;
            continue;
        }

        const auto boxes = detectPeople(image);
        cv::Mat mask(image.size(), CV_8UC1, cv::Scalar(255));
        for (const auto& box : boxes)
            cv::rectangle(mask, enlarge(box, options, image.size()),
                          cv::Scalar(0), cv::FILLED);

        switch (options.method) {
            case TouristMethod::Cut:
            case TouristMethod::CutAndMedian: {
                cv::Mat marked = image.clone();
                for (const auto& box : boxes)
                    cv::rectangle(marked, enlarge(box, options, image.size()),
                                  cv::Scalar(0, 255, 0), 6);
                last = composite(image, marked, mask);
                stack.push_back(last.clone());
                break;
            }
            case TouristMethod::InpaintAndMedian: {
                cv::Mat cleared = image.clone();
                for (const auto& box : boxes)
                    cv::rectangle(cleared, enlarge(box, options, image.size()),
                                  cv::Scalar(255, 255, 255), cv::FILLED);
                cv::Mat inverse;
                cv::bitwise_not(mask, inverse);
                cv::Mat filtered;
                cv::inpaint(cleared, inverse, filtered, 3, cv::INPAINT_NS);
                last = filtered;
                stack.push_back(filtered.clone());
                break;
            }
            case TouristMethod::NoiseAndMedian: {
                cv::Mat normalized;
                cv::normalize(image, normalized, 1, 254, cv::NORM_MINMAX);
                for (const auto& box : boxes) {
                    const cv::Rect r = enlarge(box, options, image.size());
                    cv::Mat noise(r.height, r.width, CV_8UC1);
                    cv::randu(noise, cv::Scalar(0), cv::Scalar(2));
                    noise *= 255;
                    cv::Mat color;
                    cv::cvtColor(noise, color, cv::COLOR_GRAY2BGR);
                    color.copyTo(normalized(r));
                }
                last = normalized;
                stack.push_back(normalized.clone());
                break;
            }
            case TouristMethod::Median:
                break;
        }
    }

    if (stack.empty()) {
        error = "No image could be decoded.";
        return false;
    }
    if (progress)
        progress(total, total, "Creating median image from " +
                                   std::to_string(stack.size()) + " images");

    cv::Mat result;
    if (options.method == TouristMethod::NoiseAndMedian) {
        if (stack.size() % 2 == 0) stack.push_back(stack.back());
        result = medianStack(stack);
        cv::Mat black, white, mask;
        cv::threshold(result, black, 0, 1, cv::THRESH_BINARY);
        cv::bitwise_not(black, black);
        black *= 255;
        cv::threshold(result, white, 254, 255, cv::THRESH_BINARY);
        cv::add(white, black, mask);
        cv::Mat single, cleaned;
        cv::extractChannel(mask, single, 0);
        cv::inpaint(result, single, cleaned, 10, cv::INPAINT_TELEA);
        result = cleaned;
    } else if (options.method == TouristMethod::Cut) {
        result = last;
    } else {
        result = medianStack(stack);
    }

    if (result.empty()) {
        error = "Failed to compose the result.";
        return false;
    }
    outputPath = folder + "_" + methodName(options.method) + "_native.jpg";
    if (!cv::imwrite(outputPath, result)) {
        error = "Cannot write " + outputPath;
        return false;
    }
    if (progress) progress(total, total, "Saved " + outputPath);
    return true;
}

} // namespace ps::core
