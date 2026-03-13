#include "simulation.h"
#include "ns3/log.h"
#include "ns3/ipv4-global-routing-helper.h"
#include <unistd.h>
#include <sys/stat.h>
#include <memory>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("Simulation");

static const float startTime = 0.001;

std::string GetTraceFolderName(const std::string& trace_file_path) {
    std::string path = trace_file_path;
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        path = path.substr(0, last_slash);
        last_slash = path.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            return path.substr(last_slash + 1);
        }
    }
    return "unknown_trace";
}

std::unique_ptr<WebrtcSessionManager> CreateWebrtcSessionManager(
    webrtc::TimeController* controller,
    uint32_t max_rate, uint32_t min_rate, uint32_t start_rate,
    uint32_t h, uint32_t w, uint32_t fps) {
    std::unique_ptr<WebrtcSessionManager> webrtc_manager(
        new WebrtcSessionManager(controller, min_rate, start_rate, max_rate, h, w, fps));
    webrtc_manager->CreateClients();
    return webrtc_manager;
}

void TriggerRLStateCalculation(RLStateManager* rl_manager,
                               BandwidthChanger* bandwidth_changer, 
                               Ptr<ExponentialRandomVariable> interval,
                               double current_loss_rate) {
    Time now = Simulator::Now();
    uint32_t timestamp_ms = static_cast<uint32_t>(now.GetMilliSeconds());
    
    double trace_rtt_ms = 30.0;
    double trace_loss_rate = 0.01;
    
    if (bandwidth_changer != nullptr) {
        trace_rtt_ms = bandwidth_changer->GetRTTAtTime(timestamp_ms);
        trace_loss_rate = bandwidth_changer->GetLossAtTime(timestamp_ms);
    } 
    
    double current_trace_bw = 0.0;
    if (bandwidth_changer != nullptr) {
        current_trace_bw = bandwidth_changer->GetCurrentTraceBandwidth();
    } else {
        current_trace_bw = 20 * 1000000.0;
    }
    
    double current_delay = rl_manager->GetCurrentDelay();
    
    if (rl_manager != nullptr) {
        rl_manager->UpdateNetworkState(current_delay, trace_loss_rate, MilliSeconds(trace_rtt_ms));
    }
    
    double next_interval = interval->GetValue();
    
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Simulator::Schedule(Seconds(next_interval), &TriggerRLStateCalculation,
                           rl_manager, bandwidth_changer, interval, trace_loss_rate);
    }
}

