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
            if (filename == "." || filename == "..") {
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
            if (filename == "." || filename == "..") {
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

// 在 ExtractInstanceName 函数之前添加 GetTraceFolderName 函数
std::string GetTraceFolderName(const std::string& trace_file_path) {
    std::string path = trace_file_path;
    
    // 移除文件名，只保留路径
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        path = path.substr(0, last_slash);
        
        // 再找一次，获取父文件夹名称
        last_slash = path.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            return path.substr(last_slash + 1);
        } else {
            return path;
        }
    }
    
    return "unknown_trace";
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

// RL状态分析器类
class RLStateAnalyzer {
public:
    RLStateAnalyzer() {
        std::cout << "RLStateAnalyzer initialized" << std::endl;
    }
    
    // 分析RL日志文件
    void AnalyzeRLLog(const std::string& rl_log_file) {
        std::ifstream file(rl_log_file.c_str());
        if (!file.is_open()) {
            std::cerr << "Cannot open RL log file for analysis: " << rl_log_file << std::endl;
            return;
        }
        
        std::string line;
        std::getline(file, line); // 跳过表头
        
        int total_packets = 0;
        double total_reward = 0.0;
        double total_bandwidth_utilization = 0.0;
        double total_p_delay = 0.0;
        double total_p_loss = 0.0;
        double total_p_mddl = 0.0;
        std::map<double, int> mu_usage_count;
        
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 12) {
                total_packets++;
                double reward = std::stod(tokens[5]);
                double mu_used = std::stod(tokens[2]);
                double bandwidth_utilization = std::stod(tokens[8]);
                double p_delay = std::stod(tokens[9]);
                double p_loss = std::stod(tokens[10]);
                double p_mddl = std::stod(tokens[11]);
                
                total_reward += reward;
                total_bandwidth_utilization += bandwidth_utilization;
                total_p_delay += p_delay;
                total_p_loss += p_loss;
                total_p_mddl += p_mddl;
                mu_usage_count[mu_used]++;
            }
        }
        
        file.close();
        
        if (total_packets > 0) {
            for (const auto& entry : mu_usage_count) {
                double percentage = (entry.second * 100.0) / total_packets;
                std::cout << "  mu=" << entry.first << ": " << entry.second << " packets (" << percentage << "%)" << std::endl;
            }
            std::cout << "=====================================" << std::endl;
        }
    }
    
    // 分析Rt分组奖励文件
    void AnalyzeRtGroupRewards(const std::string& rt_group_file) {
        std::ifstream file(rt_group_file.c_str());
        if (!file.is_open()) {
            std::cerr << "Cannot open Rt group rewards file for analysis: " << rt_group_file << std::endl;
            return;
        }
        
        std::string line;
        std::getline(file, line); // 跳过表头
        
        int total_groups = 0;
        double total_avg_reward = 0.0;
        std::map<uint32_t, int> rt_distribution;
        std::map<uint32_t, double> rt_total_reward;
        std::map<uint32_t, int> rt_packet_count;
        
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 7) {
                total_groups++;
                uint32_t rt_value = std::stoul(tokens[1]);
                double avg_reward = std::stod(tokens[3]);
                uint32_t packet_count = std::stoul(tokens[4]);
                
                total_avg_reward += avg_reward;
                rt_distribution[rt_value]++;
                rt_total_reward[rt_value] += avg_reward;
                rt_packet_count[rt_value] += packet_count;
            }
        }
        
        file.close();
        
        if (total_groups > 0) {
            std::cout << "=== Rt Group Reward Analysis ===" << std::endl;
            std::cout << "Total Rt groups: " << total_groups << std::endl;
            std::cout << "Average reward per group: " << (total_avg_reward / total_groups) << std::endl;
            
            std::cout << "Rt value distribution:" << std::endl;
            for (const auto& entry : rt_distribution) {
                double percentage = (entry.second * 100.0) / total_groups;
                std::cout << "  Rt=" << entry.first << ": " << entry.second << " groups (" << percentage << "%)" << std::endl;
            }
            
            std::cout << "Average reward by Rt value:" << std::endl;
            for (const auto& entry : rt_distribution) {
                uint32_t rt = entry.first;
                double avg_reward_for_rt = rt_total_reward[rt] / entry.second;
                std::cout << "  Rt=" << rt << ": avg_reward=" << avg_reward_for_rt 
                          << ", total_packets=" << rt_packet_count[rt] << std::endl;
            }
            std::cout << "===================================" << std::endl;
        }
    }
    
    // 批量分析RL日志
    void BatchAnalyzeRLLogs(const std::vector<std::string>& rl_log_files) {
        std::cout << "=== Batch RL State Analysis ===" << std::endl;
        for (const auto& log_file : rl_log_files) {
            AnalyzeRLLog(log_file);
        }
        std::cout << "=== Batch Analysis Completed ===" << std::endl;
    }
};

