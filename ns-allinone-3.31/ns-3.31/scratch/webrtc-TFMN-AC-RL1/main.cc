#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <cstring>

#include "ns3/webrtc-defines.h"
#include "simulation.h"
#include "ns3/log.h"

using namespace oscc;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("webrtc-static");

int main(int argc, char* argv[]) {
    // 重定向std::cout到日志文件
    std::ofstream log_file("webrtc_simulation.log");
    std::streambuf* cout_buffer = std::cout.rdbuf();
    std::cout.rdbuf(log_file.rdbuf());
    
    std::cout << "=== WebRTC TraceAll-Frame with Real Frame Analysis, Bandwidth Scaling and RL State Management Starting ===" << std::endl;
    
    // 启用详细日志
    LogComponentEnable("webrtc-static", LOG_LEVEL_ALL);
    LogComponentEnable("WebrtcSender", LOG_LEVEL_ALL); 
    LogComponentEnable("WebrtcReceiver", LOG_LEVEL_ALL);
    
    // 设置默认参数
    std::string mode("simu");
    std::string topo("change");
    std::string instance("default_instance");
    std::string trace_file("");
    std::string frame_weight("1280");
    std::string frame_height("720");
    std::string max_bandwidth("10");
    std::string loss_rate("0.01");
    std::string folder("trace_results");
    std::string bandwidth_scale("1.0");
    std::string oscc_enabled("false");
    std::string learner_enabled_str("false");
    std::string learner_type("bandit");
    std::string fps_str("30");
    std::string frame_trace_output("");
    std::string skip_frame_str("false");
    
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("m", "mode", mode);
    cmd.AddValue("topo", "topology", topo);
    cmd.AddValue("it", "instance", instance);
    cmd.AddValue("trace", "trace file path", trace_file);
    cmd.AddValue("mb", "max_bandwidth", max_bandwidth);
    cmd.AddValue("ls", "loss_rate", loss_rate);
    cmd.AddValue("folder", "folder name to collect data", folder);
    cmd.AddValue("mu", "bandwidth_scale_factor", bandwidth_scale);
    cmd.AddValue("oscc", "enable OSCC dynamic mu adjustment", oscc_enabled);
    cmd.AddValue("learner", "enable lightweight RL learner for mu (replaces OSCC)", learner_enabled_str);
    cmd.AddValue("learner_type", "learner type: 'bandit' (default) or 'mlp'/'torch' (2-layer MLP)", learner_type);
    cmd.AddValue("frame_trace", "frame trace output file path", frame_trace_output);
    cmd.AddValue("fps", "frame rate", fps_str);
    cmd.AddValue("skip", "enable skip frame logic", skip_frame_str);
    
    cmd.Parse(argc, argv);
    
    // 解析布尔参数
    bool oscc_mode = (oscc_enabled == "true" || oscc_enabled == "1" || oscc_enabled == "yes");
    bool learner_mode = (learner_enabled_str == "true" || learner_enabled_str == "1" || learner_enabled_str == "yes");
    bool skip_frame_enabled = (skip_frame_str == "true" || skip_frame_str == "1" || skip_frame_str == "yes");
    
    // 验证必要参数
    if (trace_file.empty()) {
        std::cerr << "ERROR: No trace file specified. Use --trace=<file_path>" << std::endl;
        std::cerr << "Usage: ./waf --run \"scratch/webrtc-TFMN(AC-RL1) --trace=<path> [--it=<instance> --folder=<output_dir> --mb=<bandwidth> --ls=<loss_rate> --mu=<scale_factor>]\"" << std::endl;
        return 1;
    }
    
    // 检查trace文件是否存在
    std::ifstream test_file(trace_file);
    if (!test_file.good()) {
        std::cerr << "ERROR: Trace file does not exist or cannot be read: " << trace_file << std::endl;
        return 1;
    }
    test_file.close();
    
    // 设置控制器类型
    ns3::TimeConollerType controller_type = ns3::TimeConollerType::SIMU_CONTROLLER;
    if (mode == "simu") {
        webrtc_register_clock();
        std::cout << "Using SIMU controller" << std::endl;
    } else if (mode == "emu") {
        controller_type = ns3::TimeConollerType::EMU_CONTROLLER;
        GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl")); 
        std::cout << "Using EMU controller" << std::endl;
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
    
    // 转换参数类型
    double mu, mb, ls;
    uint32_t fps;
    try {
        mb = std::stod(max_bandwidth);
        ls = std::stod(loss_rate);
        mu = std::stod(bandwidth_scale);
        fps = std::stoul(fps_str);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Invalid parameter format: " << e.what() << std::endl;
        return 1;
    }
    
    // 设置默认帧trace输出路径
    if (frame_trace_output.empty()) {
        frame_trace_output = folder + "/" + instance + "_frame_trace.csv";
    }

    std::cout << "Starting single trace simulation with real frame analysis..." << std::endl;
    std::cout << "Max bandwidth: " << mb << " Mbps" << std::endl;
    std::cout << "Loss rate: " << ls << std::endl;
    std::cout << "OSCC mode: " << (oscc_mode ? "ENABLED" : "disabled") << std::endl;
    std::cout << "Learner mode: " << (learner_mode ? "ENABLED (lightweight RL)" : "disabled") << std::endl;
    if (learner_mode) {
        std::cout << "Learner type: " << learner_type << std::endl;
    }
    if (learner_mode) {
        std::cout << "Initial bandwidth scale factor μ: " << mu << " (will be learned online)" << std::endl;
    } else if (oscc_mode) {
        std::cout << "Initial bandwidth scale factor μ: " << mu << " (will be dynamically adjusted)" << std::endl;
    } else {
        std::cout << "Bandwidth scale factor μ: " << mu << std::endl;
    }
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Skip frame: " << (skip_frame_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Frame trace output: " << frame_trace_output << std::endl;
    
    // 运行仿真
    run_single_trace_simulation(trace_file, instance, controller_type, 1, mb, ls, folder, mu, oscc_mode,
                               fps, frame_trace_output, skip_frame_enabled, learner_mode, learner_type);
    
    std::cout << "=== WebRTC TraceAll-Frame with Real Frame Analysis Completed Successfully ===" << std::endl;
    
    // 恢复std::cout并关闭日志文件
    std::cout.rdbuf(cout_buffer);
    log_file.close();
    
    _exit(0);
    return 0;
}
