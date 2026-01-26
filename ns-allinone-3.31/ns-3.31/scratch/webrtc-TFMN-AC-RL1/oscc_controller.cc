#include "oscc_controller.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("OSCCController");

OSCCController::OSCCController() 
    : epsilon_(0.02), mu_min_(0.5), mu_max_(1.5),
      current_frame_id_(0), oscc_enabled_(true), total_adjustments_(0),
      estimated_rtt_(MilliSeconds(30)), estimated_bandwidth_bps_(20000000.0),
      current_frame_deadline_(Seconds(0)), webrtc_sender_(nullptr) {
    NS_LOG_INFO("OSCCController initialized: epsilon=" << epsilon_ 
               << ", mu_range=[" << mu_min_ << ", " << mu_max_ << "]");
    std::cout << "=== OSCCController Initialized (Rt-based Lookup) ===" << std::endl;
    std::cout << "  epsilon: " << epsilon_ << std::endl;
    std::cout << "  mu_range: [" << mu_min_ << ", " << mu_max_ << "]" << std::endl;
    std::cout << "  loss_window_size: " << LOSS_WINDOW_SIZE << std::endl;
    std::cout << "=================================================" << std::endl;
}

void OSCCController::SetParameters(double epsilon, double mu_min, double mu_max, double initial_mu) {
    epsilon_ = epsilon;
    mu_min_ = mu_min;
    mu_max_ = mu_max;
    
    NS_LOG_INFO("OSCCController parameters set: epsilon=" << epsilon_ 
               << ", mu_range=[" << mu_min_ << ", " << mu_max_ << "]"
               << ", initial_mu=" << initial_mu);
}

void OSCCController::SetEnabled(bool enabled) {
    oscc_enabled_ = enabled;
    NS_LOG_INFO("OSCCController " << (enabled ? "enabled" : "disabled"));
}

double OSCCController::GetCurrentWindowLoss() const {
    if (loss_window_.empty()) return 0.01;
    size_t lost = std::count(loss_window_.begin(), loss_window_.end(), true);
    return static_cast<double>(lost) / loss_window_.size();
}

void OSCCController::UpdateLossWindow(bool packet_lost) {
    loss_window_.push_back(packet_lost);
    while (loss_window_.size() > LOSS_WINDOW_SIZE) {
        loss_window_.pop_front();
    }
    std::cout << "当前滑动窗口大小：" << loss_window_.size() << std::endl;
    std::cout << "当前滑动窗口内容依次为：" << std::endl;
    for (const auto& item : loss_window_) {
        std::cout << item << " ";
    }
    std::cout << std::endl;
}

double OSCCController::GetMuForPacket(uint32_t frame_id, uint32_t Rt) {
    if (!oscc_enabled_) {
        return 1.0;
    }
    
    if (frame_id != current_frame_id_) {
        OnNewFrameStart(frame_id);
    }
    
    auto cache_it = current_frame_cache_.find(Rt);
    if (cache_it != current_frame_cache_.end()) {
        NS_LOG_DEBUG("OSCC: Cache hit for Rt=" << Rt << ", mu=" << cache_it->second);
        return cache_it->second;
    }
    
    double L_curr = GetCurrentWindowLoss();
    double new_mu = CalculateMuFromHistory(Rt, L_curr);
    
    RecordMuChange(frame_id, Rt, new_mu, L_curr);
    
    current_frame_cache_[Rt] = new_mu;
    
    ApplyMuToSender(new_mu);
    
    NS_LOG_DEBUG("OSCC: GetMuForPacket frame=" << frame_id << ", Rt=" << Rt 
                << ", L_curr=" << L_curr << ", new_mu=" << new_mu);
    
    return new_mu;
}

void OSCCController::OnRtGroupComplete(uint32_t frame_id, uint32_t Rt, double group_loss) {
    if (!oscc_enabled_) {
        return;
    }
    
    NS_LOG_DEBUG("OSCC: Rt group complete - frame=" << frame_id << ", Rt=" << Rt 
               << ", loss=" << group_loss);
}

void OSCCController::OnFrameComplete(uint32_t frame_id, double frame_qoe,
                                     double bandwidth_utilization, double loss_rate,
                                     double delay_metric, double delay_avg, double ddl_miss_rate,
                                     double qoe_recv, double qoe_delay,
                                     double qoe_loss, double qoe_ddl) {
    if (!oscc_enabled_) {
        return;
    }
    
    FrameQoEDetail detail;
    detail.frame_id = frame_id;
    detail.bandwidth_utilization = bandwidth_utilization;
    detail.loss_rate = loss_rate;
    detail.delay_metric = delay_metric;
    detail.delay_avg = delay_avg;
    detail.ddl_miss_rate = ddl_miss_rate;
    detail.qoe_recv = qoe_recv;
    detail.qoe_delay = qoe_delay;
    detail.qoe_loss = qoe_loss;
    detail.qoe_ddl = qoe_ddl;
    detail.qoe = frame_qoe;
    detail.mu = GetCurrentMu();
    detail.timestamp = Simulator::Now();
    
    frame_qoe_details_[frame_id] = detail;
    frame_qoe_history_[frame_id] = frame_qoe;
    
    NS_LOG_INFO("OSCC: Frame complete - frame=" << frame_id << ", QoE=" << frame_qoe);
}

