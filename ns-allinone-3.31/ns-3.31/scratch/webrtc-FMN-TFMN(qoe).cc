#include <iostream>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <set>
#include <unistd.h>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <numeric>    // 添加：用于std::accumulate
#include <iomanip>    // 添加：用于std::setprecision, std::fixed

using namespace std;

// 检查文件是否为常规文件（非目录）
bool IsRegularFile(const std::string& path) {
    struct stat path_stat;
    if (stat(path.c_str(), &path_stat) == 0) {
        return S_ISREG(path_stat.st_mode);
    }
    return false;
}

// 提取文件名中的数字用于排序
int extractNumber(const std::string& filename) {
    size_t pos = filename.find_last_not_of("0123456789");
    std::string num_str = (pos == std::string::npos) ? filename : filename.substr(pos + 1);
    
    if (num_str.empty()) return 0;
    try {
        return std::stoi(num_str);
    } catch (const std::invalid_argument&) {
        return 0;
    } catch (const std::out_of_range&) {
        return 0;
    }
}

// 获取目录下所有文件（不限制扩展名）
std::vector<std::string> GetAllTraceFiles(const std::string& directory) {
    std::vector<std::string> trace_files;
    DIR* dir;
    struct dirent* entry;
    
    std::cout << "Scanning directory: " << directory << " for all trace files" << std::endl;
    
    if ((dir = opendir(directory.c_str())) != nullptr) {
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            
            // 跳过 "." 和 ".."
            if (filename == "." || filename == "!!") {
                continue;
            }
            
            std::string full_path = directory + "/" + filename;
            
            // 只添加常规文件
            if (IsRegularFile(full_path)) {
                trace_files.push_back(full_path);
                std::cout << "Found file: " << full_path << std::endl;
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Cannot open directory: " << directory << std::endl;
    }
    
    // 按数字排序
    std::sort(trace_files.begin(), trace_files.end(),
        [](const std::string& a, const std::string& b) {
            return extractNumber(a) < extractNumber(b);
        });
    
    std::cout << "Total files found: " << trace_files.size() << std::endl;
    return trace_files;
}

// 获取目录下指定扩展名的文件
std::vector<std::string> GetTraceFilesByExtension(const std::string& directory, const std::vector<std::string>& extensions) {
    std::vector<std::string> trace_files;
    DIR* dir;
    struct dirent* entry;
    
    std::cout << "Scanning directory: " << directory << " for extensions: ";
    for (const auto& ext : extensions) {
        std::cout << ext << " ";
    }
    std::cout << std::endl;
    
    if ((dir = opendir(directory.c_str())) != nullptr) {
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            
            // 跳过 "." 和 ".."
            if (filename == "." || filename == "!!") {
                continue;
            }
            
            // 检查文件扩展名
            for (const auto& extension : extensions) {
                if (filename.length() >= extension.length() && 
                    filename.compare(filename.length() - extension.length(), extension.length(), extension) == 0) {
                    
                    std::string full_path = directory + "/" + filename;
                    if (IsRegularFile(full_path)) {
                        trace_files.push_back(full_path);
                        std::cout << "Found trace file: " << full_path << std::endl;
                    }
                    break;
                }
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Cannot open directory: " << directory << std::endl;
    }
    
    std::sort(trace_files.begin(), trace_files.end());
    std::cout << "Total trace files found: " << trace_files.size() << std::endl;
    return trace_files;
}

// 从文件路径中提取实例名称
std::string ExtractInstanceName(const std::string& file_path) {
    std::string base_name = file_path;
    
    // 提取文件名（去掉路径）
    size_t last_slash = base_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        base_name = base_name.substr(last_slash + 1);
    }
    
    // 去掉文件扩展名
    size_t last_dot = base_name.find_last_of(".");
    if (last_dot != std::string::npos) {
        base_name = base_name.substr(0, last_dot);
    }
    
    return base_name;
}

// RL状态分析器类 - 增强版，专注于自适应mu分析
class AdaptiveMuAnalyzer {
public:
    AdaptiveMuAnalyzer() {
        std::cout << "AdaptiveMuAnalyzer initialized" << std::endl;
    }
    
    // 分析自适应mu的性能
    void AnalyzeAdaptiveMuPerformance(const std::string& frame_qoe_file) {
        std::ifstream file(frame_qoe_file.c_str());
        if (!file.is_open()) {
            std::cerr << "Cannot open Frame QoE file for analysis: " << frame_qoe_file << std::endl;
            return;
        }
        
        std::string line;
        // 跳过注释行，直到找到表头
        while (std::getline(file, line)) {
            if (line.find("frame_id,U_F(i),D_F(i),loss_F(i),QoE_i,mu_used") != std::string::npos) {
                break;
            }
        }
        
        int total_frames = 0;
        double total_qoe = 0.0;
        double total_bandwidth_util = 0.0;
        double total_frame_delay = 0.0;
        double total_frame_loss = 0.0;
        std::vector<double> mu_values;
        std::vector<double> qoe_values;
        std::map<int, double> mu_by_frame;  // 记录每帧的mu值
        
        while (std::getline(file, line)) {
            // 跳过注释行和空行
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            std::istringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 6) {
                total_frames++;
                int frame_id = std::stoi(tokens[0]);
                double bandwidth_util = std::stod(tokens[1]);
                double frame_delay = std::stod(tokens[2]);
                double frame_loss = std::stod(tokens[3]);
                double qoe = std::stod(tokens[4]);
                double mu_used = std::stod(tokens[5]);
                
                total_qoe += qoe;
                total_bandwidth_util += bandwidth_util;
                total_frame_delay += frame_delay;
                total_frame_loss += frame_loss;
                mu_values.push_back(mu_used);
                qoe_values.push_back(qoe);
                mu_by_frame[frame_id] = mu_used;
            }
        }
        
        file.close();
        
        if (total_frames > 0) {
            std::cout << "=== ADAPTIVE MU PERFORMANCE ANALYSIS ===" << std::endl;
            std::cout << "Total frames analyzed: " << total_frames << std::endl;
            std::cout << "Average QoE per frame: " << (total_qoe / total_frames) << std::endl;
            std::cout << "Average bandwidth utilization (U_F): " << (total_bandwidth_util / total_frames) << std::endl;
            std::cout << "Average frame delay (D_F): " << (total_frame_delay / total_frames) << " seconds" << std::endl;
            std::cout << "Average frame loss (loss_F): " << (total_frame_loss / total_frames) << std::endl;
            
            // 分析mu值的自适应变化
            if (!mu_values.empty()) {
                double min_mu = *std::min_element(mu_values.begin(), mu_values.end());
                double max_mu = *std::max_element(mu_values.begin(), mu_values.end());
                double avg_mu = std::accumulate(mu_values.begin(), mu_values.end(), 0.0) / mu_values.size();
                
                // 计算mu变化范围
                double mu_range = max_mu - min_mu;
                double mu_std_dev = 0.0;
                for (double mu : mu_values) {
                    mu_std_dev += (mu - avg_mu) * (mu - avg_mu);
                }
                mu_std_dev = sqrt(mu_std_dev / mu_values.size());
                
                std::cout << "\nMu Value Statistics:" << std::endl;
                std::cout << "  Minimum mu: " << min_mu << std::endl;
                std::cout << "  Maximum mu: " << max_mu << std::endl;
                std::cout << "  Average mu: " << avg_mu << std::endl;
                std::cout << "  Mu range: " << mu_range << std::endl;
                std::cout << "  Standard deviation: " << mu_std_dev << std::endl;
                std::cout << "  Bounds: [0.5, 2.0]" << std::endl;
                
                // 分析mu值分布
                std::cout << "\nMu Value Distribution:" << std::endl;
                std::map<std::string, int> mu_range_count = {
                    {"0.5-0.7", 0}, {"0.7-0.9", 0}, {"0.9-1.1", 0},
                    {"1.1-1.3", 0}, {"1.3-1.5", 0}, {"1.5-2.0", 0}
                };
                
                for (double mu : mu_values) {
                    if (mu < 0.7) mu_range_count["0.5-0.7"]++;
                    else if (mu < 0.9) mu_range_count["0.7-0.9"]++;
                    else if (mu < 1.1) mu_range_count["0.9-1.1"]++;
                    else if (mu < 1.3) mu_range_count["1.1-1.3"]++;
                    else if (mu < 1.5) mu_range_count["1.3-1.5"]++;
                    else mu_range_count["1.5-2.0"]++;
                }
                
                for (const auto& range : mu_range_count) {
                    double percentage = (range.second * 100.0) / mu_values.size();
                    std::cout << "  " << range.first << ": " << range.second 
                            << " frames (" << std::fixed << std::setprecision(1) 
                            << percentage << "%)" << std::endl;
                }
                
                // 分析mu变化趋势
                std::cout << "\nMu Adaptation Trends:" << std::endl;
                if (mu_by_frame.size() > 10) {
                    // 计算前10帧和后10帧的平均mu
                    double first_10_avg = 0.0, last_10_avg = 0.0;
                    int first_count = 0, last_count = 0;
                    
                    for (int i = 0; i < 10 && i < total_frames; i++) {
                        if (mu_by_frame.find(i) != mu_by_frame.end()) {
                            first_10_avg += mu_by_frame[i];
                            first_count++;
                        }
                    }
                    
                    for (int i = total_frames - 10; i < total_frames; i++) {
                        if (mu_by_frame.find(i) != mu_by_frame.end()) {
                            last_10_avg += mu_by_frame[i];
                            last_count++;
                        }
                    }
                    
                    if (first_count > 0 && last_count > 0) {
                        first_10_avg /= first_count;
                        last_10_avg /= last_count;
                        std::cout << "  Average mu in first 10 frames: " << first_10_avg << std::endl;
                        std::cout << "  Average mu in last 10 frames: " << last_10_avg << std::endl;
                        std::cout << "  Mu change trend: " << (last_10_avg - first_10_avg) << std::endl;
                    }
                }
            }
            
            // 分析QoE与mu的相关性
            if (!mu_values.empty() && !qoe_values.empty()) {
                std::cout << "\nQoE-Mu Correlation Analysis:" << std::endl;
                
                // 找到最佳QoE对应的mu值
                double best_qoe = *std::max_element(qoe_values.begin(), qoe_values.end());
                auto best_qoe_iter = std::max_element(qoe_values.begin(), qoe_values.end());
                size_t best_qoe_index = std::distance(qoe_values.begin(), best_qoe_iter);
                double mu_at_best_qoe = mu_values[best_qoe_index];
                
                std::cout << "  Best QoE achieved: " << best_qoe 
                        << " (frame " << best_qoe_index 
                        << ", mu=" << mu_at_best_qoe << ")" << std::endl;
                
                // 计算mu-QoE的简单相关性
                double mu_qoe_cov = 0.0;
                double mu_mean = std::accumulate(mu_values.begin(), mu_values.end(), 0.0) / mu_values.size();
                double qoe_mean = std::accumulate(qoe_values.begin(), qoe_values.end(), 0.0) / qoe_values.size();
                
                for (size_t i = 0; i < mu_values.size(); i++) {
                    mu_qoe_cov += (mu_values[i] - mu_mean) * (qoe_values[i] - qoe_mean);
                }
                
                if (mu_qoe_cov > 0) {
                    std::cout << "  Positive correlation between mu and QoE" << std::endl;
                } else if (mu_qoe_cov < 0) {
                    std::cout << "  Negative correlation between mu and QoE" << std::endl;
                } else {
                    std::cout << "  No significant correlation detected" << std::endl;
                }
            }
            
            std::cout << "\nAdaptive Rules Applied:" << std::endl;
            std::cout << "  1. Compare loss(i,j) and loss(i-1,j) for same Rt group" << std::endl;
            std::cout << "  2. If Rt > max Rt in frame i-1, increase mu" << std::endl;
            std::cout << "  3. Compare QoE_i and QoE_i-1" << std::endl;
            std::cout << "\nFrame QoE Formula:" << std::endl;
            std::cout << "  QoE_i = 0.2 * U_F(i) - 0.2 * D_F(i) - 0.3 * loss_F(i)" << std::endl;
            
            std::cout << "==========================================" << std::endl;
        }
    }
    
    // 批量分析自适应mu性能
    void BatchAnalyzeAdaptiveMu(const std::vector<std::string>& frame_qoe_files) {
        std::cout << "=== BATCH ADAPTIVE MU ANALYSIS ===" << std::endl;
        std::cout << "Total files to analyze: " << frame_qoe_files.size() << std::endl;
        
        for (size_t i = 0; i < frame_qoe_files.size(); i++) {
            std::cout << "\n--- Analysis " << (i+1) << "/" << frame_qoe_files.size() 
                     << " ---" << std::endl;
            std::cout << "File: " << frame_qoe_files[i] << std::endl;
            
            AnalyzeAdaptiveMuPerformance(frame_qoe_files[i]);
            
            if (i < frame_qoe_files.size() - 1) {
                std::cout << "Pausing 1 second before next analysis..." << std::endl;
                sleep(1);
            }
        }
        
        std::cout << "=== BATCH ANALYSIS COMPLETED ===" << std::endl;
    }
};

