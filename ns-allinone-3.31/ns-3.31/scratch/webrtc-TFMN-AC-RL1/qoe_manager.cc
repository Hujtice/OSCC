#include "qoe_manager.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <fstream>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("QoEManager");

QoEIntegrationManager::QoEIntegrationManager() 
    : rl_manager_(nullptr), bw_changer_(nullptr), 
      webrtc_sender_(nullptr), mu_learner_(nullptr), use_learner_(false) {}

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

    // 真实链路带宽（来自 trace 文件，用于 Rt 计算和奖励分母）
    double real_trace_bw = 20000000.0;
    double trace_rtt = 30.0;

    if (bw_changer_) {
        uint32_t ts = now.GetMilliSeconds();
        real_trace_bw = bw_changer_->GetTraceBandwidthAtTime(ts);
        trace_rtt = bw_changer_->GetRTTAtTime(ts);
    }

    // 实际观测丢包率（来自 seq 号差值的滑动窗口，P1）
    double observed_loss = GetObservedLossRate();

    rl_manager_->UpdateNetworkState(delay_ms, observed_loss, MilliSeconds(trace_rtt));

    // GCC 估计带宽（用于奖励分子）
    double gcc_bw = GetNearestGccBandwidth(now);
    if (gcc_bw <= 0) gcc_bw = real_trace_bw * 0.7;

    // Rt 用真实链路带宽计算（P2）
    uint32_t Rt = rl_manager_->CalculateTransmissionOpportunities(
        now, frame_stats.playout_deadline, info.packet_size, real_trace_bw);

    double mu = rl_manager_->GetCurrentMu();
    double old_mu = mu;
    bool mu_changed = false;

    // 使用MuLearner (Gym-based)，RL 状态用观测丢包（P1）
    if (use_learner_ && mu_learner_) {
        MuState state(static_cast<double>(Rt), observed_loss);
        MuAction action = mu_learner_->Act(state);
        double learner_mu = action.mu;

        if (std::abs(learner_mu - mu) > 0.001) {
            mu = learner_mu;
            mu_changed = true;
            rl_manager_->SetMu(mu);
            if (webrtc_sender_) webrtc_sender_->UpdateMuDynamic(mu);

            NS_LOG_DEBUG("MuLearner: Applied mu=" << mu << " for Rt=" << Rt << ", loss=" << observed_loss);
        }
    }

    // 计算奖励：gcc_bw 做分子、real_trace_bw 做分母（P3）
    // uint32_t packet_idx = info.is_first_packet ? 0 : 1;
    uint32_t packet_idx = info.seq;

    double miss_deadline_time = 0.0;
    if (now > frame_stats.playout_deadline) {
        miss_deadline_time = (now - frame_stats.playout_deadline).GetSeconds();
    }

    double reward = rl_manager_->CalculateReward(
        mu, gcc_bw, real_trace_bw, delay_ms, observed_loss,
        miss_deadline_time, Rt, rl_manager_->GetLastPacketRt(),
        info.frame_id, packet_idx);

    double bw_util;
    if ((gcc_bw * mu) > real_trace_bw) {
        bw_util = 1.0;
    } else {
        bw_util = (gcc_bw * mu) / real_trace_bw;
    }

    // 接收端真实吞吐量：滑动窗口内 sum(bytes)*8 / window_seconds
    throughput_window_.push_back(std::make_pair(now, info.packet_size));
    Time window_end = now - Seconds(kThroughputWindowSeconds);
    while (!throughput_window_.empty() && throughput_window_.front().first < window_end) {
        throughput_window_.pop_front();
    }
    double real_throughput_bps = ComputeRealThroughputBps(now);
    double scaled_bw = mu * gcc_bw;

    rl_manager_->RecordPacketState(info.frame_id, packet_idx, mu, Rt, observed_loss, reward,
                                   send_time, now, frame_stats.playout_deadline,
                                   bw_util, 0, 0, 0, delay_ms,
                                   real_throughput_bps, gcc_bw, real_trace_bw, scaled_bw);

    // Mu trace: record when mu actually changed (for _mu_trace.csv)
    if (mu_changed) {
        mu_change_records_.push_back(
            MuChangeRecord(now, info.frame_id, Rt, old_mu, mu, observed_loss, reward));
    }

    // Per-frame accumulation for _frame_qoe.csv
    FrameAccumulator& acc = frame_accumulator_[info.frame_id];
    acc.sum_bw_util += bw_util;
    acc.sum_delay += delay_ms;
    acc.sum_reward += reward;
    acc.sum_mu += mu;
    acc.sum_loss += observed_loss;
    acc.sum_real_throughput_bps += real_throughput_bps;
    acc.sum_gcc_bw_bps += gcc_bw;
    acc.sum_trace_bw_bps += real_trace_bw;
    acc.sum_scaled_bw_bps += scaled_bw;
    acc.count++;
}