double OSCCController::GetCurrentMu() const {
    if (current_frame_cache_.empty()) {
        return history_map_.empty() ? 1.0 : history_map_.begin()->second.mu;
    }
    return current_frame_cache_.begin()->second;
}

void OSCCController::OutputMuTrace(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open OSCC mu trace file: " << filename);
        return;
    }
    
    file << "timestamp,frame_id,Rt,old_mu,new_mu,trigger_type,loss_rate,qoe" << std::endl;
    for (const auto& record : mu_change_records_) {
        file << record.timestamp.GetSeconds() << ","
             << record.frame_id << ","
             << record.Rt_value << ","
             << record.old_mu << ","
             << record.new_mu << ","
             << record.trigger_type << ","
             << record.loss_rate << ","
             << record.qoe << std::endl;
    }
    
    file.close();
    NS_LOG_INFO("OSCC mu trace saved to: " << filename << " with " 
               << mu_change_records_.size() << " records");
}

void OSCCController::OutputFrameQoE(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open OSCC QoE file: " << filename);
        return;
    }
    
    file << "frame_id,bandwidth_utilization,loss_rate,delay_metric,delay_avg,ddl_miss_rate,"
         << "qoe_recv,qoe_delay,qoe_loss,qoe_ddl,qoe,mu,timestamp_s" << std::endl;
    for (const auto& pair : frame_qoe_details_) {
        const FrameQoEDetail& detail = pair.second;
        file << detail.frame_id << ","
             << detail.bandwidth_utilization << ","
             << detail.loss_rate << ","
             << detail.delay_metric << ","
             << detail.delay_avg << ","
             << detail.ddl_miss_rate << ","
             << detail.qoe_recv << ","
             << detail.qoe_delay << ","
             << detail.qoe_loss << ","
             << detail.qoe_ddl << ","
             << detail.qoe << ","
             << detail.mu << ","
             << detail.timestamp.GetSeconds() << std::endl;
    }
    
    file.close();
    NS_LOG_INFO("OSCC frame QoE saved to: " << filename);
}

void OSCCController::SetRtEstimationParams(Time rtt, double bandwidth_bps) {
    estimated_rtt_ = rtt;
    estimated_bandwidth_bps_ = bandwidth_bps;
}

void OSCCController::SetCurrentFrameDeadline(Time deadline) {
    current_frame_deadline_ = deadline;
}

uint32_t OSCCController::EstimateRt(uint32_t packet_size) {
    Time now = Simulator::Now();
    Time T_remain = current_frame_deadline_ - now;
    
    if (T_remain <= Seconds(0) || estimated_bandwidth_bps_ <= 0) {
        return 0;
    }
    
    double send_time_s = (packet_size * 8.0) / estimated_bandwidth_bps_;
    Time available = T_remain - Seconds(send_time_s);
    
    if (available <= Seconds(0) || estimated_rtt_ <= Seconds(0)) {
        return 0;
    }
    
    uint32_t Rt = static_cast<uint32_t>(available.GetSeconds() / estimated_rtt_.GetSeconds());
    
    NS_LOG_DEBUG("OSCC EstimateRt: T_remain=" << T_remain.GetSeconds() 
                << "s, send_time=" << send_time_s << "s, RTT=" << estimated_rtt_.GetSeconds()
                << "s, Rt=" << Rt);
    
    return Rt;
}

double OSCCController::GetMuForPacketWithEstimation(uint32_t frame_id, uint32_t packet_size) {
    uint32_t Rt = EstimateRt(packet_size);
    return GetMuForPacket(frame_id, Rt);
}

void OSCCController::SetWebrtcSender(Ptr<WebrtcSender> sender) {
    webrtc_sender_ = sender;
    if (sender) {
        std::cout << "[OSCCController] WebrtcSender set, mu changes will be applied directly!" << std::endl;
    }
}