void InstallWebrtcApplication(Ptr<Node> sender,
                              Ptr<Node> receiver,
                              uint16_t send_port,
                              uint16_t recv_port,
                              Time start_app,
                              Time stop_app,
                              WebrtcSessionManager* manager,
                              FrameAwareWebrtcTrace* trace,
                              QoEIntegrationManager* qoe_manager,
                              RLStateManager* rl_manager,
                              double bandwidth_scale_factor,
                              double loss_rate,
                              BandwidthChanger* bandwidth_changer,
                              FramePlayoutManager* frame_playout_manager,
                              bool skip_frame_enabled) {
    std::cout << "\n[DEBUG] InstallWebrtcApplication called" << std::endl;
    std::cout << "  Bandwidth scale factor: " << bandwidth_scale_factor << std::endl;
    std::cout << "  Loss rate: " << loss_rate << std::endl;
    std::cout << "  Skip frame enabled: " << (skip_frame_enabled ? "YES" : "NO") << std::endl;
    
    NS_LOG_INFO("Installing WebRTC application with RL state management and real frame analysis");
    
    Ptr<WebrtcSender> sendApp = CreateObject<WebrtcSender>(manager);
    Ptr<WebrtcReceiver> recvApp = CreateObject<WebrtcReceiver>(manager);
    
    // 集成 FramePlayoutManager
    if (frame_playout_manager && recvApp) {
        recvApp->SetFramePlayoutManager(frame_playout_manager);
        frame_playout_manager->SetSkipFrameEnabled(skip_frame_enabled);
        std::cout << "[DEBUG] FramePlayoutManager set in WebrtcReceiver with skip_enabled=" << skip_frame_enabled << std::endl;
        
        if (sendApp) {
            frame_playout_manager->SetSkipFrameCallback([sendApp](uint32_t target_keyframe_id) {
                std::cout << "[FramePlayoutManager] Skip frame callback triggered, target keyframe: " 
                          << target_keyframe_id << std::endl;
                if (sendApp) {
                    sendApp->SkipToFrame(target_keyframe_id);
                    std::cout << "[FramePlayoutManager] Notified sender to skip to keyframe " 
                              << target_keyframe_id << std::endl;
                }
            });
            std::cout << "[DEBUG] Skip frame callback registered in FramePlayoutManager" << std::endl;
        }
        
        if (qoe_manager) {
            frame_playout_manager->SetPacketReceivedCallback(
                [qoe_manager](const FramePacketInfo& info, const FrameStatistics& stats) {
                    qoe_manager->OnPacketReceived(info, stats);
                }
            );
            
            frame_playout_manager->SetFrameCompleteCallback(
                [qoe_manager](const FrameStatistics& stats) {
                    qoe_manager->OnFrameComplete(stats);
                }
            );
            std::cout << "[DEBUG] QoEIntegrationManager connected to FramePlayoutManager callbacks" << std::endl;
        }
    }
    
    if (sendApp) {
        sendApp->SetBandwidthScaleFactor(bandwidth_scale_factor);
        NS_LOG_INFO("WebrtcSender bandwidth scale factor set to: " << bandwidth_scale_factor);
    }
    
    if (trace && sendApp) {
        sendApp->SetScaledBwTraceFuc(MakeCallback(&FrameAwareWebrtcTrace::OnScaledBandwidth, trace));
        NS_LOG_INFO("Scaled bandwidth callback set for WebrtcSender");
    }
    
    if (qoe_manager) {
        if (rl_manager) qoe_manager->SetRLStateManager(rl_manager);
        if (bandwidth_changer) qoe_manager->SetBandwidthChanger(bandwidth_changer);
        if (sendApp) qoe_manager->SetWebrtcSender(sendApp);
        if (trace) qoe_manager->SetFrameAwareWebrtcTrace(trace);
        
        // [暂时注释] OSCCController 尚未实现，后续若添加 OSCCController 类及
        // QoEIntegrationManager::GetOSCCController() 方法可恢复以下代码
        // OSCCController* oscc = qoe_manager->GetOSCCController();
        // if (oscc && sendApp) {
        //     oscc->SetWebrtcSender(sendApp);
        //     std::cout << "[DEBUG] WebrtcSender set in OSCCController for direct mu application" << std::endl;
        // }
    }
    
    if (trace && bandwidth_changer) {
        trace->SetBandwidthChanger(bandwidth_changer);
        std::cout << "[DEBUG] BandwidthChanger set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    if (trace) {
        trace->SetCurrentParameters(bandwidth_scale_factor, loss_rate);
    }
    
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Ptr<ExponentialRandomVariable> interval = CreateObject<ExponentialRandomVariable>();
        interval->SetAttribute("Mean", DoubleValue(0.01));// RL状态更新定时器的平均间隔，单位秒
        Simulator::Schedule(Seconds(0.1), &TriggerRLStateCalculation,
                        rl_manager, bandwidth_changer, interval, loss_rate);
    }
    
    sender->AddApplication(sendApp);
    receiver->AddApplication(recvApp);
    sendApp->Bind(send_port);
    recvApp->Bind(recv_port);
    
    Ptr<Ipv4> ipv4 = receiver->GetObject<Ipv4>();
    Ipv4Address addr = ipv4->GetAddress(1, 0).GetLocal();
    sendApp->ConfigurePeer(addr, recv_port);
    
    ipv4 = sender->GetObject<Ipv4>();
    addr = ipv4->GetAddress(1, 0).GetLocal();
    recvApp->ConfigurePeer(addr, send_port);
    
    if (trace) {
        if (trace->LogFlag() & WebrtcTrace::E_WEBRTC_BW) {
            sendApp->SetBwTraceFuc(MakeCallback(&WebrtcTrace::OnBW, trace));
            std::cout << "[DEBUG] Bandwidth trace callback set for WebrtcSender" << std::endl;
        }
        if (trace->LogFlag() & (WebrtcTrace::E_WEBRTC_OWD | WebrtcTrace::E_WEBRTC_LOSS)) {
            recvApp->SetTraceReceiptPktInfo(MakeCallback(&FrameAwareWebrtcTrace::OnReceiptPktInfo, trace));
            std::cout << "[DEBUG] Packet receipt callback set for WebrtcReceiver" << std::endl;
        }
    }
    
    if (qoe_manager && trace) {
        trace->SetQoEManager(qoe_manager);
        std::cout << "[DEBUG] QoEIntegrationManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    if (rl_manager && trace) {
        trace->SetRLStateManager(rl_manager);
        std::cout << "[DEBUG] RLStateManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    sendApp->SetStartTime(start_app);
    sendApp->SetStopTime(stop_app);
    recvApp->SetStartTime(start_app);
    recvApp->SetStopTime(stop_app + Seconds(1));
    
    std::cout << "[SUCCESS] WebRTC application installed successfully" << std::endl;
}