// 运行单个自适应mu仿真
void RunAdaptiveMuSimulation(const std::string& trace_file, int index, int total, 
                            const std::string& video_trace_file = "") {
    std::string instance_name = ExtractInstanceName(trace_file);
        char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        std::cerr << "无法获取当前工作目录" << std::endl;
        return;
    }
    // 详细输出
    std::cout << "=== ADAPTIVE MU SIMULATION ===" << std::endl;
    std::cout << "  Instance: " << instance_name << std::endl;
    std::cout << "  Trace file: " << trace_file << std::endl;
    std::cout << "  Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "  Progress: " << (index + 1) << "/" << total << std::endl;
    std::cout << "  Initial μ: 1.0 (自适应调整)" << std::endl;
    std::cout << "  Adaptive mu: ENABLED with bounds [0.5, 2.0]" << std::endl;
    std::cout << "  Frame QoE: QoE = 0.2*U - 0.2*D - 0.3*L" << std::endl;
    std::cout << "  Adaptive rules:" << std::endl;
    std::cout << "    1. Compare loss(i,j) and loss(i-1,j)" << std::endl;
    std::cout << "    2. If Rt > max Rt in previous frame" << std::endl;
    std::cout << "    3. Compare QoE_i and QoE_i-1" << std::endl;
    std::cout << "==================================" << std::endl;


    
    // 设置目录结构
    std::string trace_folder_name = ExtractInstanceName(trace_file);
    std::string output_base_dir =std::string(current_dir) +  "/Trace_Result";
    std::string output_dir = output_base_dir + "/" + trace_folder_name;
    std::string csv_output_dir = output_dir + "/AdaptiveMu";
    
    std::cout << "Output directory: " << csv_output_dir << std::endl;
    std::string create_dirs_cmd = "mkdir -p " + csv_output_dir;
    int create_result = system(create_dirs_cmd.c_str());
    if (create_result != 0) {
        std::cerr << "Warning: Failed to create directory: " << csv_output_dir << std::endl;
    }
    
    // 构建命令行 - 使用固定的初始mu=1.0，让系统自适应调整
    std::string command = "./waf --run \"scratch/webrtc-TFMN(qoe) --m=simu --topo=change --it=" + 
                         instance_name + " --trace=" + trace_file + 
                         " --mb=6 --ls=0.01 --mu=1.0";
    
    // 如果指定了视频trace文件，添加到命令行
    if (!video_trace_file.empty()) {
        command += " --video_trace=" + video_trace_file;
    }
    command += "\"";
    
    std::cout << "=== EXECUTING COMMAND ===" << std::endl;
    std::cout << command << std::endl;
    std::cout << "=========================" << std::endl;
    
    // 记录开始时间
    time_t start_time = time(nullptr);
    std::cout << "Start time: " << ctime(&start_time);
    
    // 执行命令
    int result = system(command.c_str());
    
    // 记录结束时间
    time_t end_time = time(nullptr);
    std::cout << "End time: " << ctime(&end_time);
    
    if (result == 0) {
        std::cout << "✓ SUCCESS: Completed adaptive mu simulation" << std::endl;
        
        // 收集CSV文件
        std::vector<std::string> csv_patterns = {
            instance_name + "*bandwidth_statistics.csv",
            instance_name + "*frame_statistics.csv", 
            instance_name + "*RL_log.csv",
            instance_name + "*Frame-Rt-Reward.csv",
            instance_name + "Frame_QoE.csv"
        };
        
        for (const auto& pattern : csv_patterns) {
            std::string find_and_move_cmd = "find . -name \"" + pattern + "\" -exec mv {} " + csv_output_dir + "/ \\; 2>/dev/null";
            system(find_and_move_cmd.c_str());
        }
        
        // 重命名Frame_QoE.csv以包含实例信息
        std::string frame_qoe_target = csv_output_dir + "/" + instance_name + "_AdaptiveMu_Frame_QoE.csv";
        std::string rename_cmd = "mv Frame_QoE.csv " + frame_qoe_target + " 2>/dev/null || true";
        system(rename_cmd.c_str());
        
        // 分析自适应mu性能
        std::ifstream test_qoe_file(frame_qoe_target.c_str());
        if (test_qoe_file.good()) {
            test_qoe_file.close();
            AdaptiveMuAnalyzer analyzer;
            analyzer.AnalyzeAdaptiveMuPerformance(frame_qoe_target);
            std::cout << "✓ Adaptive mu analysis completed" << std::endl;
        } else {
            std::cout << "Note: Frame QoE file not found, checking for alternative..." << std::endl;
            // 尝试查找其他可能的文件
            std::string find_cmd = "find " + csv_output_dir + " -name \"*Frame_QoE*\" 2>/dev/null";
            system(find_cmd.c_str());
        }
        
    } else {
        std::cerr << "✗ FAILED: Simulation returned code: " << result << std::endl;
    }
    
    double duration = difftime(end_time, start_time);
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << std::endl;
}

