#ifndef SIMULATION_H
#define SIMULATION_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common_types.h"
#include "mu_learner.h"
#include "ai_mu_learner.h"
#include "rl_state_manager.h"
#include "network_components.h"
#include "qoe_manager.h"
#include "webrtc_trace.h"

#include "ns3/webrtc-defines.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ex-webrtc-module.h"
#include "ns3/frame-playout-manager.h"

namespace oscc {

using namespace ns3;

// 使用 ns3 命名空间中的 TimeConollerType
using ns3::TimeConollerType;

// ============================================================================
// 工具函数
// ============================================================================

// 从trace文件路径提取父文件夹名称
std::string GetTraceFolderName(const std::string& trace_file_path);

// 创建WebRTC会话管理器
std::unique_ptr<WebrtcSessionManager> CreateWebrtcSessionManager(
    webrtc::TimeController* controller,
    uint32_t max_rate = 20000,
    uint32_t min_rate = 100,
    uint32_t start_rate = 500,
    uint32_t h = 720,
    uint32_t w = 1280, 
    uint32_t fps = 30);

// ============================================================================
// 仿真函数
// ============================================================================

// 触发RL状态计算
void TriggerRLStateCalculation(
    RLStateManager* rl_manager,
    BandwidthChanger* bandwidth_changer, 
    Ptr<ExponentialRandomVariable> interval,
    double current_loss_rate);

// 安装WebRTC应用程序
void InstallWebrtcApplication(
    Ptr<Node> sender,
    Ptr<Node> receiver,
    uint16_t send_port,
    uint16_t recv_port,
    Time start_app,
    Time stop_app,
    WebrtcSessionManager* manager,
    FrameAwareWebrtcTrace* trace = nullptr,
    QoEIntegrationManager* qoe_manager = nullptr,
    RLStateManager* rl_manager = nullptr,
    double bandwidth_scale_factor = 1.0,
    double loss_rate = 0.01,
    BandwidthChanger* bandwidth_changer = nullptr,
    FramePlayoutManager* frame_playout_manager = nullptr,
    bool skip_frame_enabled = false);

// P2P网络仿真测试
void test_app_on_p2p(
    const std::string& instance, 
    TimeConollerType controller_type, 
    int num, 
    float startapptime, 
    float endapptime, 
    double max_bandwith,
    TriggerRandomLoss* trigger_loss, 
    BandwidthChanger* changer, 
    const std::string& trace_filename = "", 
    double bandwidth_scale_factor = 1.0,
    double loss_rate = 0.01,
    uint32_t fps = 30,
    const std::string& frame_trace_output = "",
    bool skip_frame_enabled = false,
    const std::string& base_output_folder = "trace_results",
    const std::string& mask_table_path = "");

// 运行单个trace仿真
void run_single_trace_simulation(
    const std::string& trace_file, 
    const std::string& instance, 
    TimeConollerType controller_type, 
    int num, 
    double max_bandwith,
    double loss_rate, 
    const std::string& base_output_folder = "Trace_Result",
    double bandwidth_scale_factor = 1.0, 
    uint32_t fps = 30,
    const std::string& frame_trace_output = "",
    bool skip_frame_enabled = false,
    const std::string& mask_table_path = "");

} // namespace oscc

#endif // SIMULATION_H
