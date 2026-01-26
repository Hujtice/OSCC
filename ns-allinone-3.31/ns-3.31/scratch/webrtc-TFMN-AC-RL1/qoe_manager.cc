#include "qoe_manager.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace oscc {

NS_LOG_COMPONENT_DEFINE("QoEManager");

QoEIntegrationManager::QoEIntegrationManager() 
    : oscc_controller_(nullptr), rl_manager_(nullptr), bw_changer_(nullptr), 
      webrtc_sender_(nullptr), mu_learner_(nullptr), use_learner_(false) {}

void QoEIntegrationManager::SetOSCCController(OSCCController* controller) {
    oscc_controller_ = controller;
}

void QoEIntegrationManager::SetRLStateManager(RLStateManager* manager) {
    rl_manager_ = manager;
}

void QoEIntegrationManager::SetBandwidthChanger(BandwidthChanger* changer) {
    bw_changer_ = changer;
}

void QoEIntegrationManager::SetWebrtcSender(Ptr<WebrtcSender> sender) {
    webrtc_sender_ = sender;
}

void QoEIntegrationManager::SetMuLearner(IMuLearner* learner, bool use_learner) {
    mu_learner_ = learner;
    use_learner_ = (learner != nullptr) && use_learner;
    NS_LOG_INFO("QoEIntegrationManager: MuLearner " << (use_learner_ ? "enabled" : "disabled"));
    if (use_learner_) {
        std::cout << "[QoEIntegrationManager] MuLearner enabled for mu decisions" << std::endl;
    }
}

void QoEIntegrationManager::OnPacketReceived(const FramePacketInfo& info, const FrameStatistics& frame_stats) {
    if (!rl_manager_) return;
    
    Time now = Simulator::Now();
    Time send_time = MilliSeconds(info.send_time_ms);
    double delay_ms = (now - send_time).GetMilliSeconds();
    if (delay_ms < 0) delay_ms = 0;

    // 更新 OSCC 滑动窗口
    if (oscc_controller_ && oscc_controller_->IsEnabled()) {
        oscc_controller_->UpdateLossWindow(false);
    }

    // 获取网络信息
    double trace_loss = 0.01;
    double trace_rtt = 30.0;
    double trace_bw = 20000000.0;
    
    if (bw_changer_) {
        uint32_t ts = now.GetMilliSeconds();
        trace_loss = bw_changer_->GetLossAtTime(ts);
        trace_rtt = bw_changer_->GetRTTAtTime(ts);
    }
    
    rl_manager_->UpdateNetworkState(delay_ms, trace_loss, MilliSeconds(trace_rtt));
    
    // 优先使用平滑后的GCC带宽
    double smoothed_bw = GetSmoothedGccBandwidth(now, 5);
    std::cout << "smoothed_bw: " << smoothed_bw << std::endl;
    if (smoothed_bw > 0) {
        trace_bw = smoothed_bw;
    } else if (bw_changer_) {
        trace_bw = bw_changer_->GetTraceBandwidthAtTime(now.GetMilliSeconds());
    }
    std::cout << "trace_bw: " << trace_bw << std::endl;
    
    // 获取GCC带宽
    double gcc_bw = GetNearestGccBandwidth(now);
    if (gcc_bw <= 0) gcc_bw = trace_bw * 0.7;
    
    uint32_t Rt = rl_manager_->CalculateTransmissionOpportunities(now, frame_stats.playout_deadline, 
                                                                info.packet_size, trace_bw);
    
    double mu = rl_manager_->GetCurrentMu();
    
    // 优先使用MuLearner
    if (use_learner_ && mu_learner_) {
        MuState state(static_cast<double>(Rt), trace_loss);
        MuAction action = mu_learner_->Act(state);
        double learner_mu = action.mu;
        
        if (std::abs(learner_mu - mu) > 0.001) {
            mu = learner_mu;
            rl_manager_->SetMu(mu);
            if (webrtc_sender_) webrtc_sender_->UpdateMuDynamic(mu);
            
            NS_LOG_DEBUG("MuLearner: Applied mu=" << mu << " for Rt=" << Rt << ", loss=" << trace_loss);
        }
    }
    // 回退到OSCC
    else if (oscc_controller_ && oscc_controller_->IsEnabled()) {
        double oscc_mu = oscc_controller_->GetMuForPacket(info.frame_id, Rt);
        if (std::abs(oscc_mu - mu) > 0.001) {
            mu = oscc_mu;
            rl_manager_->SetMu(mu);
            if (webrtc_sender_) webrtc_sender_->UpdateMuDynamic(mu);
        }
    }
    
    // 计算奖励
    uint32_t packet_idx = info.is_first_packet ? 0 : 1;
    
    double miss_deadline_time = 0.0;
    if (now > frame_stats.playout_deadline) {
        miss_deadline_time = (now - frame_stats.playout_deadline).GetSeconds();
    }

    double reward = rl_manager_->CalculateReward(mu, gcc_bw, trace_bw, delay_ms, trace_loss, 
                                                miss_deadline_time, Rt, rl_manager_->GetLastPacketRt(), 
                                                info.frame_id, packet_idx);
    
    double bw_util = (gcc_bw * mu) / trace_bw;
    
    rl_manager_->RecordPacketState(info.frame_id, packet_idx, mu, Rt, trace_loss, reward, 
                                   send_time, now, frame_stats.playout_deadline, 
                                   bw_util, 0, 0, 0, delay_ms);
}