void test_app_on_p2p(const std::string& instance, TimeConollerType controller_type, int num, 
                     float startapptime, float endapptime, double max_bandwith,
                     TriggerRandomLoss* trigger_loss, BandwidthChanger* changer, 
                     const std::string& trace_filename, double bandwidth_scale_factor,
                     double loss_rate, uint32_t fps,
                     const std::string& frame_trace_output, bool skip_frame_enabled,
                     const std::string& base_output_folder) {
    std::cout << "\n=== test_app_on_p2p started with Real Video Frame Analysis ===" << std::endl;
    std::cout << "Instance: " << instance << std::endl;
    std::cout << "Normalized application time: " << startapptime << "s to " << endapptime << "s" << std::endl;
    std::cout << "Total duration: " << (endapptime - startapptime) << " seconds" << std::endl;
    std::cout << "Max bandwidth: " << max_bandwith << " Mbps" << std::endl;
    std::cout << "Trace file: " << trace_filename << std::endl;
    std::cout << "Bandwidth scale factor μ: " << bandwidth_scale_factor << std::endl;
    std::cout << "Loss rate: " << loss_rate << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Skip frame: " << (skip_frame_enabled ? "ENABLED" : "DISABLED") << std::endl;
    
    NS_ASSERT(startapptime == 0.0);
    
    uint64_t bps = max_bandwith * kBwUnit;
    uint32_t link_delay = 20;
    uint32_t buffer_delay = 15;

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", DataRateValue(DataRate(bps)));
    pointToPoint.SetChannelAttribute("Delay", TimeValue(MilliSeconds(link_delay)));
    auto bufSize = std::max<uint32_t>(DEFAULT_PACKET_SIZE, bps * buffer_delay / 8000);
    
    int packets = bufSize / DEFAULT_PACKET_SIZE;
    pointToPoint.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(std::to_string(5) + "p"));
    NetDeviceContainer devices = pointToPoint.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    TrafficControlHelper pfifoHelper;
    uint16_t handle = pfifoHelper.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue(std::to_string(packets) + "p"));
    pfifoHelper.AddInternalQueues(handle, 1, "ns3::DropTailQueue", "MaxSize", StringValue(std::to_string(packets) + "p"));
    
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue(std::to_string(packets) + "p"));
    QueueDiscContainer qdiscs = tch.Install(devices);

    Ipv4AddressHelper address;
    std::string nodeip = "10.1.1.0";
    address.SetBase(nodeip.c_str(), "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    
    if (trigger_loss) {
        trigger_loss->RegisterDevice(devices.Get(1));
        std::cout << "Trigger loss registered on receiver device with rate: " 
                << trigger_loss->GetLossRate() << std::endl;
    }

    if (changer) {
        changer->RegisterDevice(devices.Get(0));
        std::cout << "Bandwidth changer registered on sender device" << std::endl;
        std::cout << "BandwidthChanger object address: " << changer << std::endl;
    } else {
        std::cout << "WARNING: BandwidthChanger is null!" << std::endl;
    }

    std::string webrtc_log_com("_gcc_");
    
    int64_t webrtc_start_us = static_cast<int64_t>(startapptime * 1000000);
    int64_t webrtc_stop_us = static_cast<int64_t>(endapptime * 1000000);
    
    std::cout << "WebRTC controller time: " << webrtc_start_us << "us to " << webrtc_stop_us << "us" << std::endl;
    
    webrtc::TimeController* time_controller = CreateTimeController(
        controller_type == TimeConollerType::SIMU_CONTROLLER ? TimeConollerType::SIMU_CONTROLLER : TimeConollerType::EMU_CONTROLLER, 
        webrtc_start_us, webrtc_stop_us);
    uint32_t max_rate = bps / 1000;

    std::vector<std::unique_ptr<WebrtcSessionManager>> sesssion_manager;
    uint32_t default_frame_height = 1080;
    uint32_t default_frame_width = 1920;
    for (int i = 0; i < num; i++) {
        std::unique_ptr<WebrtcSessionManager> m(CreateWebrtcSessionManager(
            time_controller, max_rate * 0.1, max_rate * 0.2, max_rate, 
            default_frame_height, default_frame_width, fps));
        sesssion_manager.push_back(std::move(m)); 
    }
    
    UtilCalculator* calculator = UtilCalculator::Instance();
    calculator->Enable();
    
    uint16_t sendPort = 5432;
    uint16_t recvPort = 5000;
    
    std::string trace_base_name = trace_filename;
    size_t last_slash = trace_base_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        trace_base_name = trace_base_name.substr(last_slash + 1);
    }
    size_t last_dot = trace_base_name.find_last_of(".");
    if (last_dot != std::string::npos) {
        trace_base_name = trace_base_name.substr(0, last_dot);
    }
    
    std::string prefix = instance + "_" + trace_base_name + webrtc_log_com;
    std::vector<FrameAwareWebrtcTrace*> trace_vec;
    
    // 创建管理器
    std::vector<std::unique_ptr<QoEIntegrationManager>> qoe_managers;
    std::vector<std::unique_ptr<FramePlayoutManager>> frame_playout_managers;
    std::vector<std::unique_ptr<RLStateManager>> rl_managers;
    std::vector<std::unique_ptr<AiMuLearner>> ai_learners;
    
    // 初始化 FramePlayoutManager
    for (int i = 0; i < num; i++) {
        auto pm = std::make_unique<FramePlayoutManager>();
        pm->SetFPS(fps);
        frame_playout_managers.push_back(std::move(pm));
    }
    
    // 初始化 RLStateManager
    for (int i = 0; i < num; i++) {
        auto rl = std::make_unique<RLStateManager>();
        double initial_rtt = 30.0;
        double initial_loss = loss_rate;
        if (changer && !trace_filename.empty()) {
            TraceData initial_trace_data = changer->GetTraceDataAtTime(0);
            initial_rtt = initial_trace_data.rtt;
            initial_loss = initial_trace_data.loss;
        }
        rl->SetParameters(bandwidth_scale_factor, loss_rate, MilliSeconds(initial_rtt));
        rl->SetCurrentLossRate(initial_loss);
        rl_managers.push_back(std::move(rl));
    }
    
    // 初始化 AiMuLearner (ns3-ai shared memory)
    std::cout << "\n=== Initializing AiMuLearners (Python-based via ns3-ai) ===" << std::endl;
    for (int i = 0; i < num; i++) {
        MuLearnerConfig config;
        config.mu_min = 0.5;
        config.mu_max = 1.5;
        config.rt_max = 10.0; //20.0;
        config.loss_max = 0.1;
        config.baseline_decay = 0.95;
        
        auto learner = std::make_unique<AiMuLearner>(config, AiMuLearner::kDefaultShmId + i);
        rl_managers[i]->SetMuLearner(learner.get());
        ai_learners.push_back(std::move(learner));
        
        std::cout << "AiMuLearner " << i << " initialized (shm_id=" << (AiMuLearner::kDefaultShmId + i) << ", awaiting Python)" << std::endl;
    }
    std::cout << "================================\n" << std::endl;
    
    // 初始化 QoEIntegrationManager
    for (int i = 0; i < num; i++) {
        auto qoe = std::make_unique<QoEIntegrationManager>();
        qoe->SetRLStateManager(rl_managers[i].get());
        qoe->SetBandwidthChanger(changer);
        
        // 设置 AI learner
        if (i < static_cast<int>(ai_learners.size())) {
            qoe->SetMuLearner(ai_learners[i].get(), true);
        }
        qoe_managers.push_back(std::move(qoe));
    }
        
    for (int i = 0; i < num; i++) {
        std::string log = prefix + std::to_string(i + 1);
        FrameAwareWebrtcTrace* trace = new FrameAwareWebrtcTrace(qoe_managers[i].get(), rl_managers[i].get(), bandwidth_scale_factor);
        trace_vec.push_back(trace);
        
        trace->SetCurrentParameters(bandwidth_scale_factor, loss_rate);
        
        trace->Log(log, WebrtcTrace::E_WEBRTC_BW | WebrtcTrace::E_WEBRTC_LOSS | WebrtcTrace::E_WEBRTC_OWD);
        
        if (qoe_managers[i] && changer) {
            qoe_managers[i]->SetBandwidthChanger(changer);
        }
        
        if (trace && changer) {
            trace->SetBandwidthChanger(changer);
        }
        
        InstallWebrtcApplication(nodes.Get(0), nodes.Get(1), sendPort, recvPort,
                    Seconds(startapptime), Seconds(endapptime),
                sesssion_manager.at(i).get(), trace, 
                qoe_managers[i].get(), rl_managers[i].get(), 
                bandwidth_scale_factor, loss_rate, changer,
                frame_playout_managers[i].get(),
                skip_frame_enabled);
        
        sendPort++;
        recvPort++;
    }

    float simulation_stop_time = endapptime + 10.0;
    
    std::cout << "Simulator will stop at: " << simulation_stop_time << " seconds" << std::endl;
    
    Simulator::Stop(Seconds(simulation_stop_time));
    uint64_t last = get_os_millis();
    
    std::cout << "Starting simulation..." << std::endl;
    Simulator::Run();
    std::cout << "Simulation completed at: " << Simulator::Now().GetSeconds() << " seconds" << std::endl;
    
    // 导出 Trace
    for (int i = 0; i < num; i++) {
        std::string trace_output_file = frame_trace_output;
        if (trace_output_file.empty()) {
            trace_output_file = base_output_folder + "/" + prefix + std::to_string(i + 1) + "_frame_playout_trace.csv";
        }
        frame_playout_managers[i]->ExportFrameTrace(trace_output_file);
        
        std::string bw_history_file = base_output_folder + "/" + prefix + std::to_string(i + 1) + "_bandwidth_history.csv";
        qoe_managers[i]->OutputBandwidthHistory(bw_history_file);
        
        std::string bw_stats_file = base_output_folder + "/" + prefix + std::to_string(i + 1) + "_mu=" + 
                                std::to_string(bandwidth_scale_factor) + "_L=" + 
                                std::to_string(loss_rate) + "_bandwidth_statistics.csv";
        trace_vec[i]->OutputBandwidthStatistics(bw_stats_file, loss_rate);

        rl_managers[i]->OutputStateRecords(base_output_folder + "/" + prefix + std::to_string(i + 1), bandwidth_scale_factor, loss_rate);
        rl_managers[i]->OutputRtGroupRewards(base_output_folder + "/" + prefix + std::to_string(i + 1), bandwidth_scale_factor, loss_rate);
        
        // Ai learner 输出
        if (i < static_cast<int>(ai_learners.size()) && ai_learners[i]) {
            rl_managers[i]->OutputLearnerLog(base_output_folder + "/" + prefix + std::to_string(i + 1), bandwidth_scale_factor, loss_rate);
            qoe_managers[i]->OutputMuTrace(base_output_folder + "/" + prefix + std::to_string(i + 1) + "_mu_trace.csv");
            qoe_managers[i]->OutputFrameQoE(base_output_folder + "/" + prefix + std::to_string(i + 1) + "_frame_qoe.csv");
            std::cout << "[Simulation] Final Ai learner status for instance " << i << ": " 
                      << ai_learners[i]->GetStatusString() << std::endl;
        }
    }
    
    Simulator::Destroy();
    std::cout << "Simulator destroyed" << std::endl;
    
    if (time_controller) {
        delete time_controller;
        time_controller = nullptr;
    }
    
    {
        int64_t last_stamp = calculator->GetLastReceiptMillis();
        int64_t channnel_bit = 0;
        
        if (changer) {
            changer->TotalThroughput(MilliSeconds(last_stamp), channnel_bit);
        } else {
            if (last_stamp > startapptime * 1000) {
                double duration_seconds = (last_stamp - startapptime * 1000) / 1000.0;
                channnel_bit = static_cast<int64_t>(bps * duration_seconds);
            }
        }
        
        calculator->CalculateUtil(prefix, channnel_bit);
    }

    for (auto it = trace_vec.begin(); it != trace_vec.end(); it++) {
        FrameAwareWebrtcTrace* trace = (*it);
        delete trace;
    }
    trace_vec.clear();
    
    // Redundant safety net: Simulator::Destroy() already triggered SetFinish() via
    // ScheduleDestroy registered in Ns3AIRL ctor. This explicit call ensures Python
    // is notified even if ScheduleDestroy didn't fire for some reason.
    for (auto& learner : ai_learners) {
        if (learner) learner->NotifySimulationEnd();
    }
    
    uint32_t elapse = (get_os_millis() - last);
    std::cout << "run time millis: " << elapse << std::endl;
    _exit(0);
}