// 修改：使用固定loss_rate参数，因为实际loss值从trace中读取
void RunSingleTraceSimulation(const std::string& trace_file, int index, int total, 
                             double bandwidth_scale_factor = 1.0, double loss_rate = 0.01,
                             const std::string& video_trace_file = "", bool oscc_mode = false) {
    std::string instance_name = ExtractInstanceName(trace_file);
    char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        std::cerr << "无法获取当前工作目录" << std::endl;
        return;
    }
    // 详细的调试输出
    std::cout << "=== DEBUG: RunSingleTraceSimulation Parameters ===" << std::endl;
    std::cout << "  Instance: " << instance_name << std::endl;
    std::cout << "  Trace file: " << trace_file << std::endl;
    std::cout << "  Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "  Progress: " << (index + 1) << "/" << total << std::endl;
    std::cout << "  OSCC mode: " << (oscc_mode ? "ENABLED" : "disabled") << std::endl;
    if (oscc_mode) {
        std::cout << "  Initial μ: " << bandwidth_scale_factor << " (will be dynamically adjusted)" << std::endl;
    } else {
        std::cout << "  Bandwidth scale factor (μ): " << bandwidth_scale_factor << std::endl;
    }
    std::cout << "  Loss rate parameter (for RL manager): " << loss_rate << " (注: 实际loss值从trace文件读取)" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    // 设置目录结构
    std::string trace_folder_name = GetTraceFolderName(trace_file);
    // std::string output_base_dir = "/mnt/nasDisk/hjt/oscc/Trace_Result";
    std::string output_base_dir = std::string(current_dir) + "/Trace_Result";
    std::string output_dir = output_base_dir + "/" + trace_folder_name;
    std::string csv_output_dir = output_dir + "/CSV";
    
    std::cout << "Output directory: " << csv_output_dir << std::endl;
    std::string create_dirs_cmd = "mkdir -p " + csv_output_dir;
    int create_result = system(create_dirs_cmd.c_str());
    if (create_result != 0) {
        std::cerr << "Warning: Failed to create directory: " << csv_output_dir << std::endl;
    }
    
    // 构建命令行 - 使用 webrtc-TFMN(QoE) 程序
    // 注意：loss_rate参数仍然传递给程序，但代码会从trace文件中读取实际的loss值
    std::string command = "./waf --run \"scratch/webrtc-TFMN(QoE) --m=simu --topo=change --it=" + 
                         instance_name + " --trace=" + trace_file + 
                         " --mb=5 --ls=" + std::to_string(loss_rate) + " --mu=" + std::to_string(bandwidth_scale_factor);
    
    // 如果指定了视频trace文件，添加到命令行
    if (!video_trace_file.empty()) {
        command += " --video_trace=" + video_trace_file;
    }
    
    // 如果启用OSCC模式，添加 --oscc 参数
    if (oscc_mode) {
        command += " --oscc=true";
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
        std::cout << "✓ SUCCESS: Completed simulation" << std::endl;
        
        // 收集CSV文件 - 新增：包含_Frame-Rt-Reward.csv文件
        std::vector<std::string> csv_patterns = {
            instance_name + "*bandwidth_statistics.csv",
            instance_name + "*frame_statistics.csv", 
            instance_name + "*RL_log.csv",
            instance_name + "*Frame-Rt-Reward.csv",  // 新增：Rt分组奖励文件
            instance_name + "*OSCC_mu_trace.csv",      // 新增：OSCC mu跟踪文件
            instance_name + "*OSCC_qoe.csv",           // 新增：OSCC QoE文件
            instance_name + "*bandwidth_history.csv"  // 新增：带宽历史文件
        };
        
        for (const auto& pattern : csv_patterns) {
            std::string find_and_move_cmd = "find . -name \"" + pattern + "\" -exec mv {} " + csv_output_dir + "/ \\; 2>/dev/null";
            system(find_and_move_cmd.c_str());
        }
        
        // 分析RL日志
        std::string rl_log_file = csv_output_dir + "/" + instance_name + "_gcc_1_mu=" + 
                                std::to_string(bandwidth_scale_factor) + "_L=" + 
                                std::to_string(loss_rate) + "_RL_log.csv";
        
        std::ifstream test_file(rl_log_file.c_str());
        if (test_file.good()) {
            test_file.close();
            RLStateAnalyzer analyzer;
            analyzer.AnalyzeRLLog(rl_log_file);
        }
        
        // 分析Rt分组奖励文件
        std::string rt_group_file = csv_output_dir + "/" + instance_name + "_gcc_1_mu=" + 
                                  std::to_string(bandwidth_scale_factor) + "_L=" + 
                                  std::to_string(loss_rate) + "_Frame-Rt-Reward.csv";
        
        std::ifstream test_rt_file(rt_group_file.c_str());
        if (test_rt_file.good()) {
            test_rt_file.close();
            RLStateAnalyzer analyzer;
            analyzer.AnalyzeRtGroupRewards(rt_group_file);
        } else {
            std::cout << "Note: Rt group reward file not found: " << rt_group_file << std::endl;
            // 尝试查找文件
            std::cout << "Trying to find Rt group reward files in current directory..." << std::endl;
            std::string find_cmd = "find . -name \"*Frame-Rt-Reward.csv\" 2>/dev/null";
            system(find_cmd.c_str());
        }
        
    } else {
        std::cerr << "✗ FAILED: Simulation returned code: " << result << std::endl;
    }
    
    double duration = difftime(end_time, start_time);
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << std::endl;
}