void QoEIntegrationManager::OnFrameComplete(const FrameStatistics& stats) {
    if (!oscc_controller_) return;
    
    // 计算QoE指标
    double bandwidth_utilization = 0.5;
    if (!bandwidth_history_.empty()) {
         BandwidthRecord bw_record = bandwidth_history_.back();
         if (bw_record.trace_bandwidth > 0)
            bandwidth_utilization = bw_record.scaled_bandwidth / bw_record.trace_bandwidth;
    }
    
    double qoe_recv = 100.0 * bandwidth_utilization;
    double qoe_loss = 100.0 * (1.0 - (bw_changer_ ? bw_changer_->GetLossAtTime(Simulator::Now().GetMilliSeconds()) : 0.01));
    
    double ddl_miss_rate = stats.played_on_time ? 0.0 : 1.0;
    double qoe_ddl = 100.0 * (1.0 - ddl_miss_rate);
    
    double frame_delay_ms = (stats.receive_complete_time - stats.send_time).GetMilliSeconds();
    double qoe_delay = 100.0;
    if (frame_delay_ms > 400) qoe_delay = 0;
    else if (frame_delay_ms > 50) qoe_delay = 100 - (frame_delay_ms - 50) * (100.0 / 350.0);
    
    double qoe = 0.2 * qoe_recv + 0.2 * qoe_delay + 0.3 * qoe_loss + 0.3 * qoe_ddl;
    
    oscc_controller_->OnFrameComplete(stats.frame_id, qoe, bandwidth_utilization, (100-qoe_loss)/100.0, 
                                      frame_delay_ms, frame_delay_ms, ddl_miss_rate, 
                                      qoe_recv, qoe_delay, qoe_loss, qoe_ddl);
                                      
    if (rl_manager_) {
        rl_manager_->SetMu(oscc_controller_->GetCurrentMu());
    }
}

void QoEIntegrationManager::OnPacketLost(uint32_t seq_num) {
    if (oscc_controller_ && oscc_controller_->IsEnabled()) {
        oscc_controller_->UpdateLossWindow(true);
        NS_LOG_DEBUG("QoEIntegrationManager: Packet " << seq_num << " lost, updated LossWindow");
    }
}

void QoEIntegrationManager::OnTransportFeedback(const std::vector<bool>& packet_received) {
    if (!oscc_controller_ || !oscc_controller_->IsEnabled()) return;
    
    for (bool received : packet_received) {
        oscc_controller_->UpdateLossWindow(!received);
    }
    
    NS_LOG_DEBUG("QoEIntegrationManager: Processed " << packet_received.size() 
                << " feedback entries, current loss rate: " 
                << oscc_controller_->GetCurrentWindowLoss());
}

void QoEIntegrationManager::SimulateLossFromTrace() {
    if (!oscc_controller_ || !oscc_controller_->IsEnabled() || !bw_changer_) return;
    
    double trace_loss = bw_changer_->GetLossAtTime(Simulator::Now().GetMilliSeconds());
    
    static std::default_random_engine generator(std::random_device{}());
    std::uniform_real_distribution<double> distribution(0.0, 1.0);
    
    bool packet_lost = (distribution(generator) < trace_loss);
    oscc_controller_->UpdateLossWindow(packet_lost);
}

void QoEIntegrationManager::AddBandwidthRecord(Time timestamp, double trace_bw, double gcc_bw, double scaled_bw, double mu) {
    Time cleanup_threshold = timestamp - Seconds(60);
    while (!bandwidth_history_.empty() && bandwidth_history_.front().timestamp < cleanup_threshold) {
        bandwidth_history_.pop_front();
    }
    bandwidth_history_.push_back(BandwidthRecord(timestamp, trace_bw, gcc_bw, scaled_bw, mu));
}

double QoEIntegrationManager::GetNearestGccBandwidth(Time timestamp) const {
    if (bandwidth_history_.empty()) return 0.0;
    
    Time min_diff = Seconds(100);
    double bw = 0;
    for (const auto& r : bandwidth_history_) {
        Time diff = Abs(timestamp - r.timestamp);
        if (diff < min_diff) {
            min_diff = diff;
            bw = r.gcc_bandwidth;
        }
    }
    return bw;
}

double QoEIntegrationManager::GetSmoothedGccBandwidth(Time timestamp, int window_size) const {
    if (bandwidth_history_.empty()) return 0.0;
    
    std::vector<double> recent_bw;
    for (auto it = bandwidth_history_.rbegin(); 
         it != bandwidth_history_.rend() && recent_bw.size() < static_cast<size_t>(window_size); 
         ++it) {
        if (it->gcc_bandwidth > 0) {
            recent_bw.push_back(it->gcc_bandwidth);
        }
    }
    
    if (recent_bw.empty()) return 0.0;
    
    double sum = 0.0;
    for (double bw : recent_bw) {
        sum += bw;
    }
    return sum / recent_bw.size();
}

void QoEIntegrationManager::SetSkipFrameCallback(std::function<void(uint32_t)> callback) {
    skip_callback_ = callback;
}

void QoEIntegrationManager::OutputBandwidthHistory(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open bandwidth history file: " << filename);
        return;
    }
    
    file << "timestamp,trace_bandwidth,gcc_bandwidth,scaled_bandwidth,mu" << std::endl;
    for (const auto& record : bandwidth_history_) {
        file << record.timestamp.GetSeconds() << ","
             << record.trace_bandwidth << ","
             << record.gcc_bandwidth << ","
             << record.scaled_bandwidth << ","
             << record.mu_value << std::endl;
    }
    
    file.close();
    NS_LOG_INFO("Bandwidth history saved to: " << filename);
}

} // namespace oscc