// 批量运行自适应mu仿真
void RunBatchAdaptiveMu(const std::vector<std::string>& trace_files, 
                       const std::string& video_trace_file = "") {
    std::cout << "================================================" << std::endl;
    std::cout << "ADAPTIVE MU BATCH PROCESSING" << std::endl;
    std::cout << "Total trace files: " << trace_files.size() << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Initial μ: 1.0 (自适应调整)" << std::endl;
    std::cout << "Adaptive mu bounds: [0.5, 2.0]" << std::endl;
    std::cout << "Frame QoE: QoE = 0.2*U - 0.2*D - 0.3*L" << std::endl;
    std::cout << "================================================" << std::endl;
    
    int total_simulations = trace_files.size();
    int successful = 0;
    int failed = 0;
    
    time_t batch_start_time = time(nullptr);
    
    for (size_t i = 0; i < trace_files.size(); ++i) {
        std::cout << "\n=== SIMULATION " << (i + 1) << "/" << total_simulations << " ===" << std::endl;
        std::cout << "File: " << trace_files[i] << std::endl;
        
        try {
            RunAdaptiveMuSimulation(trace_files[i], i, total_simulations, video_trace_file);
            successful++;
            
            // 在运行之间添加延迟
            if (i < trace_files.size() - 1) {
                std::cout << "Waiting 3 seconds before next simulation..." << std::endl;
                sleep(3);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cerr << "Unknown exception" << std::endl;
            failed++;
        }
    }
    
    // 计算总耗时
    time_t batch_end_time = time(nullptr);
    double total_duration = difftime(batch_end_time, batch_start_time);
    
    // 输出总结
    std::cout << "================================================" << std::endl;
    std::cout << "ADAPTIVE MU BATCH COMPLETED" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "Total simulations: " << total_simulations << std::endl;
    std::cout << "Successful: " << successful << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Success rate: " << (successful * 100.0 / total_simulations) << "%" << std::endl;
    std::cout << "Total time: " << total_duration << " seconds" << std::endl;
    std::cout << "Average time per simulation: " << (total_duration / total_simulations) << " seconds" << std::endl;
    
    std::cout << "\nOutput Files Generated Per Simulation:" << std::endl;
    std::cout << "  1. <trace>_AdaptiveMu_Frame_QoE.csv" << std::endl;
    std::cout << "     - frame_id: 帧ID" << std::endl;
    std::cout << "     - U_F(i): 带宽利用率" << std::endl;
    std::cout << "     - D_F(i): 帧延迟(秒)" << std::endl;
    std::cout << "     - loss_F(i): 帧丢包率" << std::endl;
    std::cout << "     - QoE_i: QoE值 (0.2*U - 0.2*D - 0.3*L)" << std::endl;
    std::cout << "     - mu_used: 该帧使用的mu值" << std::endl;
    std::cout << "  2. <trace>_Frame-Rt-Reward.csv" << std::endl;
    std::cout << "  3. <trace>_RL_log.csv" << std::endl;
    std::cout << "  4. <trace>_bandwidth_statistics.csv" << std::endl;
    std::cout << "  5. <trace>_frame_statistics.csv" << std::endl;
    
    std::cout << "\nAdaptive Rules Summary:" << std::endl;
    std::cout << "  1. Compare loss(i,j) and loss(i-1,j) for same Rt group" << std::endl;
    std::cout << "  2. If Rt > max Rt in frame i-1, increase mu" << std::endl;
    std::cout << "  3. Compare QoE_i and QoE_i-1" << std::endl;
    
    std::cout << "Started: " << ctime(&batch_start_time);
    std::cout << "Finished: " << ctime(&batch_end_time);
    std::cout << "================================================" << std::endl;
}

// 比较不同初始mu值对自适应性能的影响
void CompareInitialMuEffects(const std::string& trace_file, 
                           const std::vector<double>& initial_mu_values,
                           const std::string& video_trace_file = "") {
    std::cout << "================================================" << std::endl;
    std::cout << "INITIAL MU VALUE COMPARISON STUDY" << std::endl;
    std::cout << "Trace file: " << trace_file << std::endl;
    std::cout << "Video trace: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Testing initial μ values: ";
    for (double mu : initial_mu_values) std::cout << mu << " ";
    std::cout << std::endl;
    std::cout << "Adaptive mu bounds: [0.5, 2.0]" << std::endl;
    std::cout << "================================================" << std::endl;
    
    std::map<double, std::vector<double>> mu_convergence_data;
    std::map<double, double> final_qoe_scores;
    
    for (size_t i = 0; i < initial_mu_values.size(); i++) {
        double initial_mu = initial_mu_values[i];
        std::cout << "\n=== TEST " << (i+1) << "/" << initial_mu_values.size() << " ===" << std::endl;
        std::cout << "Initial μ: " << initial_mu << std::endl;
        
        // 运行仿真
        std::string instance_name = ExtractInstanceName(trace_file) + "_initMu_" + std::to_string(initial_mu);
        
        // 构建命令行
        std::string command = "./waf --run \"scratch/webrtc-TFMN(qoe) --m=simu --topo=change --it=" + 
                             instance_name + " --trace=" + trace_file + 
                             " --mb=6 --ls=0.01 --mu=" + std::to_string(initial_mu);
        
        if (!video_trace_file.empty()) {
            command += " --video_trace=" + video_trace_file;
        }
        command += "\"";
        
        std::cout << "Command: " << command << std::endl;
        
        time_t start_time = time(nullptr);
        int result = system(command.c_str());
        time_t end_time = time(nullptr);
        
        if (result == 0) {
            std::cout << "✓ Simulation completed in " << difftime(end_time, start_time) << " seconds" << std::endl;
            
            // 分析结果文件
            std::string frame_qoe_file = instance_name + "_gcc_1_mu=" + 
                                       std::to_string(initial_mu) + "_L=0.01_Frame_QoE.csv";
            
            std::ifstream qoe_file(frame_qoe_file.c_str());
            if (qoe_file.good()) {
                std::string line;
                std::vector<double> mu_values;
                double total_qoe = 0.0;
                int frame_count = 0;
                
                // 跳过表头
                while (std::getline(qoe_file, line) && line.find("frame_id") == std::string::npos);
                
                while (std::getline(qoe_file, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    
                    std::istringstream ss(line);
                    std::string token;
                    std::vector<std::string> tokens;
                    
                    while (std::getline(ss, token, ',')) {
                        tokens.push_back(token);
                    }
                    
                    if (tokens.size() >= 6) {
                        frame_count++;
                        double mu_used = std::stod(tokens[5]);
                        double qoe = std::stod(tokens[4]);
                        
                        mu_values.push_back(mu_used);
                        total_qoe += qoe;
                    }
                }
                
                qoe_file.close();
                
                if (!mu_values.empty()) {
                    mu_convergence_data[initial_mu] = mu_values;
                    final_qoe_scores[initial_mu] = total_qoe / frame_count;
                    
                    std::cout << "  Final average QoE: " << final_qoe_scores[initial_mu] << std::endl;
                    std::cout << "  Mu convergence: " << mu_values.front() << " -> " << mu_values.back() << std::endl;
                    std::cout << "  Mu range used: [" << *std::min_element(mu_values.begin(), mu_values.end()) 
                              << ", " << *std::max_element(mu_values.begin(), mu_values.end()) << "]" << std::endl;
                }
            }
        } else {
            std::cerr << "✗ Simulation failed" << std::endl;
        }
        
        // 清理并等待
        std::string cleanup_cmd = "rm -f Frame_QoE.csv 2>/dev/null";
        system(cleanup_cmd.c_str());
        
        if (i < initial_mu_values.size() - 1) {
            std::cout << "Waiting 5 seconds before next test..." << std::endl;
            sleep(5);
        }
    }
    
    // 输出比较结果
    std::cout << "\n================================================" << std::endl;
    std::cout << "INITIAL MU COMPARISON RESULTS" << std::endl;
    std::cout << "================================================" << std::endl;
    
    std::cout << "Performance by initial μ:" << std::endl;
    for (const auto& entry : final_qoe_scores) {
        std::cout << "  Initial μ=" << entry.first << ": Avg QoE = " << entry.second << std::endl;
    }
    
    std::cout << "\nMu adaptation patterns:" << std::endl;
    for (const auto& entry : mu_convergence_data) {
        const std::vector<double>& mu_values = entry.second;
        if (mu_values.size() > 10) {
            double start_avg = std::accumulate(mu_values.begin(), mu_values.begin() + 10, 0.0) / 10;
            double end_avg = std::accumulate(mu_values.end() - 10, mu_values.end(), 0.0) / 10;
            std::cout << "  Initial μ=" << entry.first << ": " << start_avg << " -> " << end_avg 
                     << " (Δ=" << (end_avg - start_avg) << ")" << std::endl;
        }
    }
    
    // 找出最佳初始mu
    auto best_qoe = std::max_element(final_qoe_scores.begin(), final_qoe_scores.end(),
        [](const std::pair<double, double>& a, const std::pair<double, double>& b) {
            return a.second < b.second;
        });
    
    if (best_qoe != final_qoe_scores.end()) {
        std::cout << "\nRECOMMENDATION:" << std::endl;
        std::cout << "  Best initial μ: " << best_qoe->first 
                 << " (QoE = " << best_qoe->second << ")" << std::endl;
    }
    
    std::cout << "================================================" << std::endl;
}

// 显示使用说明
void ShowUsage(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --dir <directory>    Specify trace directory" << std::endl;
    std::cout << "  --video_trace <file> Specify video trace file for frame analysis" << std::endl;
    std::cout << "  --ext <extensions>   File extensions to process (comma-separated)" << std::endl;
    std::cout << "  --all                Process all files in directory" << std::endl;
    std::cout << "  --single <file>      Process a single trace file" << std::endl;
    std::cout << "  --analyze <file>     Analyze existing adaptive mu results" << std::endl;
    std::cout << "  --compare_init       Compare different initial mu values" << std::endl;
    std::cout << "  --help               Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Adaptive Mu Features:" << std::endl;
    std::cout << "  - Mu adapts within bounds [0.5, 2.0]" << std::endl;
    std::cout << "  - Based on frame-level QoE and network conditions" << std::endl;
    std::cout << "  - Frame QoE formula: QoE = 0.2*U - 0.2*D - 0.3*L" << std::endl;
    std::cout << "  - Initial μ is fixed at 1.0, system adapts automatically" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  ./waf --run \"scratch/webrtc-FMN-TFMN(qoe) --dir /path/to/traces --all\"" << std::endl;
    std::cout << "  ./waf --run \"scratch/webrtc-FMN-TFMN(qoe) --single /path/to/trace.log --video_trace /path/to/video_trace\"" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string trace_directory = "/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans";
    std::string video_trace_file = "";
    std::vector<std::string> extensions = {".log", ".txt", ".dat", ".trace", ".bw"};
    bool process_all_files = false;
    std::string single_trace_file = "";
    std::string analyze_file = "";
    bool compare_initial_mu = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dir" && i + 1 < argc) {
            trace_directory = argv[++i];
        } else if (arg == "--video_trace" && i + 1 < argc) {
            video_trace_file = argv[++i];
        } else if (arg == "--ext" && i + 1 < argc) {
            std::string ext_list = argv[++i];
            extensions.clear();
            size_t start = 0, end = 0;
            while ((end = ext_list.find(',', start)) != std::string::npos) {
                extensions.push_back(ext_list.substr(start, end - start));
                start = end + 1;
            }
            extensions.push_back(ext_list.substr(start));
        } else if (arg == "--all") {
            process_all_files = true;
        } else if (arg == "--single" && i + 1 < argc) {
            single_trace_file = argv[++i];
        } else if (arg == "--analyze" && i + 1 < argc) {
            analyze_file = argv[++i];
        } else if (arg == "--compare_init") {
            compare_initial_mu = true;
        } else if (arg == "--help") {
            ShowUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            ShowUsage(argv[0]);
            return 1;
        }
    }
    
    // 如果指定了分析模式
    if (!analyze_file.empty()) {
        AdaptiveMuAnalyzer analyzer;
        
        if (analyze_file.find("Frame_QoE") != std::string::npos) {
            analyzer.AnalyzeAdaptiveMuPerformance(analyze_file);
        } else {
            std::cout << "Note: Analysis requires Frame_QoE.csv file" << std::endl;
        }
        return 0;
    }
    
    std::cout << "================================================" << std::endl;
    std::cout << "ADAPTIVE MU BATCH PROCESSOR" << std::endl;
    std::cout << "Frame QoE Analysis with Self-Adjusting μ" << std::endl;
    std::cout << "================================================" << std::endl;
    
    if (!single_trace_file.empty()) {
        std::cout << "Single trace file mode: " << single_trace_file << std::endl;
    } else {
        std::cout << "Trace directory: " << trace_directory << std::endl;
    }
    
    if (!video_trace_file.empty()) {
        std::cout << "Video trace file: " << video_trace_file << std::endl;
    }
    
    std::cout << "Initial μ: 1.0 (自适应调整)" << std::endl;
    std::cout << "Adaptive mu bounds: [0.5, 2.0]" << std::endl;
    std::cout << "Frame QoE formula: QoE = 0.2*U - 0.2*D - 0.3*L" << std::endl;
    std::cout << "Adaptive rules:" << std::endl;
    std::cout << "  1. Compare loss(i,j) and loss(i-1,j) for same Rt group" << std::endl;
    std::cout << "  2. If Rt > max Rt in frame i-1, increase mu" << std::endl;
    std::cout << "  3. Compare QoE_i and QoE_i-1" << std::endl;
    
    // 获取trace文件
    std::vector<std::string> trace_files;
    
    if (!single_trace_file.empty()) {
        if (IsRegularFile(single_trace_file)) {
            trace_files.push_back(single_trace_file);
            std::cout << "Processing single trace file: " << single_trace_file << std::endl;
        } else {
            std::cerr << "Single trace file does not exist: " << single_trace_file << std::endl;
            return -1;
        }
    } else if (process_all_files) {
        trace_files = GetAllTraceFiles(trace_directory);
    } else {
        trace_files = GetTraceFilesByExtension(trace_directory, extensions);
    }
    
    if (trace_files.empty()) {
        std::cerr << "No trace files found!" << std::endl;
        return -1;
    }
    
    time_t batch_start_time = time(nullptr);
    
    if (compare_initial_mu) {
        // 比较不同初始mu值
        std::vector<double> initial_mu_values = {0.5, 0.8, 1.0, 1.2, 1.5};
        if (!single_trace_file.empty()) {
            CompareInitialMuEffects(single_trace_file, initial_mu_values, video_trace_file);
        } else if (!trace_files.empty()) {
            CompareInitialMuEffects(trace_files[0], initial_mu_values, video_trace_file);
        }
    } else {
        // 运行自适应mu批处理
        RunBatchAdaptiveMu(trace_files, video_trace_file);
    }
    
    time_t batch_end_time = time(nullptr);
    double total_duration = difftime(batch_end_time, batch_start_time);
    
    std::cout << "\n================================================" << std::endl;
    std::cout << "PROCESSING COMPLETED" << std::endl;
    std::cout << "Total time: " << total_duration << " seconds" << std::endl;
    std::cout << "Started: " << ctime(&batch_start_time);
    std::cout << "Finished: " << ctime(&batch_end_time);
    std::cout << "================================================" << std::endl;
    
    return 0;
}