void run_single_trace_simulation(const std::string& trace_file, const std::string& instance, 
                                 TimeConollerType controller_type, int num, double max_bandwith,
                                 double loss_rate, const std::string& base_output_folder,
                                 double bandwidth_scale_factor,
                                 uint32_t fps, const std::string& frame_trace_output,
                                 bool skip_frame_enabled) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Starting simulation for: " << trace_file << std::endl;
    std::cout << "Instance: " << instance << std::endl;
    std::cout << "Max bandwidth: " << max_bandwith << " Mbps" << std::endl;
    std::cout << "Loss rate: " << loss_rate << " (THIS SHOULD BE 0.01, 0.02, etc.)" << std::endl;
    std::cout << "AI Learner mode: ENABLED (Python-based RL via ns3-ai)" << std::endl;
    std::cout << "Initial bandwidth scale factor μ: " << bandwidth_scale_factor << " (will be learned by Python agent)" << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Skip frame: " << (skip_frame_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Frame trace output: " << (frame_trace_output.empty() ? "auto-generated" : frame_trace_output) << std::endl;
    std::cout << "Base output folder: " << base_output_folder << std::endl;
    std::cout << "==========================================" << std::endl;
    
    std::string data_result_folder = base_output_folder;
    
    std::cout << "Using output path: " << data_result_folder << std::endl;
    
    std::string create_dir_cmd = "mkdir -p " + data_result_folder;
    int result = system(create_dir_cmd.c_str());
    if (result != 0) {
        std::cerr << "Warning: Failed to create directory: " << data_result_folder << std::endl;
    } else {
        std::cout << "Confirmed output directory: " << data_result_folder << std::endl;
    }
    
    {
        char buffer[128] = {0};
        if (getcwd(buffer, sizeof(buffer)) != buffer) {
            std::cerr << "Path error" << std::endl;
            return;
        }
        std::string ns3_path(buffer, ::strlen(buffer));
        if ('/' != ns3_path.back()) {
            ns3_path.push_back('/');
        }
        std::string full_output_path = ns3_path + data_result_folder;
        
        std::cout << "Setting output folder to: " << full_output_path << std::endl;
        set_webrtc_trace_folder(full_output_path);
    }
    
    std::unique_ptr<TriggerRandomLoss> triggerloss = nullptr;
    std::unique_ptr<BandwidthChanger> changer = nullptr;

    std::cout << "Configuring packet loss rate: " << loss_rate << std::endl;
    
    Config::SetDefault("ns3::RateErrorModel::ErrorRate", DoubleValue(loss_rate));
    Config::SetDefault("ns3::RateErrorModel::ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
    Config::SetDefault("ns3::BurstErrorModel::ErrorRate", DoubleValue(loss_rate));
    Config::SetDefault("ns3::BurstErrorModel::BurstSize", StringValue("ns3::UniformRandomVariable[Min=1|Max=3]"));
    
    triggerloss.reset(new TriggerRandomLoss(loss_rate));
    
    changer.reset(new BandwidthChanger());
    
    std::cout << "Reading and normalizing trace file..." << std::endl;
    std::pair<float, float> during_time = changer->ConfigwithReadNetworkTrace(max_bandwith * kBwUnit, trace_file);
    
    if (during_time.second <= 0) {
        std::cerr << "ERROR: Failed to read trace file or file is empty: " << trace_file << std::endl;
        return;
    }
    
    changer->Start();
    if (triggerloss) {
        triggerloss->SetBandwidthChanger(changer.get());
        triggerloss->SetUseTraceLoss(true);
        triggerloss->SetUpdateInterval(100);
        triggerloss->Start();
        std::cout << "TriggerRandomLoss started with initial rate: " << loss_rate << std::endl;
        std::cout << "  Dynamic loss update from trace: ENABLED" << std::endl;
        std::cout << "  Update interval: 100ms" << std::endl;
    }
    
    float startapptime = during_time.first;
    float endapptime = during_time.second;
    
    std::cout << "Normalized simulation time range: " << startapptime << "s to " << endapptime << "s" << std::endl;
    std::cout << "Total simulation duration: " << (endapptime - startapptime) << "s" << std::endl;
    
    test_app_on_p2p(instance, controller_type, num, startapptime, endapptime, 
                   max_bandwith, triggerloss.get(), changer.get(), trace_file, 
                   bandwidth_scale_factor, loss_rate,
                   fps, frame_trace_output, skip_frame_enabled,
                   base_output_folder);
    
    std::cout << "Simulation completed successfully" << std::endl;
    
    if (triggerloss) {
        triggerloss.reset();
    }
    if (changer) {
        changer.reset();
    }
}

} // namespace oscc