double OSCCController::CalculateMuFromHistory(uint32_t Rt, double L_curr) {
    if (history_map_.empty()) {
        NS_LOG_DEBUG("OSCC: HistoryMap empty, using default mu=1.0");
        return 1.0;
    }
    
    auto it = history_map_.find(Rt);
    
    // 情况 1: 精确命中
    if (it != history_map_.end()) {
        double new_mu = AdjustMuByLoss(it->second.mu, it->second.recorded_loss, L_curr);
        NS_LOG_DEBUG("OSCC: Exact match for Rt=" << Rt 
                    << ", L_prev=" << it->second.recorded_loss 
                    << ", L_curr=" << L_curr << ", mu: " << it->second.mu << " -> " << new_mu);
        return new_mu;
    }
    
    uint32_t min_rt = history_map_.begin()->first;
    uint32_t max_rt = history_map_.rbegin()->first;
    
    // 情况 2: 大于最大值 (激进策略)
    if (Rt > max_rt) {
        double mu_max_hist = history_map_.rbegin()->second.mu;
        double new_mu = ClipMu(mu_max_hist + epsilon_);
        NS_LOG_DEBUG("OSCC: Rt=" << Rt << " > max_rt=" << max_rt 
                    << ", aggressive: mu " << mu_max_hist << " -> " << new_mu);
        return new_mu;
    }
    
    // 情况 3: 小于最小值 (保守策略)
    if (Rt < min_rt) {
        double mu_min_hist = history_map_.begin()->second.mu;
        double new_mu = ClipMu(mu_min_hist - epsilon_);
        NS_LOG_DEBUG("OSCC: Rt=" << Rt << " < min_rt=" << min_rt 
                    << ", conservative: mu " << mu_min_hist << " -> " << new_mu);
        return new_mu;
    }
    
    // 情况 4: 位于区间内，找 lower_bound 邻居
    auto lower = history_map_.lower_bound(Rt);
    if (lower != history_map_.begin()) {
        --lower;
        double new_mu = AdjustMuByLoss(lower->second.mu, lower->second.recorded_loss, L_curr);
        NS_LOG_DEBUG("OSCC: Rt=" << Rt << " in range, neighbor Rt=" << lower->first 
                    << ", L_prev=" << lower->second.recorded_loss 
                    << ", L_curr=" << L_curr << ", mu: " << lower->second.mu << " -> " << new_mu);
        return new_mu;
    }
    
    NS_LOG_DEBUG("OSCC: Fallback to default mu=1.0");
    return 1.0;
}

double OSCCController::AdjustMuByLoss(double mu_prev, double L_prev, double L_curr) {
    if (L_curr < L_prev) {
        return ClipMu(mu_prev + epsilon_);
    } else if (L_curr > L_prev) {
        return ClipMu(mu_prev - epsilon_);
    }
    return mu_prev;
}

void OSCCController::OnNewFrameStart(uint32_t new_frame_id) {
    if (!current_frame_cache_.empty()) {
        history_map_.clear();
        double L_curr = GetCurrentWindowLoss();
        for (const auto& entry : current_frame_cache_) {
            history_map_[entry.first] = RtHistoryEntry(entry.second, L_curr);
        }
        
        NS_LOG_DEBUG("OSCC: Frame " << current_frame_id_ << " -> " << new_frame_id 
                    << ", updated HistoryMap with " << history_map_.size() << " entries");
        
        std::cout << "[OSCC] New frame " << new_frame_id << " started, HistoryMap updated with " 
                  << history_map_.size() << " Rt entries from frame " << current_frame_id_ << std::endl;
    }
    
    current_frame_cache_.clear();
    current_frame_id_ = new_frame_id;
}

void OSCCController::RecordMuChange(uint32_t frame_id, uint32_t Rt, double new_mu, double loss_rate) {
    double old_mu = 1.0;
    auto hist_it = history_map_.find(Rt);
    if (hist_it != history_map_.end()) {
        old_mu = hist_it->second.mu;
    } else if (!history_map_.empty()) {
        auto lower = history_map_.lower_bound(Rt);
        if (lower != history_map_.begin()) {
            --lower;
            old_mu = lower->second.mu;
        } else if (Rt > history_map_.rbegin()->first) {
            old_mu = history_map_.rbegin()->second.mu;
        } else {
            old_mu = history_map_.begin()->second.mu;
        }
    }
    
    if (std::abs(old_mu - new_mu) > 0.001) {
        total_adjustments_++;
        MuChangeRecord record(Simulator::Now(), frame_id, Rt, old_mu, new_mu,
                             "rt_lookup", loss_rate, 0.0);
        mu_change_records_.push_back(record);
        
        std::cout << "[OSCC] Rt-lookup adjustment: frame=" << frame_id 
                 << ", Rt=" << Rt << ", mu: " << old_mu << " -> " << new_mu
                 << ", loss=" << loss_rate << std::endl;
    }
}

double OSCCController::ClipMu(double mu) const {
    return std::max(mu_min_, std::min(mu_max_, mu));
}

void OSCCController::ApplyMuToSender(double new_mu) {
    if (webrtc_sender_) {
        webrtc_sender_->UpdateMuDynamic(new_mu);
        std::cout << "[OSCC->Sender] Applied mu=" << new_mu 
                  << " directly to WebrtcSender at " << Simulator::Now().GetSeconds() << "s" << std::endl;
    } else {
        std::cout << "[OSCC->Sender] WARNING: webrtc_sender_ is null, cannot apply mu!" << std::endl;
    }
}

} // namespace oscc