void QoEIntegrationManager::OnFrameComplete(const FrameStatistics& stats) {
    auto it = frame_accumulator_.find(stats.frame_id);
    if (it == frame_accumulator_.end() || it->second.count == 0) {
        return;
    }
    FrameAccumulator& acc = it->second;
    FrameQoESummary summary;
    summary.frame_id = stats.frame_id;
    summary.bandwidth_utilization = acc.sum_bw_util / acc.count;
    summary.loss_rate = acc.sum_loss / acc.count;
    summary.delay_avg = acc.sum_delay / acc.count;
    summary.mu = acc.sum_mu / acc.count;
    summary.reward_avg = acc.sum_reward / acc.count;
    summary.timestamp = Simulator::Now();
    summary.real_throughput_bps = acc.sum_real_throughput_bps / acc.count;
    summary.avg_gcc_bw_bps = acc.sum_gcc_bw_bps / acc.count;
    summary.avg_trace_bw_bps = acc.sum_trace_bw_bps / acc.count;
    summary.avg_scaled_bw_bps = acc.sum_scaled_bw_bps / acc.count;
    frame_qoe_map_[stats.frame_id] = summary;
    frame_accumulator_.erase(it);
}

void QoEIntegrationManager::OnPacketLost(uint32_t seq_num) {
    NS_LOG_DEBUG("QoEIntegrationManager: Packet " << seq_num << " lost");
}

void QoEIntegrationManager::OnTransportFeedback(const std::vector<bool>& packet_received) {
    NS_LOG_DEBUG("QoEIntegrationManager: Processed " << packet_received.size() << " feedback entries");
}

void QoEIntegrationManager::SimulateLossFromTrace() {
    // Simulated loss handling if needed
}

void QoEIntegrationManager::ReportPacketSeq(uint32_t seq) {
    if (first_packet_) {
        last_seq_ = seq;
        first_packet_ = false;
        return;
    }
    uint32_t gap = seq - last_seq_;
    uint32_t lost = (gap > 1) ? (gap - 1) : 0;
    uint32_t received = 1;
    uint32_t expected = lost + received;

    window_received_ += received;
    window_expected_ += expected;
    loss_window_.push_back({received, expected});

    while (loss_window_.size() > kLossWindowSize) {
        window_received_ -= loss_window_.front().first;
        window_expected_ -= loss_window_.front().second;
        loss_window_.pop_front();
    }
    last_seq_ = seq;
}

double QoEIntegrationManager::GetObservedLossRate() const {
    if (window_expected_ == 0) return 0.0;
    return 1.0 - static_cast<double>(window_received_) / window_expected_;
}

double QoEIntegrationManager::ComputeRealThroughputBps(Time now) const {
    if (throughput_window_.empty() || kThroughputWindowSeconds <= 0.0) return 0.0;
    uint64_t total_bytes = 0;
    for (const auto& entry : throughput_window_) {
        total_bytes += entry.second;
    }
    return (total_bytes * 8.0) / kThroughputWindowSeconds;
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

void QoEIntegrationManager::OutputMuTrace(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open mu trace file: " << filename);
        return;
    }
    file << "timestamp,frame_id,Rt,old_mu,new_mu,loss_rate,reward" << std::endl;
    for (const auto& record : mu_change_records_) {
        file << record.timestamp.GetSeconds() << ","
             << record.frame_id << ","
             << record.Rt_value << ","
             << record.old_mu << ","
             << record.new_mu << ","
             << record.loss_rate << ","
             << record.reward << std::endl;
    }
    file.close();
    NS_LOG_INFO("Mu trace saved to: " << filename << " with " << mu_change_records_.size() << " records");
}

void QoEIntegrationManager::OutputFrameQoE(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open frame QoE file: " << filename);
        return;
    }
    file << "frame_id,bandwidth_utilization,loss_rate,delay_avg,mu,reward_avg,timestamp_s,"
         << "real_throughput_bps,avg_gcc_bw_bps,avg_trace_bw_bps,avg_scaled_bw_bps" << std::endl;
    for (const auto& pair : frame_qoe_map_) {
        const FrameQoESummary& s = pair.second;
        file << s.frame_id << ","
             << s.bandwidth_utilization << ","
             << s.loss_rate << ","
             << s.delay_avg << ","
             << s.mu << ","
             << s.reward_avg << ","
             << s.timestamp.GetSeconds() << ","
             << s.real_throughput_bps << ","
             << s.avg_gcc_bw_bps << ","
             << s.avg_trace_bw_bps << ","
             << s.avg_scaled_bw_bps << std::endl;
    }
    file.close();
    NS_LOG_INFO("Frame QoE saved to: " << filename);
}

} // namespace oscc