// 批量运行不同带宽缩放系数的仿真（仅带宽缩放，不包含loss率）
void RunBatchBandwidthScaling(const std::string& trace_file, const std::vector<double>& scale_factors, 
                             double loss_rate = 0.01, const std::string& video_trace_file = "") {
    std::string instance_name = ExtractInstanceName(trace_file);
    
    std::cout << "================================================" << std::endl;
    std::cout << "BANDWIDTH SCALING BATCH PROCESSING" << std::endl;
    std::cout << "Trace file: " << trace_file << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Instance name: " << instance_name << std::endl;
    std::cout << "Loss rate parameter (固定): " << loss_rate << " (注: 实际loss值从trace文件读取)" << std::endl;
    std::cout << "Scale factors to test: ";
    for (double factor : scale_factors) {
        std::cout << factor << " ";
    }
    std::cout << std::endl;
    std::cout << "================================================" << std::endl;
    
    int total_runs = scale_factors.size();
    int successful_runs = 0;
    int failed_runs = 0;
    
    time_t batch_start_time = time(nullptr);
    
    for (size_t i = 0; i < scale_factors.size(); ++i) {
        std::cout << "RUN " << (i + 1) << "/" << total_runs << " with μ=" << scale_factors[i] << ", L=" << loss_rate << std::endl;
        
        try {
            RunSingleTraceSimulation(trace_file, i, total_runs, scale_factors[i], loss_rate, video_trace_file);
            successful_runs++;
            
            // 在运行之间添加延迟
            if (i < scale_factors.size() - 1) {
                std::cout << "Waiting 3 seconds before next simulation..." << std::endl;
                sleep(3);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Exception while processing with μ=" << scale_factors[i] << ": " << e.what() << std::endl;
            failed_runs++;
        } catch (...) {
            std::cerr << "Unknown exception while processing with μ=" << scale_factors[i] << std::endl;
            failed_runs++;
        }
    }
    
    // 计算总耗时
    time_t batch_end_time = time(nullptr);
    double total_duration = difftime(batch_end_time, batch_start_time);
    
    // 输出总结
    std::cout << "================================================" << std::endl;
    std::cout << "BANDWIDTH SCALING BATCH COMPLETED" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "Trace file: " << trace_file << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Loss rate parameter (固定): " << loss_rate << std::endl;
    std::cout << "Total runs: " << total_runs << std::endl;
    std::cout << "Successful runs: " << successful_runs << std::endl;
    std::cout << "Failed runs: " << failed_runs << std::endl;
    std::cout << "Success rate: " << (successful_runs * 100.0 / total_runs) << "%" << std::endl;
    std::cout << "Total time: " << total_duration << " seconds" << std::endl;
    std::cout << "Average time per run: " << (total_duration / total_runs) << " seconds" << std::endl;
    std::cout << "Scale factors tested: ";
    for (double factor : scale_factors) {
        std::cout << factor << " ";
    }
    std::cout << std::endl;
    std::cout << "注意: 实际loss值从trace文件读取，loss_rate参数仅用于RL管理器初始化" << std::endl;
    std::cout << "RL state records generated for each run with format: <trace>_mu=<value>_L=" << loss_rate << "_RL_log.csv" << std::endl;
    std::cout << "Rt group reward records generated with format: <trace>_mu=<value>_L=" << loss_rate << "_Frame-Rt-Reward.csv" << std::endl;
    std::cout << "Started: " << ctime(&batch_start_time);
    std::cout << "Finished: " << ctime(&batch_end_time);
    std::cout << "================================================" << std::endl;
}

// 修改：移除RunBatchLossRateScaling函数，因为loss值现在从trace文件读取
// 不再需要批量测试不同loss率

// 自定义批量处理 - 只进行带宽缩放，不进行loss率测试
void RunCustomBatchProcessing(const std::vector<std::string>& trace_files, const std::string& video_trace_file = "") {
    // 修改：只保留带宽缩放系数，移除loss值数组
    // std::vector<double> custom_mu_values  = {0.5,0.52,0.54,0.56,0.58,0.6,0.62,0.64,0.66,0.68,0.7,0.72,0.74,0.76,0.78,0.8,0.82,0.84,0.86,0.88,0.9,0.92,0.94,0.96,0.98, 1.0,1.02,1.04,1.06,1.08, 1.1, 1.12, 1.14, 1.16, 1.18,1.2,1.22,1.24,1.26,1.28,1.30,1.32,1.34,1.36,1.38,1.4,1.42,1.44,1.46,1.48,1.5};
    std::vector<double> custom_mu_values={0.5,0.6,1.0,1.2};
    // 固定loss_rate参数为0.01，实际loss值从trace文件读取
    double fixed_loss_rate = 0.01;
    
    std::cout << "=== CUSTOM BATCH PROCESSING STARTED ===" << std::endl;
    std::cout << "Total trace files: " << trace_files.size() << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Mu values to test: ";
    for (double mu : custom_mu_values) std::cout << mu << " ";
    std::cout << std::endl;
    std::cout << "Loss rate (固定参数): " << fixed_loss_rate << " (注: 实际loss值从trace文件读取)" << std::endl;
    std::cout << "Total parameter combinations: " << custom_mu_values.size() << std::endl;
    std::cout << "Total simulations: " << (trace_files.size() * custom_mu_values.size()) << std::endl;
    std::cout << "======================================" << std::endl;
    
    int total_simulations = trace_files.size() * custom_mu_values.size();
    int completed = 0;
    int successful = 0;
    int failed = 0;
    
    time_t start_time = time(nullptr);
    
    for (size_t file_idx = 0; file_idx < trace_files.size(); ++file_idx) {
        const std::string& trace_file = trace_files[file_idx];
        
        for (double mu : custom_mu_values) {
            std::cout << "\n*** PROCESSING COMBINATION " << (completed+1) << "/" << total_simulations << " ***" << std::endl;
            std::cout << "File: " << (file_idx+1) << "/" << trace_files.size() << " - " << trace_file << std::endl;
            std::cout << "Video trace: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
            std::cout << "Parameters: μ=" << mu << ", L=" << fixed_loss_rate << " (固定参数)" << std::endl;
            
            try {
                RunSingleTraceSimulation(trace_file, completed, total_simulations, mu, fixed_loss_rate, video_trace_file);
                successful++;
                std::cout << "*** SUCCESS: Combination " << (completed+1) << " completed ***" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "*** FAILED: " << e.what() << " ***" << std::endl;
                failed++;
            }
            
            completed++;
            
            // 添加延迟
            if (completed < total_simulations) {
                std::cout << "Waiting 2 seconds before next simulation..." << std::endl;
                sleep(2);
            }
        }
    }
    
    time_t end_time = time(nullptr);
    double total_duration = difftime(end_time, start_time);
    
    std::cout << "\n=== CUSTOM BATCH PROCESSING COMPLETED ===" << std::endl;
    std::cout << "Video trace file used: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Fixed loss rate parameter: " << fixed_loss_rate << std::endl;
    std::cout << "Successful: " << successful << "/" << total_simulations << std::endl;
    std::cout << "Failed: " << failed << "/" << total_simulations << std::endl;
    std::cout << "Total time: " << total_duration << " seconds" << std::endl;
    std::cout << "==========================================" << std::endl;
}

// 显示使用说明
void ShowUsage(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --dir <directory>    Specify trace directory" << std::endl;
    std::cout << "  --video_trace <file> Specify video trace file for frame analysis" << std::endl;
    std::cout << "  --ext <extensions>   File extensions to process (comma-separated)" << std::endl;
    std::cout << "  --all                Process all files in directory" << std::endl;
    std::cout << "  --mu <factors>       Bandwidth scale factors to test (comma-separated)" << std::endl;
    std::cout << "  --loss <rate>        Fixed loss rate parameter (默认: 0.01, 实际loss从trace文件读取)" << std::endl;
    std::cout << "  --single <file>      Process a single trace file" << std::endl;
    std::cout << "  --analyze <file>     Analyze existing RL log file" << std::endl;
    std::cout << "  --custom             Use custom parameter set (仅带宽缩放)" << std::endl;
    std::cout << "  --oscc               Enable OSCC mode (dynamic adaptive μ adjustment)" << std::endl;
    std::cout << "  --help               Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "OSCC模式说明:" << std::endl;
    std::cout << "  - 启用HAFA启发式算法进行动态μ调整" << std::endl;
    std::cout << "  - 帧内调整：根据同一Rt组的丢包率对比" << std::endl;
    std::cout << "  - 帧间调整：根据整帧QoE对比" << std::endl;
    std::cout << "  - 参数: epsilon=0.02, mu_range=[0.5, 1.5]" << std::endl;
    std::cout << "  - 初始μ=1.0，后续动态调整" << std::endl;
    std::cout << std::endl;
    std::cout << "重要更新:" << std::endl;
    std::cout << "  - Loss值现在从trace文件读取，不再需要批处理不同loss率" << std::endl;
    std::cout << "  - 仅进行带宽缩放(μ)的批处理测试" << std::endl;
    std::cout << "  - loss_rate参数用于RL管理器初始化，实际loss值从trace获取" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string trace_directory = "/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans";
    std::string video_trace_file = "";  // 新增：视频trace文件参数
    std::vector<std::string> extensions = {".log", ".txt", ".dat", ".trace", ".bw"};
    bool process_all_files = false;
    std::string single_trace_file = "";
    std::string analyze_rl_log = "";
    bool use_custom_params = false;
    std::vector<double> bandwidth_scale_factors = {1.0};
    double fixed_loss_rate = 0.01;  // 固定loss_rate参数，实际loss从trace文件读取
    bool oscc_mode = false;  // OSCC模式：启用动态μ调整
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dir" && i + 1 < argc) {
            trace_directory = argv[++i];
        } else if (arg == "--video_trace" && i + 1 < argc) {  // 新增参数
            video_trace_file = argv[++i];
        } else if (arg == "--oscc") {  // OSCC模式
            oscc_mode = true;
            // bandwidth_scale_factors = {1.0};  // OSCC模式下，初始μ=1.0，后续动态调整
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
        } else if (arg == "--mu" && i + 1 < argc) {
            std::string mu_list = argv[++i];
            bandwidth_scale_factors.clear();
            size_t start = 0, end = 0;
            while ((end = mu_list.find(',', start)) != std::string::npos) {
                try {
                    double factor = std::stod(mu_list.substr(start, end - start));
                    bandwidth_scale_factors.push_back(factor);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid scale factor: " << mu_list.substr(start, end - start) << std::endl;
                }
                start = end + 1;
            }
            try {
                double factor = std::stod(mu_list.substr(start));
                bandwidth_scale_factors.push_back(factor);
            } catch (const std::exception& e) {
                std::cerr << "Invalid scale factor: " << mu_list.substr(start) << std::endl;
            }
        } else if (arg == "--loss" && i + 1 < argc) {
            // 修改：只接受单个loss_rate参数
            try {
                fixed_loss_rate = std::stod(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Invalid loss rate: " << argv[i] << std::endl;
            }
        } else if (arg == "--single" && i + 1 < argc) {
            single_trace_file = argv[++i];
        } else if (arg == "--analyze" && i + 1 < argc) {
            analyze_rl_log = argv[++i];
        } else if (arg == "--custom") {
            use_custom_params = true;
            bandwidth_scale_factors = {0.7,0.8,0.9,1.0,1.1,1.2};
            fixed_loss_rate = 0.01;  // 固定参数
        } else if (arg == "--help") {
            ShowUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            ShowUsage(argv[0]);
            return 1;
        }
    }
    
    // 如果指定了分析模式，直接分析RL日志文件
    if (!analyze_rl_log.empty()) {
        RLStateAnalyzer analyzer;
        analyzer.AnalyzeRLLog(analyze_rl_log);
        return 0;
    }
    
    std::cout << "================================================" << std::endl;
    std::cout << "WEBRTC TRACE BATCH PROCESSOR WITH VIDEO TRACE ANALYSIS" << std::endl;
    if (oscc_mode) {
        std::cout << "OSCC MODE: DYNAMIC ADAPTIVE MU ADJUSTMENT" << std::endl;
    } else {
        std::cout << "BANDWIDTH SCALING AND RL STATE MANAGEMENT" << std::endl;
    }
    std::cout << "================================================" << std::endl;
    if (oscc_mode) {
        std::cout << "重要: 启用OSCC动态μ调整算法（HAFA启发式）" << std::endl;
        std::cout << "      μ值将根据帧内/帧间反馈自动调整" << std::endl;
        std::cout << "      参数: epsilon=0.02, mu_range=[0.5, 1.5]" << std::endl;
    } else {
        std::cout << "重要更新: Loss值从trace文件读取，仅进行带宽缩放测试" << std::endl;
    }
    std::cout << "================================================" << std::endl;
    
    if (!single_trace_file.empty()) {
        std::cout << "Single trace file mode: " << single_trace_file << std::endl;
    } else {
        std::cout << "Trace directory: " << trace_directory << std::endl;
    }
    
    if (!video_trace_file.empty()) {
        std::cout << "Video trace file: " << video_trace_file << std::endl;
        std::cout << "Video trace analysis: ENABLED" << std::endl;
    } else {
        std::cout << "Video trace analysis: DISABLED" << std::endl;
    }
    
    std::cout << "Bandwidth scale factors to test: ";
    for (double factor : bandwidth_scale_factors) {
        std::cout << factor << " ";
    }
    std::cout << std::endl;
    
    std::cout << "Fixed loss rate parameter: " << fixed_loss_rate << " (实际loss值从trace文件读取)" << std::endl;
    
    if (oscc_mode) {
        std::cout << "OSCC MODE: ENABLED (Dynamic adaptive μ adjustment)" << std::endl;
    } else if (use_custom_params) {
        std::cout << "CUSTOM PARAMETER SET ACTIVATED (仅带宽缩放)" << std::endl;
    }
    
    time_t batch_start_time = time(nullptr);
    std::cout << "Batch processing started at: " << ctime(&batch_start_time);
    
    // 获取trace文件
    std::vector<std::string> trace_files;
    
    if (!single_trace_file.empty()) {
        if (IsRegularFile(single_trace_file)) {
            trace_files.push_back(single_trace_file);
            std::cout << "Processing single trace file: " << single_trace_file << std::endl;
        } else {
            std::cerr << "Single trace file does not exist or is not a regular file: " << single_trace_file << std::endl;
            return -1;
        }
    } else if (process_all_files) {
        trace_files = GetAllTraceFiles(trace_directory);
    } else {
        trace_files = GetTraceFilesByExtension(trace_directory, extensions);
    }
    
    if (trace_files.empty()) {
        std::cerr << "No trace files found!" << std::endl;
        if (!single_trace_file.empty()) {
            std::cerr << "Specified file: " << single_trace_file << std::endl;
        } else {
            std::cerr << "Directory: " << trace_directory << std::endl;
        }
        return -1;
    }
    
    // OSCC模式：只运行一次，μ值动态调整
    if (oscc_mode) {
        std::cout << "=== OSCC MODE: Running with dynamic μ adjustment ===" << std::endl;
        std::cout << "Initial μ: 1.0 (will be dynamically adjusted during simulation)" << std::endl;
        std::cout << "Total trace files to process: " << trace_files.size() << std::endl;
        
        int successful_runs = 0;
        int failed_runs = 0;
        
        for (size_t file_index = 0; file_index < trace_files.size(); ++file_index) {
            const std::string& trace_file = trace_files[file_index];
            
            std::cout << "\n*** OSCC Processing file " << (file_index + 1) << "/" << trace_files.size() << " ***" << std::endl;
            
            try {
                // OSCC模式：初始μ=1.0，通过oscc_mode=true启用动态调整
                RunSingleTraceSimulation(trace_file, file_index, trace_files.size(), 
                                       1.0, fixed_loss_rate, video_trace_file, true);  // oscc_mode=true
                successful_runs++;
                
                if (file_index < trace_files.size() - 1) {
                    std::cout << "Waiting 2 seconds before next file..." << std::endl;
                    sleep(2);
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Exception while processing " << trace_file << ": " << e.what() << std::endl;
                failed_runs++;
            } catch (...) {
                std::cerr << "Unknown exception while processing " << trace_file << std::endl;
                failed_runs++;
            }
        }
        
        std::cout << "\n=== OSCC BATCH PROCESSING COMPLETED ===" << std::endl;
        std::cout << "Successful: " << successful_runs << "/" << trace_files.size() << std::endl;
        std::cout << "Failed: " << failed_runs << "/" << trace_files.size() << std::endl;
        std::cout << "==========================================" << std::endl;
        
    } else if (use_custom_params) {
        // 使用自定义批量处理函数，传递视频trace文件参数
        RunCustomBatchProcessing(trace_files, video_trace_file);
    } else {
        // 修改：仅进行带宽缩放批处理
        int total_simulations = trace_files.size() * bandwidth_scale_factors.size();
        int completed_simulations = 0;
        int successful_runs = 0;
        int failed_runs = 0;
        
        // 处理每个trace文件
        for (size_t file_index = 0; file_index < trace_files.size(); ++file_index) {
            const std::string& trace_file = trace_files[file_index];
            
            if (bandwidth_scale_factors.size() > 1) {
                // 仅进行带宽缩放批处理
                RunBatchBandwidthScaling(trace_file, bandwidth_scale_factors, fixed_loss_rate, video_trace_file);
                completed_simulations += bandwidth_scale_factors.size();
                successful_runs += bandwidth_scale_factors.size();
            } else {
                // 单个参数运行
                try {
                    RunSingleTraceSimulation(trace_file, file_index, trace_files.size(), 
                                           bandwidth_scale_factors[0], fixed_loss_rate, video_trace_file, false);  // oscc_mode=false
                    successful_runs++;
                    completed_simulations++;
                    
                    if (file_index < trace_files.size() - 1) {
                        std::cout << "Waiting 2 seconds before next file..." << std::endl;
                        sleep(2);
                    }
                    
                } catch (const std::exception& e) {
                    std::cerr << "Exception while processing " << trace_file << ": " << e.what() << std::endl;
                    failed_runs++;
                    completed_simulations++;
                } catch (...) {
                    std::cerr << "Unknown exception while processing " << trace_file << std::endl;
                    failed_runs++;
                    completed_simulations++;
                }
            }
        }
        
        // 计算总耗时
        time_t batch_end_time = time(nullptr);
        double total_duration = difftime(batch_end_time, batch_start_time);
        
        // 输出总结
        std::cout << "================================================" << std::endl;
        std::cout << "BATCH PROCESSING COMPLETED" << std::endl;
        std::cout << "================================================" << std::endl;
        
        if (!single_trace_file.empty()) {
            std::cout << "Single trace file: " << single_trace_file << std::endl;
        } else {
            std::cout << "Trace directory: " << trace_directory << std::endl;
        }
        
        std::cout << "Total files processed: " << trace_files.size() << std::endl;
        std::cout << "Total simulations: " << total_simulations << std::endl;
        std::cout << "Completed simulations: " << completed_simulations << std::endl;
        std::cout << "Successful runs: " << successful_runs << std::endl;
        std::cout << "Failed runs: " << failed_runs << std::endl;
        
        if (completed_simulations > 0) {
            std::cout << "Success rate: " << (successful_runs * 100.0 / completed_simulations) << "%" << std::endl;
        }
        
        std::cout << "Total time: " << total_duration << " seconds" << std::endl;
        
        if (completed_simulations > 0) {
            std::cout << "Average time per simulation: " << (total_duration / completed_simulations) << " seconds" << std::endl;
        }
        
        std::cout << "Bandwidth scale factors tested: ";
        for (double factor : bandwidth_scale_factors) {
            std::cout << factor << " ";
        }
        std::cout << std::endl;
        
        std::cout << "Fixed loss rate parameter: " << fixed_loss_rate << " (实际loss值从trace文件读取)" << std::endl;
        
        std::cout << "输出文件格式说明:" << std::endl;
        std::cout << "  RL日志文件: <trace>_mu=<value>_L=" << fixed_loss_rate << "_RL_log.csv" << std::endl;
        std::cout << "  Rt分组奖励文件: <trace>_mu=<value>_L=" << fixed_loss_rate << "_Frame-Rt-Reward.csv" << std::endl;
        
        std::cout << "Started: " << ctime(&batch_start_time);
        std::cout << "Finished: " << ctime(&batch_end_time);
        std::cout << "================================================" << std::endl;
        
        return (failed_runs > 0) ? 1 : 0;
    }
    
    return 0;
}


// ./waf --run "scratch/webrtc-FmR-M --dir /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans --all --custom"
// ./waf --run "scratch/webrtc-FMN-TFMN(QoE) --oscc --single --dir /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/ --video_trace /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/video_trace/AsianCup_China_Uzbekistan/frame_trace_0 --all --custom"

// 处理单个trace文件
//./waf --run "scratch/webrtc-FMN-TFMN(QoE) --oscc --single /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/AItrans_0.log --video_trace /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/video_trace/AsianCup_China_Uzbekistan/frame_trace_0

// 处理目录下所有文件
// ./waf --run "scratch/webrtc-FMN-TFMN(QoE) --oscc --dir /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/ --all --video_trace /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/video_trace/AsianCup_China_Uzbekistan/frame_trace_0"

// 重定向输出到webrtc_ns3.log文件
// ./waf --run "scratch/webrtc-FMN-TFMN(QoE) --oscc --dir /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/ --all --video_trace /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/video_trace/AsianCup_China_Uzbekistan/frame_trace_0" > webrtc_ns3.log 2>&1

//./waf --run "scratch/webrtc-FMN-TFMN(QoE) --oscc --dir /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/ --all " > webrtc_ns3.log 2>&1