#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <utility>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <set>
#include <functional>
#include <cmath>
#include <numeric>
#include <deque>
#include <random>
#include "ns3/webrtc-defines.h"
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/log.h"
#include "ns3/ex-webrtc-module.h"
#include "ns3/frame-playout-manager.h"
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <time.h>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace ns3;
using namespace std;


NS_LOG_COMPONENT_DEFINE ("webrtc-static");

const uint32_t DEFAULT_PACKET_SIZE = 1500;
const uint32_t kBwUnit=1000000;

uint64_t get_os_millis()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// VideoFrame and VideoTraceManager removed - using real WebRTC frames via FramePlayoutManager


// 包级别的状态记录
struct PacketStateRecord {
    uint32_t frame_id;
    uint32_t packet_index;
    double mu_used;
    uint32_t Rt;
    double loss_rate;
    double reward;
    Time send_time;
    Time recivied_time;  // 新增：接收时间
    Time deadline;
    double bandwidth_utilization;
    double p_delay_value;
    double p_loss_value;
    double p_mddl_value;
    double current_delay;  // 新增：实际延迟
    
    PacketStateRecord() : frame_id(0), packet_index(0), mu_used(1.0), 
                         Rt(0), loss_rate(0.0), reward(0.0),
                         bandwidth_utilization(0.0), p_delay_value(0.0),
                         p_loss_value(0.0), p_mddl_value(0.0), current_delay(0.0) {}
};

// 新增：Rt分组奖励记录结构
struct RtGroupRewardRecord {
    uint32_t frame_id;
    uint32_t Rt_value;
    double loss_rate;
    double mu_used;       // 新增：该Rt组中使用的μ值
    double avg_reward;
    uint32_t packet_count;
    Time start_time;
    Time end_time;
    
    RtGroupRewardRecord() : frame_id(0), Rt_value(0), loss_rate(0.0), 
                           mu_used(1.0), avg_reward(0.0), packet_count(0), 
                           start_time(Seconds(0)), end_time(Seconds(0)) {}
    
    RtGroupRewardRecord(uint32_t fid, uint32_t rt, double loss, double mu, 
                       double reward, uint32_t count, Time start, Time end)
        : frame_id(fid), Rt_value(rt), loss_rate(loss), mu_used(mu),
          avg_reward(reward), packet_count(count), 
          start_time(start), end_time(end) {}
};

// 新增：Trace数据结构，包含四列数据
struct TraceData {
    Time timestamp;
    double bandwidth;  // 带宽数据
    double rtt;        // RTT数据（毫秒）
    double loss;       // loss数据
    
    TraceData() : timestamp(Seconds(0)), bandwidth(0.0), rtt(0.0), loss(0.0) {}
    
    TraceData(Time ts, double bw, double rt, double l) 
        : timestamp(ts), bandwidth(bw), rtt(rt), loss(l) {}
};

// ============================================================================
// OSCCController 类 - OSCC算法核心控制器
// 实现基于Rt的精细化历史查表机制进行动态μ调整
// ============================================================================
class OSCCController {
public:
    // 历史状态记录结构 (用于 HistoryMap)
    struct RtHistoryEntry {
        double mu;
        double recorded_loss;
        
        RtHistoryEntry(double m = 1.0, double l = 0.01) : mu(m), recorded_loss(l) {}
    };
    
    // μ变化记录结构 (用于日志输出)
    struct MuChangeRecord {
        Time timestamp;
        uint32_t frame_id;
        uint32_t Rt_value;
        double old_mu;
        double new_mu;
        std::string trigger_type;  // "rt_lookup"
        double loss_rate;
        double qoe;
        
        MuChangeRecord() : timestamp(Seconds(0)), frame_id(0), Rt_value(0),
                          old_mu(1.0), new_mu(1.0), trigger_type(""), 
                          loss_rate(0.0), qoe(0.0) {}
        
        MuChangeRecord(Time ts, uint32_t fid, uint32_t rt, double old_m, double new_m,
                      const std::string& type, double loss, double q)
            : timestamp(ts), frame_id(fid), Rt_value(rt), old_mu(old_m), new_mu(new_m),
              trigger_type(type), loss_rate(loss), qoe(q) {}
    };

    struct FrameQoEDetail {
        uint32_t frame_id;
        double bandwidth_utilization;
        double loss_rate;
        double delay_metric;
        double delay_avg;        // 新增：平均延迟
        double ddl_miss_rate;
        double qoe_recv;
        double qoe_delay;
        double qoe_loss;
        double qoe_ddl;
        double qoe;
        double mu;
        Time timestamp;
        
        FrameQoEDetail() : frame_id(0), bandwidth_utilization(0.0), loss_rate(0.0),
                          delay_metric(0.0), delay_avg(0.0), ddl_miss_rate(0.0), qoe_recv(0.0),
                          qoe_delay(0.0), qoe_loss(0.0), qoe_ddl(0.0), qoe(0.0),
                          mu(1.0), timestamp(Seconds(0)) {}
    };
    
    // 滑动窗口大小常量
    static const size_t LOSS_WINDOW_SIZE = 100;

    OSCCController() 
        : epsilon_(0.02), mu_min_(0.5), mu_max_(1.5),
          current_frame_id_(0), oscc_enabled_(true), total_adjustments_(0) {
        NS_LOG_INFO("OSCCController initialized: epsilon=" << epsilon_ 
                   << ", mu_range=[" << mu_min_ << ", " << mu_max_ << "]");
        std::cout << "=== OSCCController Initialized (Rt-based Lookup) ===" << std::endl;
        std::cout << "  epsilon: " << epsilon_ << std::endl;
        std::cout << "  mu_range: [" << mu_min_ << ", " << mu_max_ << "]" << std::endl;
        std::cout << "  loss_window_size: " << LOSS_WINDOW_SIZE << std::endl;
        std::cout << "  startup_policy: frames 0-1 use GCC only (mu=1.0), OSCC starts at frame 2" << std::endl;
        std::cout << "=================================================" << std::endl;
    }
    
    // 设置OSCC参数
    void SetParameters(double epsilon, double mu_min, double mu_max, double initial_mu) {
        epsilon_ = epsilon;
        mu_min_ = mu_min;
        mu_max_ = mu_max;
        
        NS_LOG_INFO("OSCCController parameters set: epsilon=" << epsilon_ 
                   << ", mu_range=[" << mu_min_ << ", " << mu_max_ << "]"
                   << ", initial_mu=" << initial_mu);
    }
    
    // 启用/禁用OSCC
    void SetEnabled(bool enabled) {
        oscc_enabled_ = enabled;
        NS_LOG_INFO("OSCCController " << (enabled ? "enabled" : "disabled"));
    }
    
    bool IsEnabled() const { return oscc_enabled_; }
    
    // ============================================================================
    // 滑动窗口丢包率相关方法
    // ============================================================================
    
    // 获取当前滑动窗口的丢包率
    double GetCurrentWindowLoss() const {
        if (loss_window_.empty()) return 0.01;  // 默认值
        size_t lost = std::count(loss_window_.begin(), loss_window_.end(), true);
        return static_cast<double>(lost) / loss_window_.size();
    }
    
    // 更新滑动窗口 (RTCP 反馈时调用)
    void UpdateLossWindow(bool packet_lost) {
        loss_window_.push_back(packet_lost);
        while (loss_window_.size() > LOSS_WINDOW_SIZE) {
            loss_window_.pop_front();
        }
        // 打印滑动窗口大小和内容
        std::cout << "当前滑动窗口大小：" << loss_window_.size() << std::endl;
        std::cout << "当前滑动窗口内容依次为：" << std::endl;
        for (const auto& item : loss_window_) {
            std::cout << item << " ";
        }
        std::cout << std::endl;
    }
    
    // ============================================================================
    // 核心查表逻辑 - 基于Rt的mu决策
    // ============================================================================
    
    // 获取当前μ值（供每个包使用）- 基于Rt查表机制
    double GetMuForPacket(uint32_t frame_id, uint32_t Rt) {
        if (!oscc_enabled_) {
            return 1.0;  // 禁用时返回默认值
        }
        
        // 前两帧（frame_id < 2）使用原始GCC速率，不进行OSCC自适应调整
        if (frame_id < 2) {
            NS_LOG_DEBUG("OSCC: Frame " << frame_id << " < 2, using default mu=1.0 (GCC only)");
            std::cout << "[OSCC] Frame " << frame_id << " < 2, using default mu=1.0 (original GCC rate)" << std::endl;
            return 1.0;
        }
        
        // 帧边界检测：新帧开始时更新 HistoryMap
        if (frame_id != current_frame_id_) {
            OnNewFrameStart(frame_id);
        }
        
        // 步骤 A: 检查帧内缓存
        auto cache_it = current_frame_cache_.find(Rt);
        if (cache_it != current_frame_cache_.end()) {
            NS_LOG_DEBUG("OSCC: Cache hit for Rt=" << Rt << ", mu=" << cache_it->second);
            return cache_it->second;  // 缓存命中
        }
        
        // 步骤 B: 计算新 mu
        double L_curr = GetCurrentWindowLoss();
        double new_mu = CalculateMuFromHistory(Rt, L_curr);
        
        // 记录mu变化（用于日志）
        RecordMuChange(frame_id, Rt, new_mu, L_curr);
        
        // 存入缓存
        current_frame_cache_[Rt] = new_mu;
        
        // 应用新mu到发送端
        ApplyMuToSender(new_mu);
        
        NS_LOG_DEBUG("OSCC: GetMuForPacket frame=" << frame_id << ", Rt=" << Rt 
                    << ", L_curr=" << L_curr << ", new_mu=" << new_mu);
        
        return new_mu;
    }
    
    // Rt组完成时调用 - 新架构下不再需要此方法进行mu调整，保留用于兼容性
    void OnRtGroupComplete(uint32_t frame_id, uint32_t Rt, double group_loss) {
        if (!oscc_enabled_) {
            return;
        }
        
        // 仅记录日志，不再进行mu调整（mu已在GetMuForPacket中确定）
        NS_LOG_DEBUG("OSCC: Rt group complete - frame=" << frame_id << ", Rt=" << Rt 
                   << ", loss=" << group_loss);
    }
    
    // 帧完成时调用 - 仅记录统计信息
    void OnFrameComplete(uint32_t frame_id, double frame_qoe,
                      double bandwidth_utilization, double loss_rate,
                      double delay_metric, double delay_avg, double ddl_miss_rate,
                      double qoe_recv, double qoe_delay,
                      double qoe_loss, double qoe_ddl) {
        if (!oscc_enabled_) {
            return;
        }
        
        // 保存当前帧QoE详细信息（仅用于统计，不影响mu调整）
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
        detail.mu = GetCurrentMu();  // 获取最近使用的mu
        detail.timestamp = Simulator::Now();
        
        frame_qoe_details_[frame_id] = detail;
        frame_qoe_history_[frame_id] = frame_qoe;
        
        NS_LOG_INFO("OSCC: Frame complete - frame=" << frame_id << ", QoE=" << frame_qoe);
    }
    
    // 获取当前μ（返回最近使用的mu，用于日志/统计）
    double GetCurrentMu() const {
        if (current_frame_cache_.empty()) {
            return history_map_.empty() ? 1.0 : history_map_.begin()->second.mu;
        }
        return current_frame_cache_.begin()->second;
    }
    
    // 获取μ变化记录
    const std::vector<MuChangeRecord>& GetMuChangeRecords() const {
        return mu_change_records_;
    }
    
    // 获取帧QoE历史
    const std::map<uint32_t, double>& GetFrameQoEHistory() const {
        return frame_qoe_history_;
    }
    
    // 输出μ变化日志到文件
    void OutputMuTrace(const std::string& filename) const {
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
    
    // 输出帧QoE日志到文件
    void OutputFrameQoE(const std::string& filename) const {
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
    
    // 获取统计信息
    uint32_t GetTotalAdjustments() const { return total_adjustments_; }
    
    // ============================================================================
    // 发送端 Rt 预估
    // ============================================================================
    
    // 设置用于 Rt 预估的参数
    void SetRtEstimationParams(Time rtt, double bandwidth_bps) {
        estimated_rtt_ = rtt;
        estimated_bandwidth_bps_ = bandwidth_bps;
    }
    
    // 设置帧截止时间（由发送端帧管理器调用）
    void SetCurrentFrameDeadline(Time deadline) {
        current_frame_deadline_ = deadline;
    }
    
    // 发送端预估 Rt（在发包前调用）
    uint32_t EstimateRt(uint32_t packet_size) {
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
    
    // 便捷方法：预估 Rt 并获取对应的 mu
    double GetMuForPacketWithEstimation(uint32_t frame_id, uint32_t packet_size) {
        uint32_t Rt = EstimateRt(packet_size);
        return GetMuForPacket(frame_id, Rt);
    }
    
private:
    // ============================================================================
    // 核心查表算法
    // ============================================================================
    
    // 根据历史计算 mu
    double CalculateMuFromHistory(uint32_t Rt, double L_curr) {
        if (history_map_.empty()) {
            NS_LOG_DEBUG("OSCC: HistoryMap empty, using default mu=1.0");
            return 1.0;  // 初始默认值
        }
        
        auto it = history_map_.find(Rt);
        
        // 情况 1: 精确命中
        if (it != history_map_.end()) {
            double new_mu = AdjustMuByLoss(it->second.mu, it->second.recorded_loss, L_curr);
            NS_LOG_DEBUG("OSCC: Exact match for Rt=" << Rt 
                        << ", L_prev=" << it->second.recorded_loss 
                        << ", L_curr=" << L_curr << ", mu: " << it->second.mu << " -> " << new_mu);
            return new_mu;
            // return 1.0;
        }
        
        // 获取历史 Rt 范围
        uint32_t min_rt = history_map_.begin()->first;
        uint32_t max_rt = history_map_.rbegin()->first;
        
        // 情况 2: 大于最大值 (激进策略)
        if (Rt > max_rt) {
            double mu_max_hist = history_map_.rbegin()->second.mu;
            double new_mu = ClipMu(mu_max_hist + epsilon_);
            NS_LOG_DEBUG("OSCC: Rt=" << Rt << " > max_rt=" << max_rt 
                        << ", aggressive: mu " << mu_max_hist << " -> " << new_mu);
            return new_mu;
            // return 1.0;
        }
        
        // 情况 3: 小于最小值 (保守策略)
        if (Rt < min_rt) {
            double mu_min_hist = history_map_.begin()->second.mu;
            double new_mu = ClipMu(mu_min_hist - epsilon_);
            NS_LOG_DEBUG("OSCC: Rt=" << Rt << " < min_rt=" << min_rt 
                        << ", conservative: mu " << mu_min_hist << " -> " << new_mu);
            return new_mu;
            // return 1.0;
        }
        
        // 情况 4: 位于区间内，找 lower_bound 邻居
        auto lower = history_map_.lower_bound(Rt);
        if (lower != history_map_.begin()) {
            --lower;  // 找到比 Rt 小的最大邻居
            double new_mu = AdjustMuByLoss(lower->second.mu, lower->second.recorded_loss, L_curr);
            NS_LOG_DEBUG("OSCC: Rt=" << Rt << " in range, neighbor Rt=" << lower->first 
                        << ", L_prev=" << lower->second.recorded_loss 
                        << ", L_curr=" << L_curr << ", mu: " << lower->second.mu << " -> " << new_mu);
            return new_mu;
            // return 1.0;
        }
        
        NS_LOG_DEBUG("OSCC: Fallback to default mu=1.0");
        return 1.0;  // 回退默认值
    }
    
    // 根据丢包率变化调整 mu
    double AdjustMuByLoss(double mu_prev, double L_prev, double L_curr) {
        if (L_curr < L_prev) {
            return ClipMu(mu_prev + epsilon_);  // 情况变好，更激进
        } else if (L_curr > L_prev) {
            return ClipMu(mu_prev - epsilon_);  // 情况变差，更保守
        }
        return mu_prev;  // 相等，保持不变
    }
    
    // 帧边界处理：将当前帧缓存更新到全局历史表
    void OnNewFrameStart(uint32_t new_frame_id) {
        // 将当前帧缓存更新到全局历史表（不清空历史表）
        if (!current_frame_cache_.empty()) {
            double L_curr = GetCurrentWindowLoss();
            int updated_count = 0;
            int new_count = 0;
            
            for (const auto& entry : current_frame_cache_) {
                uint32_t Rt = entry.first;
                double mu = entry.second;
                
                // 更新或插入HistoryMap中的条目
                auto it = history_map_.find(Rt);
                if (it != history_map_.end()) {
                    // 已存在该Rt，更新mu和loss
                    it->second.mu = mu;
                    it->second.recorded_loss = L_curr;
                    updated_count++;
                } else {
                    // 新的Rt值，插入到HistoryMap
                    history_map_[Rt] = RtHistoryEntry(mu, L_curr);
                    new_count++;
                }
            }
            
            NS_LOG_DEBUG("OSCC: Frame " << current_frame_id_ << " -> " << new_frame_id 
                        << ", HistoryMap updated: " << updated_count << " existing, " 
                        << new_count << " new, total=" << history_map_.size() << " entries");
            
            std::cout << "[OSCC] New frame " << new_frame_id << " started, HistoryMap updated: " 
                      << updated_count << " existing Rt entries updated, " 
                      << new_count << " new Rt entries added, total=" << history_map_.size() 
                      << " (global accumulation)" << std::endl;
        }
        
        // 清空缓存，准备新帧
        current_frame_cache_.clear();
        current_frame_id_ = new_frame_id;
    }
    
    // 记录mu变化（用于日志输出）
    void RecordMuChange(uint32_t frame_id, uint32_t Rt, double new_mu, double loss_rate) {
        // 获取旧的mu值用于记录
        double old_mu = 1.0;
        auto hist_it = history_map_.find(Rt);
        if (hist_it != history_map_.end()) {
            old_mu = hist_it->second.mu;
        } else if (!history_map_.empty()) {
            // 尝试找到邻居的mu
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
    
    // 约束μ在有效范围内
    double ClipMu(double mu) const {
        return std::max(mu_min_, std::min(mu_max_, mu));
    }
    
    // ============================================================================
    // 核心参数
    // ============================================================================
    double epsilon_;      // 调整步长
    double mu_min_;       // μ下限
    double mu_max_;       // μ上限
    
    // ============================================================================
    // 基于Rt的历史查表数据结构
    // ============================================================================
    
    // HistoryMap: 全局累积的 {Rt -> (mu, loss)} 映射表
    // 跨帧保持，每个帧结束时用CurrentFrameCache的内容更新相应Rt条目
    std::map<uint32_t, RtHistoryEntry> history_map_;
    
    // CurrentFrameCache: 当前帧的 {Rt -> mu}
    std::map<uint32_t, double> current_frame_cache_;
    
    // LossWindow: 最近 LOSS_WINDOW_SIZE 个包的丢包状态 (true = 丢包, false = 成功)
    std::deque<bool> loss_window_;
    
    // ============================================================================
    // QoE统计（仅用于记录，不影响mu调整）
    // ============================================================================
    std::map<uint32_t, double> frame_qoe_history_;      // 帧QoE历史
    std::map<uint32_t, FrameQoEDetail> frame_qoe_details_;  // 帧QoE详细信息
    
    // ============================================================================
    // 状态变量
    // ============================================================================
    uint32_t current_frame_id_;      // 当前帧ID
    
    // 控制标志
    bool oscc_enabled_;
    
    // 统计和日志
    std::vector<MuChangeRecord> mu_change_records_;  // μ变化记录
    uint32_t total_adjustments_;                      // 总调整次数
    
    // ============================================================================
    // Rt 预估相关参数（发送端使用）
    // ============================================================================
    Time estimated_rtt_ = MilliSeconds(30);           // 预估 RTT
    double estimated_bandwidth_bps_ = 20000000.0;     // 预估带宽 (bps)
    Time current_frame_deadline_ = Seconds(0);        // 当前帧截止时间
    
    // WebrtcSender引用（用于直接应用μ值到发送端）
    Ptr<WebrtcSender> webrtc_sender_ = nullptr;
    
public:
    // 设置WebrtcSender引用（在OSCCController中直接调用UpdateMuDynamic）
    void SetWebrtcSender(Ptr<WebrtcSender> sender) {
        webrtc_sender_ = sender;
        if (sender) {
            std::cout << "[OSCCController] WebrtcSender set, mu changes will be applied directly!" << std::endl;
        }
    }
    
private:
    // 直接应用μ值到WebrtcSender
    void ApplyMuToSender(double new_mu) {
        if (webrtc_sender_) {
            webrtc_sender_->UpdateMuDynamic(new_mu);
            std::cout << "[OSCC->Sender] Applied mu=" << new_mu 
                      << " directly to WebrtcSender at " << Simulator::Now().GetSeconds() << "s" << std::endl;
        } else {
            std::cout << "[OSCC->Sender] WARNING: webrtc_sender_ is null, cannot apply mu!" << std::endl;
        }
    }
};

// ============================================================================
// 强化学习状态管理器类
// ============================================================================
class RLStateManager {
public:
    RLStateManager() : current_mu(1.0), max_loss_rate(0.05), 
                      current_loss_rate(0.01), last_packet_Rt(1), current_rtt(MilliSeconds(30)),
                      current_delay(20.0) {
        std::cout << "=== RLStateManager Constructor ===" << std::endl;
        std::cout << "Default values:" << std::endl;
        std::cout << "  current_mu: " << current_mu << std::endl;
        std::cout << "  max_loss_rate: " << max_loss_rate << std::endl;
        std::cout << "  current_loss_rate: " << current_loss_rate << std::endl;
        std::cout << "==================================" << std::endl;
        NS_LOG_INFO("RLStateManager initialized with default mu=1.0, Lmax=0.05");
    }
    
    // 设置参数
    void SetParameters(double initial_mu, double lmax, Time rtt) {
        std::cout << "=== RLStateManager::SetParameters ===" << std::endl;
        std::cout << "Received parameters:" << std::endl;
        std::cout << "  initial_mu: " << initial_mu << std::endl;
        std::cout << "  lmax: " << lmax << " (THIS IS THE LOSS RATE PARAMETER)" << std::endl;
        std::cout << "  rtt: " << rtt.GetMilliSeconds() << "ms" << std::endl;
        
        current_mu = initial_mu;
        max_loss_rate = lmax;
        current_rtt = rtt;
        
        std::cout << "After setting:" << std::endl;
        std::cout << "  current_mu: " << current_mu << std::endl;
        std::cout << "  max_loss_rate: " << max_loss_rate << std::endl;
        std::cout << "  current_loss_rate: " << current_loss_rate << " (NOT CHANGED)" << std::endl;
        std::cout << "=====================================" << std::endl;
        NS_LOG_INFO("RLStateManager parameters set: mu=" << current_mu 
                   << ", Lmax=" << max_loss_rate << ", RTT=" << rtt.GetMilliSeconds() << "ms");
    }
    
    // 计算传输机会 Rt
    uint32_t CalculateTransmissionOpportunities(Time current_time, Time frame_deadline, 
                                               uint32_t packet_size, double trace_bandwidth_bps) {
        Time T_remain = frame_deadline - current_time;
        if (T_remain <= Seconds(0)) {
            return 0;
        }
        
        double send_time_seconds = static_cast<double>(packet_size * 8) / trace_bandwidth_bps;
        Time packet_send_time = Seconds(send_time_seconds);
        
        Time available_time = T_remain - packet_send_time;
        if (available_time <= Seconds(0)) {
            return 0;
        }
    
        uint32_t Rt = static_cast<uint32_t>(std::floor(available_time.GetSeconds() / current_rtt.GetSeconds()));
        
        NS_LOG_DEBUG("Transmission opportunities calculation:");
        NS_LOG_DEBUG("  Current time: " << current_time.GetSeconds() << "s");
        NS_LOG_DEBUG("  Frame deadline: " << frame_deadline.GetSeconds() << "s");
        NS_LOG_DEBUG("  T_remain: " << T_remain.GetSeconds() << "s");
        NS_LOG_DEBUG("  Packet size: " << packet_size << " bytes");
        NS_LOG_DEBUG("  Trace bandwidth: " << trace_bandwidth_bps << " bps");
        NS_LOG_DEBUG("  Packet send time: " << packet_send_time.GetSeconds() << "s");
        NS_LOG_DEBUG("  Available time: " << available_time.GetSeconds() << "s");
        NS_LOG_DEBUG("  RTT: " << current_rtt.GetSeconds() << "s");
        NS_LOG_DEBUG("  Rt: " << Rt);
        
        return Rt;
    }
    
    double CalculateReward(double mu_prev, double gcc_bandwidth_bps, double trace_bandwidth_bps,
                      double current_delay_ms, double current_loss_rate, 
                      double miss_deadline_time, uint32_t Rt_current, uint32_t Rt_prev,
                      uint32_t frame_id, uint32_t packet_index) {
    
    // 确定使用的Rt值
    uint32_t Rt_used = Rt_prev;
    if (frame_id == 0 && packet_index == 0) {
        Rt_used = 1;
    } else if (packet_index == 0 && frame_id > 0) {
        Rt_used = last_packet_Rt;
    }

    // (1) 带宽利用率 U
    double U = (mu_prev * gcc_bandwidth_bps) / trace_bandwidth_bps;
    U = std::min(std::max(U, 0.0), 1.0);
    
    // (2) 延迟惩罚 - 使用实际延迟
    double p_delay = 0.0;
    if (current_delay_ms < 30.0) {
        p_delay = current_delay_ms / 200.0;
    } else if (current_delay_ms < 80) {
        p_delay = 0.15 + current_delay_ms / 100.0;
    } else {
        p_delay = current_delay_ms / 50.0 + 0.65;
    }
    p_delay = std::min(p_delay, 1.0);
    
    // Rt越小，对延迟越敏感
    double delay_sensitivity = 1.0 + (1.0 - Rt_used / 10.0) * 0.3;
    p_delay *= delay_sensitivity;
    
    // (3) 丢包率惩罚
    double Ptget = 1.0 - max_loss_rate;
    double Ltol;
    if (Rt_used == 0){
        Ltol = 0.01;
    } else {
        Ltol = std::pow(1.0 - Ptget, 1.0 / Rt_used);
    }

    // 自适应丢包容忍度：Rt越大容忍度越高
    double adaptive_tolerance = Ltol * (1.0 + 0.5 * (Rt_used / 10.0));
    double p_loss = current_loss_rate / adaptive_tolerance;
    p_loss = std::min(p_loss, 1.0);
    
    // (4) 错过截止时间惩罚
    double p_mddl = 0.0;
    double rtt_seconds = current_rtt.GetSeconds();
    
    if (packet_index == 0) {
        if (Rt_current > 0) {
            double denominator = (Rt_current - Rt_prev + 1) * rtt_seconds;
            if (denominator > 0.001) {
                p_mddl = miss_deadline_time / denominator;
                p_mddl = std::min(std::max(p_mddl, 0.0), 1.0);
            }
        }
    } else {
        uint32_t Rt_frame_first = Rt_current + packet_index;
        if (Rt_frame_first > 0) {
            double denominator = (Rt_frame_first - Rt_prev + 1) * rtt_seconds;
            if (denominator > 0.001) {
                p_mddl = miss_deadline_time / denominator;
                p_mddl = std::min(std::max(p_mddl, 0.0), 1.0);
            }
        }
    }

    double U_weight = 2.5;
    double delay_weight = 10;
    double loss_weight = 10;
    double mddl_weight = 10.0;

    double reward = U_weight * U 
                  - delay_weight * p_delay 
                  - loss_weight * p_loss 
                  - mddl_weight * p_mddl;

    
    NS_LOG_DEBUG("Rt_used: " << Rt_used << ", mu_prev: " << mu_prev << ", U: " << U);
    NS_LOG_DEBUG("p_delay: " << p_delay << ", p_loss: " << p_loss << ", p_mddl: " << p_mddl);
    
    return reward;
    }
    
    // 记录包状态 - 修改为包含接收时间和实际延迟
    void RecordPacketState(uint32_t frame_id, uint32_t packet_index, double mu_used,
                          uint32_t Rt, double loss_rate, double reward,
                          Time send_time, Time recivied_time, Time deadline,
                          double bandwidth_utilization, double p_delay, 
                          double p_loss, double p_mddl, double current_delay_ms) {
        PacketStateRecord record;
        record.frame_id = frame_id;
        record.packet_index = packet_index;
        record.mu_used = mu_used;
        record.Rt = Rt;
        record.loss_rate = loss_rate;
        record.reward = reward;
        record.send_time = send_time;
        record.recivied_time = recivied_time;  // 记录接收时间
        record.deadline = deadline;
        record.bandwidth_utilization = bandwidth_utilization;
        record.p_delay_value = p_delay;
        record.p_loss_value = p_loss;
        record.p_mddl_value = p_mddl;
        record.current_delay = current_delay_ms;  // 记录实际延迟
        
        packet_records.push_back(record);
        last_packet_Rt = Rt;
        
        // 将包记录添加到Rt分组管理器中
        AddPacketToRtGroup(frame_id, packet_index, Rt, loss_rate, reward, send_time, mu_used);
        
        NS_LOG_INFO("Recorded REAL packet state: frame=" << frame_id << ", packet=" << packet_index
                   << ", mu=" << mu_used << ", Rt=" << Rt << ", loss_rate=" << loss_rate 
                   << ", reward=" << reward << ", send_time=" << send_time.GetSeconds() 
                   << "s, recv_time=" << recivied_time.GetSeconds() 
                   << "s, delay=" << current_delay_ms << "ms");
    }
    
    // 新增：将包添加到Rt分组管理器
    void AddPacketToRtGroup(uint32_t frame_id, uint32_t packet_index, uint32_t Rt, 
                           double loss_rate, double reward, Time send_time, double mu_used) {
        // 如果这是该帧的第一个包，或者Rt值发生了变化，结束前一个Rt组
        if (current_rt_group.frame_id != frame_id || current_rt_group.Rt_value != Rt) {
            // 如果前一个Rt组有数据，计算平均奖励并保存
            if (current_rt_group.packet_count > 0) {
                FinalizeCurrentRtGroup();
            }
            
            // 开始新的Rt组
            current_rt_group.frame_id = frame_id;
            current_rt_group.Rt_value = Rt;
            current_rt_group.loss_rate = loss_rate;
            current_rt_group.mu_used = mu_used;  // 记录μ值
            current_rt_group.avg_reward = 0.0;
            current_rt_group.packet_count = 0;
            current_rt_group.reward_sum = 0.0;
            current_rt_group.start_time = send_time;
        }
        
        // 添加当前包到当前Rt组
        current_rt_group.packet_count++;
        current_rt_group.reward_sum += reward;
        current_rt_group.end_time = send_time;
    }
    
    // 新增：完成当前Rt组并保存
    void FinalizeCurrentRtGroup() {
        if (current_rt_group.packet_count > 0) {
            current_rt_group.avg_reward = current_rt_group.reward_sum / current_rt_group.packet_count;
            
            RtGroupRewardRecord record(
                current_rt_group.frame_id,
                current_rt_group.Rt_value,
                current_rt_group.loss_rate,
                current_rt_group.mu_used,        // 包含μ值
                current_rt_group.avg_reward,
                current_rt_group.packet_count,
                current_rt_group.start_time,
                current_rt_group.end_time
            );
            
            rt_group_records.push_back(record);
            
            NS_LOG_INFO("Finalized Rt group: frame=" << record.frame_id 
                       << ", Rt=" << record.Rt_value 
                       << ", mu_used=" << record.mu_used
                       << ", packets=" << record.packet_count
                       << ", avg_reward=" << record.avg_reward
                       << ", loss_rate=" << record.loss_rate);
            
            // OSCC集成：Rt组完成时触发帧内μ调整
            if (oscc_enabled_ && oscc_controller_) {
                oscc_controller_->OnRtGroupComplete(
                    current_rt_group.frame_id,
                    current_rt_group.Rt_value,
                    current_rt_group.loss_rate
                );
                // 同步更新本地μ值
                current_mu = oscc_controller_->GetCurrentMu();
                NS_LOG_DEBUG("OSCC: Updated mu to " << current_mu << " after Rt group complete");
            }
        }
        
        // 重置当前Rt组
        current_rt_group.frame_id = 0;
        current_rt_group.Rt_value = 0;
        current_rt_group.loss_rate = 0.0;
        current_rt_group.mu_used = 1.0;
        current_rt_group.avg_reward = 0.0;
        current_rt_group.packet_count = 0;
        current_rt_group.reward_sum = 0.0;
        current_rt_group.start_time = Seconds(0);
        current_rt_group.end_time = Seconds(0);
    }
    
    // 新增：获取所有Rt组记录
    const std::vector<RtGroupRewardRecord>& GetRtGroupRecords() const {
        return rt_group_records;
    }
    
    // 输出状态记录到文件 - 修改为包含recivied_time和current_delay
    void OutputStateRecords(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
        std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                              "_L=" + std::to_string(initial_loss_rate) + "_RL_log.csv";
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open RL log file: " << filename);
            return;
        }
        
        // 修改表头以包含recivied_time
        file << "frame_id,packet_index,mu_used,Rt,loss_rate,reward,send_time,recivied_time,deadline,"
             << "bandwidth_utilization,p_delay,p_loss,p_mddl,current_delay" << std::endl;
        
        // 按时间排序记录
        std::vector<PacketStateRecord> sorted_records = packet_records;
        std::sort(sorted_records.begin(), sorted_records.end(), 
                 [](const PacketStateRecord& a, const PacketStateRecord& b) {
                     return a.send_time < b.send_time;
                 });
        
        for (const auto& record : sorted_records) {
            file << record.frame_id << ","
                 << record.packet_index << ","
                 << record.mu_used << ","
                 << record.Rt << ","
                 << record.loss_rate << ","
                 << record.reward << ","
                 << record.send_time.GetSeconds() << ","
                 << record.recivied_time.GetSeconds() << ","
                 << record.deadline.GetSeconds() << ","
                 << record.bandwidth_utilization << ","
                 << record.p_delay_value << ","
                 << record.p_loss_value << ","
                 << record.p_mddl_value << ","
                 << record.current_delay << std::endl;  // 输出实际延迟
        }
        
        file.close();
        
        NS_LOG_INFO("RL state records saved to: " << filename << " with " << packet_records.size() << " REAL records");
        
        if (packet_records.empty()) {
            std::cout << "WARNING: No RL state records were generated!" << std::endl;
            std::cout << "This means no real packets were processed." << std::endl;
        } else {
            std::cout << "Successfully recorded " << packet_records.size() << " REAL RL state entries" << std::endl;
        }
    }
    
    // 新增：输出Rt组奖励记录到文件
    void OutputRtGroupRewards(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
        // 确保完成最后一个Rt组
        FinalizeCurrentRtGroup();
        
        std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                              "_L=" + std::to_string(initial_loss_rate) + "_Frame-Rt-Reward.csv";
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open Rt group reward file: " << filename);
            return;
        }
        
        // 写入表头 - 包含mu_used字段
        file << "frame_id,Rt_value,loss_rate,mu_used,avg_reward,packet_count,start_time,end_time" << std::endl;
        
        for (const auto& record : rt_group_records) {
            file << record.frame_id << ","
                 << record.Rt_value << ","
                 << record.loss_rate << ","
                 << record.mu_used << ","          // 输出μ值
                 << record.avg_reward << ","
                 << record.packet_count << ","
                 << record.start_time.GetSeconds() << ","
                 << record.end_time.GetSeconds() << std::endl;
        }
        
        file.close();
        
        NS_LOG_INFO("Rt group reward records saved to: " << filename << " with " 
                   << rt_group_records.size() << " Rt groups");
        
        if (rt_group_records.empty()) {
            std::cout << "WARNING: No Rt group reward records were generated!" << std::endl;
        } else {
            std::cout << "Successfully recorded " << rt_group_records.size() << " Rt group reward entries" << std::endl;
            
            // 输出前几个记录作为示例
            std::cout << "First few Rt group records:" << std::endl;
            int count = 0;
            for (const auto& record : rt_group_records) {
                if (count++ >= 5) break;
                std::cout << "  Frame " << record.frame_id 
                          << ", Rt=" << record.Rt_value 
                          << ", μ=" << record.mu_used
                          << ", Reward=" << record.avg_reward 
                          << ", Packets=" << record.packet_count << std::endl;
            }
        }
    }
    
    // 更新当前mu值
    void SetCurrentMu(double mu) {
        current_mu = mu;
    }
    
    double GetCurrentMu() const {
        return current_mu;
    }
    
    // 设置μ值（由OSCC调用）
    void SetMu(double mu) {
        if (mu >= 0.5 && mu <= 1.5) {
            current_mu = mu;
            NS_LOG_DEBUG("RLStateManager: mu set to " << mu);
        }
    }
    
    // 获取包记录（用于QoE计算）
    const std::vector<PacketStateRecord>& GetPacketRecords() const {
        return packet_records;
    }
    
    // 更新网络状态 - 修改为使用实际延迟
    void UpdateNetworkState(double delay_ms, double loss_rate, Time rtt) {
        current_delay = delay_ms;
        current_loss_rate = loss_rate;
        current_rtt = rtt;
        NS_LOG_DEBUG("Network state updated: delay=" << delay_ms << "ms, loss_rate=" << loss_rate 
                   << ", RTT=" << rtt.GetMilliSeconds() << "ms");
    }
    
    // 获取最后一个包的传输机会
    uint32_t GetLastPacketRt() const {
        return last_packet_Rt;
    }
    
    // 设置当前 loss_rate
    void SetCurrentLossRate(double loss_rate) {
        current_loss_rate = loss_rate;
        NS_LOG_INFO("RLStateManager current_loss_rate updated to: " << current_loss_rate);
    }
    
    // 获取当前 loss_rate
    double GetCurrentLossRate() const {
        return current_loss_rate;
    }
    
    // 获取 max_loss_rate
    double GetMaxLossRate() const {
        return max_loss_rate;
    }
    
    // 获取当前延迟
    double GetCurrentDelay() const {
        return current_delay;
    }
    
    // 新增：获取当前RTT
    Time GetCurrentRTT() const {
        return current_rtt;
    }
    
    // ============ OSCC集成方法 ============
    
    // 设置OSCCController
    void SetOSCCController(OSCCController* controller) {
        oscc_controller_ = controller;
        oscc_enabled_ = (controller != nullptr);
        NS_LOG_INFO("RLStateManager: OSCCController " << (oscc_enabled_ ? "enabled" : "disabled"));
        if (oscc_enabled_) {
            std::cout << "[RLStateManager] OSCC enabled, will use dynamic mu from OSCCController" << std::endl;
        }
    }
    
    // 检查OSCC是否启用
    bool IsOSCCEnabled() const {
        return oscc_enabled_ && oscc_controller_ != nullptr;
    }
    
    // 获取OSCCController
    OSCCController* GetOSCCController() {
        return oscc_controller_;
    }
    
    // 从OSCC获取自适应μ值（如果启用）
    double GetAdaptiveMu(uint32_t frame_id, uint32_t Rt) {
        if (oscc_enabled_ && oscc_controller_) {
            double oscc_mu = oscc_controller_->GetMuForPacket(frame_id, Rt);
            // 同步更新本地current_mu
            current_mu = oscc_mu;
            return oscc_mu;
        }
        return current_mu;
    }
    
    // 通知Rt组完成（触发OSCC帧内调整）
    void NotifyRtGroupComplete(uint32_t frame_id, uint32_t Rt, double loss) {
        if (oscc_enabled_ && oscc_controller_) {
            oscc_controller_->OnRtGroupComplete(frame_id, Rt, loss);
            // 同步更新本地μ值
            current_mu = oscc_controller_->GetCurrentMu();
        }
    }
    
private:
    double current_mu;
    double max_loss_rate;
    double current_loss_rate;
    uint32_t last_packet_Rt;
    Time current_rtt;
    double current_delay;
    std::vector<PacketStateRecord> packet_records;
    
    // 新增：Rt分组管理相关成员
    struct RtGroup {
        uint32_t frame_id;
        uint32_t Rt_value;
        double loss_rate;
        double mu_used;       // 新增：该Rt组中使用的μ值
        double avg_reward;
        uint32_t packet_count;
        double reward_sum;
        Time start_time;
        Time end_time;
        
        RtGroup() : frame_id(0), Rt_value(0), loss_rate(0.0), mu_used(1.0),
                   avg_reward(0.0), packet_count(0), reward_sum(0.0), 
                   start_time(Seconds(0)), end_time(Seconds(0)) {}
    };
    
    RtGroup current_rt_group;
    std::vector<RtGroupRewardRecord> rt_group_records;
    
    // OSCC集成
    OSCCController* oscc_controller_ = nullptr;
    bool oscc_enabled_ = false;
};

// 强化学习状态数据结构
struct RLState {
    double mu;                    // 当前使用的带宽缩放因子
    double reward;               // 当前奖励值
    double bandwidth_utilization; // 带宽利用率 U
    double p_delay;              // 延迟惩罚项
    double p_loss;               // 丢包率惩罚项  
    double p_mddl;               // 错过截止时间惩罚项
    double current_delay;        // 当前延迟
    double current_loss_rate;    // 当前丢包率
    double miss_deadline_time;   // 错过截止时间的时间量
    uint32_t transmission_opportunities; // 剩余传输机会 Rt
    Time packet_send_time;       // 包发送时间
    Time frame_deadline;         // 帧截止时间
    
    RLState() : mu(1.0), reward(0.0), bandwidth_utilization(0.0), 
                p_delay(0.0), p_loss(0.0), p_mddl(0.0),
                current_delay(0.0), current_loss_rate(0.0), 
                miss_deadline_time(0.0), transmission_opportunities(0) {}
};

// 前向声明 BandwidthChanger（因为 TriggerRandomLoss 需要使用它）
class BandwidthChanger;

class TriggerRandomLoss{
public:
    TriggerRandomLoss(double loss_rate = 0.0) 
        : m_loss_rate(loss_rate), m_last_applied_loss(-1.0),
          m_bandwidth_changer(nullptr), m_use_trace_loss(false), 
          m_update_interval_ms(100) {
        NS_LOG_INFO("TriggerRandomLoss initialized with loss rate: " << m_loss_rate);
    }
    
    ~TriggerRandomLoss(){
        if(m_timer.IsRunning()){
            m_timer.Cancel();
        }
        if(m_update_timer.IsRunning()){
            m_update_timer.Cancel();
        }
    }
    
    void RegisterDevice(Ptr<NetDevice> dev){
        m_dev = dev;
        NS_LOG_INFO("Device registered for random loss with rate: " << m_loss_rate);
    }
    
    // 设置 BandwidthChanger 以便从trace读取动态loss值
    void SetBandwidthChanger(BandwidthChanger* changer) {
        m_bandwidth_changer = changer;
        m_use_trace_loss = (changer != nullptr);
        if (m_use_trace_loss) {
            std::cout << "[TriggerRandomLoss] BandwidthChanger set - will use DYNAMIC loss from trace!" << std::endl;
        }
    }
    
    // 设置是否使用trace中的动态loss值
    void SetUseTraceLoss(bool use_trace) {
        m_use_trace_loss = use_trace && (m_bandwidth_changer != nullptr);
    }
    
    // 设置更新间隔（毫秒）
    void SetUpdateInterval(uint32_t interval_ms) {
        m_update_interval_ms = interval_ms;
    }
    
    void Start(){
        Time next = MilliSeconds(10);
        m_timer = Simulator::Schedule(next, &TriggerRandomLoss::ConfigureRandomLoss, this);
        NS_LOG_INFO("Scheduled random loss configuration at " << next.GetSeconds() << "s with rate: " << m_loss_rate);
    }
    
    void ConfigureRandomLoss(){
        if (m_dev) {
            // 创建 RateErrorModel
            m_error_model = CreateObject<RateErrorModel>();
            m_error_model->SetAttribute ("ErrorRate", DoubleValue (m_loss_rate));
            m_error_model->SetAttribute ("ErrorUnit", EnumValue (RateErrorModel::ERROR_UNIT_PACKET));
            
            // 使用 PointToPointNetDevice 的 SetReceiveErrorModel 方法
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(m_dev);
            if (p2pDev) {
                p2pDev->SetReceiveErrorModel(m_error_model);
                m_last_applied_loss = m_loss_rate;
                
                std::cout << "================================================" << std::endl;
                std::cout << "[TriggerRandomLoss] ReceiveErrorModel CONFIGURED!" << std::endl;
                std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
                std::cout << "  Initial Loss Rate: " << m_loss_rate << " (" << (m_loss_rate * 100) << "%)" << std::endl;
                std::cout << "  Device Type: PointToPointNetDevice" << std::endl;
                std::cout << "  Dynamic Update: " << (m_use_trace_loss ? "ENABLED (from trace)" : "DISABLED") << std::endl;
                std::cout << "  Update Interval: " << m_update_interval_ms << "ms" << std::endl;
                std::cout << "================================================" << std::endl;
                
                NS_LOG_INFO("Random loss configured at " << Simulator::Now().GetSeconds() 
                           << "s with rate: " << m_loss_rate);
                
                // 如果启用了动态更新，开始周期性更新
                if (m_use_trace_loss && m_bandwidth_changer) {
                    ScheduleNextUpdate();
                }
            } else {
                // 回退到 SetAttribute 方式
                m_dev->SetAttribute ("ReceiveErrorModel", PointerValue (m_error_model));
                m_last_applied_loss = m_loss_rate;
                
                std::cout << "[TriggerRandomLoss] ReceiveErrorModel CONFIGURED (fallback)!" << std::endl;
                std::cout << "  Loss Rate: " << m_loss_rate << " (" << (m_loss_rate * 100) << "%)" << std::endl;
                
                // 如果启用了动态更新，开始周期性更新
                if (m_use_trace_loss && m_bandwidth_changer) {
                    ScheduleNextUpdate();
                }
            }
        } else {
            NS_LOG_ERROR("No device registered for random loss configuration");
            std::cout << "[ERROR] TriggerRandomLoss: No device registered!" << std::endl;
        }
        
        m_timer.Cancel();
    }
    
    // 周期性更新丢包率（从trace读取）- 实现在 BandwidthChanger 类定义之后
    void UpdateLossFromTrace();
    
    void ScheduleNextUpdate() {
        m_update_timer = Simulator::Schedule(
            MilliSeconds(m_update_interval_ms), 
            &TriggerRandomLoss::UpdateLossFromTrace, 
            this
        );
    }
    
    void SetLossRate(double loss_rate) {
        m_loss_rate = loss_rate;
        // 如果已经有error model，立即更新
        if (m_error_model) {
            m_error_model->SetAttribute("ErrorRate", DoubleValue(loss_rate));
            m_last_applied_loss = loss_rate;
        }
        NS_LOG_INFO("Loss rate updated to: " << m_loss_rate);
    }
    
    double GetLossRate() const {
        return m_loss_rate;
    }
    
    // 获取最后应用的loss率
    double GetLastAppliedLoss() const {
        return m_last_applied_loss;
    }
    
private:
    Ptr<NetDevice> m_dev;
    Ptr<RateErrorModel> m_error_model;  // 保存引用以便动态更新
    EventId m_timer;
    EventId m_update_timer;  // 用于周期性更新的定时器
    double m_loss_rate;
    double m_last_applied_loss;  // 上次应用的loss值
    BandwidthChanger* m_bandwidth_changer;  // 用于获取trace中的loss值
    bool m_use_trace_loss;  // 是否使用trace中的动态loss值
    uint32_t m_update_interval_ms;  // 更新间隔（毫秒）
};

struct CompareV
{
    bool operator() (const std::pair<Time,int64_t> &a,const std::pair<Time,int64_t> &b)
    {
        return a.first<b.first;
    }
};

class BandwidthChanger
{
public:
    BandwidthChanger() : current_trace_bandwidth_bps_(0) {}
    ~BandwidthChanger(){
        if (m_timer.IsRunning()) {
            m_timer.Cancel();
        }
    }
    
    void RegisterDevice(Ptr<NetDevice> dev) {
        m_dev=dev;
    }
    
    void Config(int64_t initial_bps,std::vector<std::pair<Time,int64_t> >& info) {
        m_initialRate=initial_bps;
        m_info.swap(info);
        current_trace_bandwidth_bps_ = initial_bps;
    }

    std::pair<float,float> ConfigwithReadNetworkTrace(int64_t initial_bps, const std::string& trace_file)
    {
        std::pair<float,float> dur_time;

        std::cout << "[DEBUG] Reading trace file: " << trace_file << std::endl;
        m_initialRate = initial_bps;
        current_trace_bandwidth_bps_ = initial_bps;

        if (m_timer.IsRunning()) {
            m_timer.Cancel();
        }
        m_info.clear();
        m_index = 0;

        std::ifstream singlefile(trace_file);
        if (!singlefile.is_open()) {
            NS_LOG_ERROR("Cannot open bandwidth file: " << trace_file);
            dur_time.first = 0;
            dur_time.second = 0;
            return dur_time;
        }

        float start_time = -1.0;
        float end_time = 0.0;
        float time, bandwidth, rtt, loss;
        
        // 清空trace数据
        m_trace_data.clear();
        
        // 读取四列数据
        while (singlefile >> time >> bandwidth >> rtt >> loss) {
            // 存储完整的trace数据
            TraceData trace_data;
            trace_data.timestamp = Seconds(time);
            trace_data.bandwidth = bandwidth * 1 * 1000000.0; // 转换为bps
            trace_data.rtt = rtt;
            trace_data.loss = loss;
            m_trace_data.push_back(trace_data);
            
            // 仅带宽变化用于带宽调度
            int64_t FIX_bandwidth_bps = static_cast<int64_t>(bandwidth * 1 * 1000000.0);//暂时将带宽放大1倍
            m_info.push_back(std::make_pair(Seconds(time), FIX_bandwidth_bps));
            
            if (start_time < 0 || time < start_time)
                start_time = time;
            if (time > end_time)
                end_time = time;
        }
        
        singlefile.close();
        
        std::cout << "Original trace time range: " << start_time << "s to " << end_time << "s" << std::endl;
        std::cout << "Total events: " << m_info.size() << std::endl;
        std::cout << "Trace data entries: " << m_trace_data.size() << std::endl;
        
        // 输出前几个trace数据用于调试
        for (size_t i = 0; i < std::min(m_trace_data.size(), size_t(5)); i++) {
            const TraceData& data = m_trace_data[i];
            std::cout << "Trace data " << i << ": time=" << data.timestamp.GetSeconds() 
                      << "s, bw=" << data.bandwidth << " bps, rtt=" << data.rtt 
                      << "ms, loss=" << data.loss << std::endl;
        }
        
        if (!m_info.empty()) {
            dur_time.first = 0.0;
            dur_time.second = (end_time - start_time) + 10.0;
        } else {
            dur_time.first = 0;
            dur_time.second = 30.0;
        }
        
        return dur_time;
    }

    void ResetBandwidthFromTrace()
    {
        if (m_timer.IsExpired() && m_dev && m_index < m_info.size()) {
            PointToPointNetDevice *device=static_cast<PointToPointNetDevice*>(PeekPointer(m_dev));
            device->SetDataRate(DataRate(m_info[m_index].second));
            current_trace_bandwidth_bps_ = m_info[m_index].second;
            
            NS_LOG_INFO("Bandwidth changed at " << Simulator::Now().GetSeconds() 
                    << "s to " << m_info[m_index].second << " bps (" 
                    << (m_info[m_index].second / 1000000.0) << " Mbps)");
            
            m_index++;
            if (m_index < m_info.size()){
                NS_ASSERT(m_info[m_index].first>m_info[m_index-1].first);
                Time next=m_info[m_index].first-m_info[m_index-1].first;
                m_timer=Simulator::Schedule(next,&BandwidthChanger::ResetBandwidthFromTrace,this);
            } else {
                NS_LOG_INFO("All bandwidth changes completed at " << Simulator::Now().GetSeconds() << "s");
            }
        }
    }

    void TotalThroughput(Time stop,int64_t &channel_bit){
        int index=lower_bound_index(stop);
        if (0 == index){
            channel_bit=0;
        }else{
            int64_t bit=0;
            for(int i=0;i<index;i++){
                if(0 == i){
                    bit+=m_initialRate*(m_info.at(i).first.GetMilliSeconds()/1000);
                }else{
                    bit+=m_info.at(i-1).second*((m_info.at(i).first-m_info.at(i-1).first).GetMilliSeconds()/1000);
                }
            }
            if (stop > m_info.back().first) {
                bit+=m_info.back().second*((stop-m_info.back().first).GetMilliSeconds()/1000);
            }
            channel_bit=bit;
        }
    }
    
    void Start()
    {
        if (m_info.size()>0) {
            m_index = 0;
            Time next=m_info[m_index].first;
            m_timer=Simulator::Schedule(next,&BandwidthChanger::ResetBandwidthFromTrace,this);
        }
    }

    int lower_bound_index(Time point){
        auto ele=std::make_pair(point,0);
        auto iter=std::lower_bound(m_info.begin(), m_info.end(),ele,CompareV());
        return iter-m_info.begin();
    }
    
    // 获取当前trace带宽
    double GetCurrentTraceBandwidth() const {
        return current_trace_bandwidth_bps_;
    }
    
    // 新增：根据时间戳获取trace带宽
    uint32_t GetTraceBandwidthAtTime(uint32_t timestamp_ms) const {
        Time query_time = MilliSeconds(timestamp_ms);
        
        if (m_info.empty()) {
            return static_cast<uint32_t>(current_trace_bandwidth_bps_);
        }
        
        // 找到最后一个小于等于查询时间的带宽设置
        int64_t trace_bw = m_initialRate;
        
        for (const auto& change : m_info) {
            if (change.first <= query_time) {
                trace_bw = change.second;
            } else {
                break;
            }
        }
        
        std::cout << "[BandwidthChanger] GetTraceBandwidthAtTime: " << timestamp_ms 
                  << "ms -> " << trace_bw << " bps (" << (trace_bw / 1000000.0) << " Mbps)" << std::endl;
        
        return static_cast<uint32_t>(trace_bw);
    }
    
    // 新增：根据时间获取RTT值
    double GetRTTAtTime(uint32_t timestamp_ms) const {
        Time query_time = MilliSeconds(timestamp_ms);
        
        if (m_trace_data.empty()) {
            return 30.0; // 默认30ms
        }
        
        // 找到最接近的时间戳的RTT值
        double rtt = 30.0; // 默认值
        Time min_diff = Seconds(1000000);
        
        for (const auto& data : m_trace_data) {
            Time diff = Abs(data.timestamp - query_time);
            if (diff < min_diff) {
                min_diff = diff;
                rtt = data.rtt;
            }
        }
        
        // 如果最接近的记录相差超过0.5秒，使用默认值
        if (min_diff > Seconds(0.5)) {
            return 30.0;
        }
        
        std::cout << "[BandwidthChanger] GetRTTAtTime: " << timestamp_ms 
                  << "ms -> " << rtt << "ms (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
        
        return rtt;
    }
    
    // 新增：根据时间获取Loss值
    double GetLossAtTime(uint32_t timestamp_ms) const {
        Time query_time = MilliSeconds(timestamp_ms);
        
        if (m_trace_data.empty()) {
            return 0.01; // 默认1% loss
        }
        
        // 找到最接近的时间戳的Loss值
        double loss = 0.01; // 默认值
        Time min_diff = Seconds(1000000);
        
        for (const auto& data : m_trace_data) {
            Time diff = Abs(data.timestamp - query_time);
            if (diff < min_diff) {
                min_diff = diff;
                loss = data.loss;
            }
        }
        
        // 如果最接近的记录相差超过0.5秒，使用默认值
        if (min_diff > Seconds(0.5)) {
            return 0.01;
        }
        
        std::cout << "[BandwidthChanger] GetLossAtTime: " << timestamp_ms 
                  << "ms -> " << loss << " (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
        
        return loss;
    }
    
    // 新增：根据时间获取TraceData
    TraceData GetTraceDataAtTime(uint32_t timestamp_ms) const {
        Time query_time = MilliSeconds(timestamp_ms);
        
        if (m_trace_data.empty()) {
            // 返回默认值
            TraceData default_data;
            default_data.timestamp = query_time;
            default_data.bandwidth = current_trace_bandwidth_bps_;
            default_data.rtt = 30.0;
            default_data.loss = 0.01;
            return default_data;
        }
        
        // 找到最接近的时间戳的TraceData
        const TraceData* closest_data = &m_trace_data[0];
        Time min_diff = Abs(closest_data->timestamp - query_time);
        
        for (const auto& data : m_trace_data) {
            Time diff = Abs(data.timestamp - query_time);
            if (diff < min_diff) {
                min_diff = diff;
                closest_data = &data;
            }
        }
        
        // 如果最接近的记录相差超过0.5秒，使用当前带宽和默认RTT/loss
        if (min_diff > Seconds(0.5)) {
            TraceData default_data;
            default_data.timestamp = query_time;
            default_data.bandwidth = current_trace_bandwidth_bps_;
            default_data.rtt = 30.0;
            default_data.loss = 0.01;
            return default_data;
        }
        
        std::cout << "[BandwidthChanger] GetTraceDataAtTime: " << timestamp_ms 
                  << "ms -> bw=" << closest_data->bandwidth << " bps, rtt=" << closest_data->rtt 
                  << "ms, loss=" << closest_data->loss << " (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
        
        return *closest_data;
    }
    
private:
    int64_t m_initialRate=0;
    std::vector<std::pair<Time,int64_t> > m_info;
    std::vector<TraceData> m_trace_data;  // 新增：存储完整的trace数据
    uint32_t m_index{0};
    Ptr<NetDevice> m_dev;
    EventId m_timer;
    double current_trace_bandwidth_bps_;
};

// TriggerRandomLoss::UpdateLossFromTrace 实现（需要在 BandwidthChanger 定义之后）
void TriggerRandomLoss::UpdateLossFromTrace() {
    if (!m_dev || !m_bandwidth_changer) {
        ScheduleNextUpdate();
        return;
    }
    
    // 获取当前时间对应的trace loss值
    uint32_t now_ms = static_cast<uint32_t>(Simulator::Now().GetMilliSeconds());
    double trace_loss = m_bandwidth_changer->GetLossAtTime(now_ms);
    
    // 只有当loss值变化超过阈值时才更新（避免过于频繁的重新创建）
    if (std::abs(trace_loss - m_last_applied_loss) > 0.001) {
        // 创建新的 RateErrorModel 并重新应用到设备
        // 这是因为 SetAttribute 可能不会立即生效
        Ptr<RateErrorModel> new_error_model = CreateObject<RateErrorModel>();
        new_error_model->SetAttribute("ErrorRate", DoubleValue(trace_loss));
        new_error_model->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(m_dev);
        if (p2pDev) {
            p2pDev->SetReceiveErrorModel(new_error_model);
            m_error_model = new_error_model;  // 更新引用
            m_last_applied_loss = trace_loss;
            m_loss_rate = trace_loss;
            
            std::cout << "[TriggerRandomLoss] Loss rate UPDATED from trace!" << std::endl;
            std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
            std::cout << "  New Loss Rate: " << trace_loss << " (" << (trace_loss * 100) << "%)" << std::endl;
            
            NS_LOG_INFO("Loss rate updated from trace at " << Simulator::Now().GetSeconds() 
                       << "s to: " << trace_loss);
        }
    }
    
    // 调度下一次更新
    ScheduleNextUpdate();
}

// FrameManager replaced by QoEIntegrationManager
class QoEIntegrationManager {
public:
    struct BandwidthRecord {
        Time timestamp;
        double trace_bandwidth;
        double gcc_bandwidth;
        double scaled_bandwidth;
        double mu_value;
        
        BandwidthRecord(Time ts, double trace_bw, double gcc_bw, double scaled_bw, double mu)
            : timestamp(ts), trace_bandwidth(trace_bw), gcc_bandwidth(gcc_bw), 
              scaled_bandwidth(scaled_bw), mu_value(mu) {}
    };

    QoEIntegrationManager() 
        : oscc_controller_(nullptr), rl_manager_(nullptr), bw_changer_(nullptr), 
          webrtc_sender_(nullptr) {}

    void SetOSCCController(OSCCController* controller) {
        oscc_controller_ = controller;
    }
    
    OSCCController* GetOSCCController() { return oscc_controller_; }

    void SetRLStateManager(RLStateManager* manager) {
        rl_manager_ = manager;
    }
    
    void SetBandwidthChanger(BandwidthChanger* changer) {
        bw_changer_ = changer;
    }
    
    void SetWebrtcSender(Ptr<WebrtcSender> sender) {
        webrtc_sender_ = sender;
    }
    
    // Callback from FramePlayoutManager
    void OnPacketReceived(const FramePacketInfo& info, const FrameStatistics& frame_stats) {
        if (!rl_manager_) return;
        
        Time now = Simulator::Now();
        Time send_time = MilliSeconds(info.send_time_ms);
        double delay_ms = (now - send_time).GetMilliSeconds();
        if (delay_ms < 0) delay_ms = 0;

        // 更新 OSCC 滑动窗口：收到包表示成功，丢包状态为 false
        if (oscc_controller_ && oscc_controller_->IsEnabled()) {
            oscc_controller_->UpdateLossWindow(false);  // false = 包成功接收
        }

        // Get network info from trace via BandwidthChanger
        double trace_loss = 0.01;
        double trace_rtt = 30.0;
        double trace_bw = 20000000.0;  // 默认值
        
        if (bw_changer_) {
            uint32_t ts = now.GetMilliSeconds();
            trace_loss = bw_changer_->GetLossAtTime(ts);
            trace_rtt = bw_changer_->GetRTTAtTime(ts);
            // 注：不再从trace文件直接获取带宽
            // trace_bw = bw_changer_->GetTraceBandwidthAtTime(ts);
        }
        
        rl_manager_->UpdateNetworkState(delay_ms, trace_loss, MilliSeconds(trace_rtt));
        
        // 优先使用平滑后的GCC带宽（真实场景可用）
        double smoothed_bw = GetSmoothedGccBandwidth(now, 5);  // 前5个样本平均
        std::cout << "smoothed_bw: " << smoothed_bw << std::endl;
        if (smoothed_bw > 0) {
            trace_bw = smoothed_bw;
        } else if (bw_changer_) {
            // 仿真场景回退到trace文件
            trace_bw = bw_changer_->GetTraceBandwidthAtTime(now.GetMilliSeconds());
        }
        std::cout << "trace_bw: " << trace_bw << std::endl;
        // Get GCC bandwidth from history
        double gcc_bw = GetNearestGccBandwidth(now);
        if (gcc_bw <= 0) gcc_bw = trace_bw * 0.7; // Fallback
        
        uint32_t Rt = rl_manager_->CalculateTransmissionOpportunities(now, frame_stats.playout_deadline, 
                                                                    info.packet_size, trace_bw);
        
        double mu = rl_manager_->GetCurrentMu();
        if (oscc_controller_ && oscc_controller_->IsEnabled()) {
            double oscc_mu = oscc_controller_->GetMuForPacket(info.frame_id, Rt);
            if (std::abs(oscc_mu - mu) > 0.001) {
                mu = oscc_mu;
                rl_manager_->SetMu(mu);
                if (webrtc_sender_) webrtc_sender_->UpdateMuDynamic(mu);
            }
        }
        
        // Calculate Reward
        // Use is_first_packet to determine "packet_index" logic (0 vs non-0)
        uint32_t packet_idx = info.is_first_packet ? 0 : 1; 
        
        double miss_deadline_time = 0.0;
        if (now > frame_stats.playout_deadline) {
            miss_deadline_time = (now - frame_stats.playout_deadline).GetSeconds();
        }

        double reward = rl_manager_->CalculateReward(mu, gcc_bw, trace_bw, delay_ms, trace_loss, 
                                                    miss_deadline_time, Rt, rl_manager_->GetLastPacketRt(), 
                                                    info.frame_id, packet_idx);
                                                    
        // Calculate p_delay, p_loss, etc. for logging (simplified here, logic is in CalculateReward mostly)
        // We just record what we have.
        double bw_util = (gcc_bw * mu) / trace_bw;
        
        rl_manager_->RecordPacketState(info.frame_id, packet_idx, mu, Rt, trace_loss, reward, 
                                       send_time, now, frame_stats.playout_deadline, 
                                       bw_util, 0, 0, 0, delay_ms);
    }
    
    // Callback from FramePlayoutManager
    void OnFrameComplete(const FrameStatistics& stats) {
        if (!oscc_controller_) return;
        
        // Calculate simplified QoE metrics based on stats
        double bandwidth_utilization = 0.5; // Todo: refine
        if (!bandwidth_history_.empty()) {
             BandwidthRecord bw_record = bandwidth_history_.back();
             if (bw_record.trace_bandwidth > 0)
                bandwidth_utilization = bw_record.scaled_bandwidth / bw_record.trace_bandwidth;
        }
        
        double qoe_recv = 100.0 * bandwidth_utilization;
        double qoe_loss = 100.0 * (1.0 - (bw_changer_ ? bw_changer_->GetLossAtTime(Simulator::Now().GetMilliSeconds()) : 0.01));
        
        double ddl_miss_rate = stats.played_on_time ? 0.0 : 1.0;
        double qoe_ddl = 100.0 * (1.0 - ddl_miss_rate);
        
        // Delay metric
        // We need average delay of packets in this frame. 
        // RLStateManager records all packets. We can query it?
        // Or just use (receive_complete_time - send_time) as a rough estimate for the frame.
        double frame_delay_ms = (stats.receive_complete_time - stats.send_time).GetMilliSeconds();
        double qoe_delay = 100.0;
        if (frame_delay_ms > 400) qoe_delay = 0;
        else if (frame_delay_ms > 50) qoe_delay = 100 - (frame_delay_ms - 50)*(100.0/350.0);
        
        double qoe = 0.2 * qoe_recv + 0.2 * qoe_delay + 0.3 * qoe_loss + 0.3 * qoe_ddl;
        
        oscc_controller_->OnFrameComplete(stats.frame_id, qoe, bandwidth_utilization, (100-qoe_loss)/100.0, 
                                          frame_delay_ms, frame_delay_ms, ddl_miss_rate, 
                                          qoe_recv, qoe_delay, qoe_loss, qoe_ddl);
                                          
         if (rl_manager_) {
            rl_manager_->SetMu(oscc_controller_->GetCurrentMu());
        }
    }
    
    // ============================================================================
    // RTCP 反馈处理：更新 OSCC 滑动窗口
    // ============================================================================
    
    // 当检测到丢包时调用（例如通过 RTCP TransportFeedback）
    void OnPacketLost(uint32_t seq_num) {
        if (oscc_controller_ && oscc_controller_->IsEnabled()) {
            oscc_controller_->UpdateLossWindow(true);  // true = 包丢失
            NS_LOG_DEBUG("QoEIntegrationManager: Packet " << seq_num << " lost, updated LossWindow");
        }
    }
    
    // 批量处理 TransportFeedback
    void OnTransportFeedback(const std::vector<bool>& packet_received) {
        if (!oscc_controller_ || !oscc_controller_->IsEnabled()) return;
        
        for (bool received : packet_received) {
            oscc_controller_->UpdateLossWindow(!received);  // true = lost
        }
        
        NS_LOG_DEBUG("QoEIntegrationManager: Processed " << packet_received.size() 
                    << " feedback entries, current loss rate: " 
                    << oscc_controller_->GetCurrentWindowLoss());
    }
    
    // 根据 trace 中的 loss 值模拟丢包反馈
    void SimulateLossFromTrace() {
        if (!oscc_controller_ || !oscc_controller_->IsEnabled() || !bw_changer_) return;
        
        double trace_loss = bw_changer_->GetLossAtTime(Simulator::Now().GetMilliSeconds());
        
        // 根据 trace loss 概率决定是否模拟丢包
        // 使用简单的随机模拟
        static std::default_random_engine generator(std::random_device{}());
        std::uniform_real_distribution<double> distribution(0.0, 1.0);
        
        bool packet_lost = (distribution(generator) < trace_loss);
        oscc_controller_->UpdateLossWindow(packet_lost);
    }
    
    // Bandwidth History (Legacy interface for FrameAwareWebrtcTrace)
    void AddBandwidthRecord(Time timestamp, double trace_bw, double gcc_bw, double scaled_bw, double mu) {
        Time cleanup_threshold = timestamp - Seconds(60);
        while (!bandwidth_history_.empty() && bandwidth_history_.front().timestamp < cleanup_threshold) {
            bandwidth_history_.pop_front();
        }
        bandwidth_history_.push_back(BandwidthRecord(timestamp, trace_bw, gcc_bw, scaled_bw, mu));
    }
    
    const std::deque<BandwidthRecord>& GetBandwidthHistory() const {
        return bandwidth_history_;
    }
    
    double GetNearestGccBandwidth(Time timestamp) const {
        if (bandwidth_history_.empty()) return 0.0;
        // Simple search
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
    
    // 获取平滑后的GCC带宽（滑动窗口平均）
    double GetSmoothedGccBandwidth(Time timestamp, int window_size = 5) const {
        if (bandwidth_history_.empty()) return 0.0;
        
        // 收集最近 window_size 个带宽样本
        std::vector<double> recent_bw;
        for (auto it = bandwidth_history_.rbegin(); 
             it != bandwidth_history_.rend() && recent_bw.size() < static_cast<size_t>(window_size); 
             ++it) {
            if (it->gcc_bandwidth > 0) {
                recent_bw.push_back(it->gcc_bandwidth);
            }
        }
        
        if (recent_bw.empty()) return 0.0;
        
        // 计算平均值
        double sum = 0.0;
        for (double bw : recent_bw) {
            sum += bw;
        }
        return sum / recent_bw.size();
    }
    
    // Skip Frame Proxy (Legacy interface)
    void SetSkipFrameCallback(std::function<void(uint32_t)> callback) {
        // This is usually set by InstallWebrtcApplication to notify sender
        // But FramePlayoutManager handles this now. 
        // We can keep it if needed for manual triggers or remove if redundant.
        // For now, keep as no-op or implementation if required.
        skip_callback_ = callback;
    }
    
    // Methods to support legacy interface in InstallWebrtcApplication
    void SetVideoTraceManager(void* unused) {} 

    void SetFrameAwareWebrtcTrace(void* unused) {} // Placeholder

    // Output bandwidth history to file
    void OutputBandwidthHistory(const std::string& filename) const {
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

private:
    OSCCController* oscc_controller_;
    RLStateManager* rl_manager_;
    BandwidthChanger* bw_changer_;
    Ptr<WebrtcSender> webrtc_sender_;
    std::deque<BandwidthRecord> bandwidth_history_;
    std::function<void(uint32_t)> skip_callback_;
};


// 增强的WebrtcTrace类来支持帧管理和带宽缩放统计
class FrameAwareWebrtcTrace : public WebrtcTrace {
public:
    // 定义回调类型
    typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;
    typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;
    typedef Callback<uint32_t, uint32_t> GetTraceBandwidthCallback;  // 新增：获取trace带宽的回调
    
    FrameAwareWebrtcTrace(QoEIntegrationManager* qoe_manager = nullptr, RLStateManager* rl_manager = nullptr, 
                         double bandwidth_scale_factor = 1.0) 
        : qoe_manager_(qoe_manager), rl_manager_(rl_manager), 
          total_bw_changes(0), bandwidth_scale_factor_(bandwidth_scale_factor),
          m_changer(nullptr), current_mu(1.0), current_loss_rate(0.01) {
        NS_LOG_INFO("FrameAwareWebrtcTrace created with bandwidth scale factor: " << bandwidth_scale_factor_);
    }
    
    virtual ~FrameAwareWebrtcTrace() {}
    
    // 设置当前mu和loss率参数（用于文件名生成）
    void SetCurrentParameters(double mu, double loss_rate) {
        current_mu = mu;
        current_loss_rate = loss_rate;
        NS_LOG_INFO("FrameAwareWebrtcTrace parameters updated: μ=" << mu << ", L=" << loss_rate);
    }
    
    // 设置回调函数
    void SetBwTraceFuc(TraceBandwidth cb) {
        m_traceBw = cb;
        NS_LOG_INFO("Bandwidth trace callback set in FrameAwareWebrtcTrace");
    }
    
    void SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
        m_traceScaledBw = cb;
        NS_LOG_INFO("Scaled bandwidth trace callback set in FrameAwareWebrtcTrace");
    }
    
    // 新增：设置获取trace带宽的回调
    void SetTraceBandwidthCallback(GetTraceBandwidthCallback cb) {
        m_getTraceBw = cb;
        NS_LOG_INFO("Trace bandwidth callback set in FrameAwareWebrtcTrace");
    }
    
    // 新增：直接设置带宽changer（更简单的方法）
    void SetBandwidthChanger(BandwidthChanger* changer) {
        m_changer = changer;
        NS_LOG_INFO("BandwidthChanger set in FrameAwareWebrtcTrace");
    }

    // 重写包到达信息处理函数
    // now: 接收时间（毫秒）
    // seq: 包序列号
    // owd: 单向延迟（毫秒）= now - send_time
    virtual void OnReceiptPktInfo(uint32_t now, uint32_t seq, uint32_t owd) {
        NS_LOG_DEBUG("FrameAwareWebrtcTrace::OnReceiptPktInfo called - time: " << now 
                     << "ms, seq: " << seq << ", owd: " << owd << "ms");
        
        // 调用父类处理 - 修改为写入三列数据
        WebrtcTrace::OnReceiptPktInfo(now, seq, owd);
        
        // 从trace获取当前的RTT和loss值
        double trace_rtt_ms = 30.0;  // 默认值
        double trace_loss_rate = 0.01;  // 默认值
        
        if (m_changer) {
            trace_rtt_ms = m_changer->GetRTTAtTime(now);
            trace_loss_rate = m_changer->GetLossAtTime(now);
        }
        
        // 更新RL状态管理器中的当前延迟
        // 注意：owd 已经是毫秒单位（now 和 tag.GetTime() 都是毫秒）
        if (rl_manager_) {
            double current_delay_ms = static_cast<double>(owd);  // owd 已经是毫秒
            rl_manager_->UpdateNetworkState(current_delay_ms, trace_loss_rate, MilliSeconds(trace_rtt_ms));
            NS_LOG_DEBUG("Updated current delay in RL manager: " << current_delay_ms << "ms, RTT=" 
                       << trace_rtt_ms << "ms, loss=" << trace_loss_rate);
        }
        
        // Note: Packet arrival processing for FrameManager removed. 
        // FramePlayoutManager handles it directly.
    }
    
    // 修改：重写Log函数以包含μ和L参数
    void Log(const std::string& name, uint32_t flags) {
        // 生成包含μ和L参数的文件名
        std::string filename = name;
        if (filename.find("_gcc_1") != std::string::npos) {
            // 移除可能的已有后缀
            size_t pos = filename.find("_mu=");
            if (pos != std::string::npos) {
                filename = filename.substr(0, pos);
            }
            pos = filename.find("_L=");
            if (pos != std::string::npos) {
                filename = filename.substr(0, pos);
            }
            
            // 添加当前μ和L参数
            filename += "_mu=" + std::to_string(current_mu) + 
                       "_L=" + std::to_string(current_loss_rate);
        }
        
        // 调用基类Log函数
        WebrtcTrace::Log(filename, flags);
        
        NS_LOG_INFO("FrameAwareWebrtcTrace logging to files with suffix: _mu=" 
                   << current_mu << "_L=" << current_loss_rate);
    }
    
    // 重写带宽估计回调，记录原始和缩放带宽
    virtual void OnBW(uint32_t now, uint32_t bps) {
        std::cout << "\n[GCC-DEBUG] OnBW called at time: " << now << "ms, bandwidth: " << bps << " bps" << std::endl;
        
        // 调用父类处理 - 这会写入_gcc_1_bw文件
        WebrtcTrace::OnBW(now, bps);
        
        // 记录原始带宽
        total_bw_changes++;
        original_bw_history.push_back(std::make_pair(now, bps));
        
        std::cout << "[GCC-Original] Time: " << now << "ms, BW: " << bps << " bps (" 
                << (bps / 1000000.0) << " Mbps)" << std::endl;

        // 获取当前的μ值
        double current_mu_val = 1.0;
        if (rl_manager_) {
            current_mu_val = rl_manager_->GetCurrentMu();
            std::cout << "[DEBUG] Got current mu from RL manager: " << current_mu_val << std::endl;
        } else {
            std::cout << "[WARNING] RL manager is null, using default mu=1.0" << std::endl;
        }
        
        // 计算缩放后的带宽
        uint32_t scaled_bw = static_cast<uint32_t>(bps * current_mu_val);
        
        // 记录缩放带宽历史
        scaled_bw_history.push_back(std::make_tuple(now, bps, scaled_bw, current_mu_val));
        
        // 获取当前时间的真实trace数据
        TraceData trace_data;
        if (m_changer) {
            trace_data = m_changer->GetTraceDataAtTime(now);
            std::cout << "[Real-Trace-Data] Time: " << now << "ms:" << std::endl;
            std::cout << "  Real Trace BW: " << trace_data.bandwidth << " bps (" 
                    << (trace_data.bandwidth / 1000000.0) << " Mbps)" << std::endl;
            std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
            std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
        } else {
            // 如果没有BandwidthChanger，使用GCC带宽作为估计
            trace_data.bandwidth = bps;
            trace_data.rtt = 30.0;
            trace_data.loss = 0.01;
            std::cout << "[WARNING] No BandwidthChanger available, using default trace data" << std::endl;
        }
        
        // 更新FrameManager中的带宽信息 - 关键修改
        if (qoe_manager_) {
            Time timestamp = MilliSeconds(now);
            
            // 使用提供的公有方法而不是直接访问私有成员
            const auto& history = qoe_manager_->GetBandwidthHistory();
            bool should_add = true;
            if (!history.empty()) {
                // 检查是否与最后一条记录的时间戳相同
                const auto& last_record = history.back();
                if (Abs(timestamp - last_record.timestamp) < MilliSeconds(1)) {
                    should_add = false;
                    std::cout << "[FrameManager-Update] Similar timestamp exists, skipping duplicate record" << std::endl;
                }
            }
            
            if (should_add) {
                qoe_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, bps, scaled_bw, current_mu_val);
                
                std::cout << "[FrameManager-Bandwidth] Time: " << now << "ms:" << std::endl;
                std::cout << "  Trace BW: " << trace_data.bandwidth << " bps" << std::endl;
                std::cout << "  GCC BW: " << bps << " bps" << std::endl;
                std::cout << "  Scaled BW: " << scaled_bw << " bps (μ=" << current_mu_val << ")" << std::endl;
                std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
                std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
                std::cout << "  Bandwidth history size: " << history.size() + 1 << std::endl;
            }
        }
        
        if (!m_traceBw.IsNull()) {
            m_traceBw(now, bps);
        }
        
        if (!m_traceScaledBw.IsNull()) {
            m_traceScaledBw(now, bps, scaled_bw, current_mu_val);
        }
    }
    
    // 记录缩放后的带宽 - 这个函数会被WebrtcSender调用
    void OnScaledBandwidth(uint32_t now, uint32_t original_bps, uint32_t scaled_bps, double scale_factor) {
        NS_LOG_INFO("Scaled Bandwidth - Time: " << now << "ms, Original: " << original_bps 
                   << " bps, Scaled: " << scaled_bps << " bps, μ: " << scale_factor);
        
        scaled_bw_history.push_back(std::make_tuple(now, original_bps, scaled_bps, scale_factor));
        
        std::cout << "[GCC-Scaled] Time: " << now << "ms, Original: " << original_bps 
                  << " bps -> Scaled: " << scaled_bps << " bps (μ=" << scale_factor << ")" << std::endl;
        
        // 同时更新原始带宽历史记录，确保时间戳匹配
        bool found = false;
        for (auto& entry : original_bw_history) {
            if (entry.first == now) {
                entry.second = original_bps;
                found = true;
                break;
            }
        }
        if (!found) {
            original_bw_history.push_back(std::make_pair(now, original_bps));
        }
        
        // 更新FrameManager中的带宽信息
        if (qoe_manager_) {
            Time timestamp = MilliSeconds(now);
            
            // 获取当前时间的真实trace数据
            TraceData trace_data;
            if (m_changer) {
                trace_data = m_changer->GetTraceDataAtTime(now);
            } else {
                trace_data.bandwidth = original_bps; // 使用GCC带宽作为trace带宽
                trace_data.rtt = 30.0;
                trace_data.loss = 0.01;
            }
            
            qoe_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, original_bps, scaled_bps, scale_factor);
            
            std::cout << "[FrameManager-Update] Updated with scaled bandwidth:" << std::endl;
            std::cout << "  Time: " << now << "ms" << std::endl;
            std::cout << "  Trace BW: " << trace_data.bandwidth << " bps" << std::endl;
            std::cout << "  GCC BW: " << original_bps << " bps" << std::endl;
            std::cout << "  Scaled BW: " << scaled_bps << " bps (μ=" << scale_factor << ")" << std::endl;
            std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
            std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
        }
    }
    
    // 输出带宽统计（包含原始和缩放后的）- 修改为包含trace带宽、RTT和loss
    void OutputBandwidthStatistics(const std::string& filename, double loss_rate = 0.01) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open bandwidth statistics file: " << filename);
            return;
        }
        
        file << "# Bandwidth scale factor (μ): " << bandwidth_scale_factor_ << std::endl;
        file << "# Loss rate (L): " << loss_rate << std::endl;
        // 修改表头，添加trace_bandwidth_bps、trace_rtt_ms和trace_loss
        file << "timestamp_ms,trace_bandwidth_bps,trace_bandwidth_mbps,trace_rtt_ms,trace_loss,original_bandwidth_bps,original_bandwidth_mbps,"
            << "scaled_bandwidth_bps,scaled_bandwidth_mbps,scale_factor" << std::endl;
        
        std::set<uint32_t> processed_timestamps;
        
        for (const auto& scaled_entry : scaled_bw_history) {
            uint32_t timestamp = std::get<0>(scaled_entry);
            uint32_t original_bw = std::get<1>(scaled_entry);
            uint32_t scaled_bw = std::get<2>(scaled_entry);
            double scale_factor = std::get<3>(scaled_entry);

            // 如果有OSCC控制器，使用动态μ值替代发送时记录的值
            if (oscc_controller_) {
                double dynamic_mu = GetMuAtTimestamp(timestamp / 1000.0);  // 转换为秒
                scale_factor = dynamic_mu;
                // 重新计算缩放后的带宽
                scaled_bw = static_cast<uint32_t>(original_bw * dynamic_mu);
            }
            
            // 获取当前时刻的trace数据
            TraceData trace_data;
            if (m_changer) {
                trace_data = m_changer->GetTraceDataAtTime(timestamp);
            } else if (!m_getTraceBw.IsNull()) {
                trace_data.bandwidth = m_getTraceBw(timestamp);
                trace_data.rtt = 30.0;  // 默认值
                trace_data.loss = 0.01; // 默认值
            } else {
                // 如果没有回调，尝试使用原始带宽作为trace带宽
                trace_data.bandwidth = original_bw;
                trace_data.rtt = 30.0;
                trace_data.loss = 0.01;
                NS_LOG_WARN("No trace data callback available, using default values");
            }
            
            file << timestamp << "," 
                 << trace_data.bandwidth << "," << (trace_data.bandwidth / 1000000.0) << ","
                 << trace_data.rtt << "," << trace_data.loss << ","
                 << original_bw << "," << (original_bw / 1000000.0) << ","
                 << scaled_bw << "," << (scaled_bw / 1000000.0) << "," 
                 << scale_factor << std::endl;
            
            processed_timestamps.insert(timestamp);
        }
        
        // 处理只有原始带宽数据的条目
        for (const auto& original_entry : original_bw_history) {
            uint32_t timestamp = original_entry.first;
            uint32_t original_bw = original_entry.second;
            
            if (processed_timestamps.find(timestamp) == processed_timestamps.end()) {
                uint32_t scaled_bw = original_bw;
                double scale_factor = 1.0;
                
                // 获取当前时刻的trace数据
                TraceData trace_data;
                if (m_changer) {
                    trace_data = m_changer->GetTraceDataAtTime(timestamp);
                } else if (!m_getTraceBw.IsNull()) {
                    trace_data.bandwidth = m_getTraceBw(timestamp);
                    trace_data.rtt = 30.0;
                    trace_data.loss = 0.01;
                } else {
                    trace_data.bandwidth = original_bw;
                    trace_data.rtt = 30.0;
                    trace_data.loss = 0.01;
                }
                
                file << timestamp << ","
                     << trace_data.bandwidth << "," << (trace_data.bandwidth / 1000000.0) << ","
                     << trace_data.rtt << "," << trace_data.loss << ","
                     << original_bw << "," << (original_bw / 1000000.0) << ","
                     << scaled_bw << "," << (scaled_bw / 1000000.0) << "," 
                     << scale_factor << std::endl;
            }
        }
        
        file.close();
        
        NS_LOG_INFO("Bandwidth statistics saved to: " << filename 
                << " with μ=" << bandwidth_scale_factor_ << ", L=" << loss_rate);
        std::cout << "Bandwidth statistics saved to: " << filename 
                  << " (10 columns: timestamp_ms, trace_bandwidth_bps, trace_bandwidth_mbps, trace_rtt_ms, trace_loss, "
                  << "original_bandwidth_bps, original_bandwidth_mbps, "
                  << "scaled_bandwidth_bps, scaled_bandwidth_mbps, scale_factor)" << std::endl;
        
        // 输出样本数据以验证
        if (!scaled_bw_history.empty()) {
            auto sample = scaled_bw_history[0];
            std::cout << "Sample data from bandwidth statistics:" << std::endl;
            std::cout << "  Timestamp: " << std::get<0>(sample) << "ms" << std::endl;
            std::cout << "  Original BW: " << std::get<1>(sample) << " bps" << std::endl;
            std::cout << "  Scaled BW: " << std::get<2>(sample) << " bps" << std::endl;
            std::cout << "  Scale factor: " << std::get<3>(sample) << std::endl;
        }
    }
    
    // 获取带宽统计摘要
    void GetBandwidthStats(uint32_t& total_changes, uint32_t& avg_original_bw, uint32_t& avg_scaled_bw) const {
        total_changes = total_bw_changes;
        
        if (!original_bw_history.empty()) {
            uint64_t sum_original = 0;
            for (const auto& entry : original_bw_history) {
                sum_original += entry.second;
            }
            avg_original_bw = sum_original / original_bw_history.size();
        } else {
            avg_original_bw = 0;
        }
        
        if (!scaled_bw_history.empty()) {
            uint64_t sum_scaled = 0;
            for (const auto& entry : scaled_bw_history) {
                sum_scaled += std::get<2>(entry);
            }
            avg_scaled_bw = sum_scaled / scaled_bw_history.size();
        } else {
            avg_scaled_bw = 0;
        }
    }
    
    void SetQoEManager(QoEIntegrationManager* qoe_manager) {
        qoe_manager_ = qoe_manager;
        NS_LOG_INFO("QoEIntegrationManager set in FrameAwareWebrtcTrace");
    }
    
    void SetRLStateManager(RLStateManager* rl_manager) {
        rl_manager_ = rl_manager;
        NS_LOG_INFO("RLStateManager set in FrameAwareWebrtcTrace");
    }
    
    void SetBandwidthScaleFactor(double factor) {
        bandwidth_scale_factor_ = factor;
        NS_LOG_INFO("Bandwidth scale factor updated to: " << bandwidth_scale_factor_);
    }
    
    // 新增：设置OSCCController用于获取动态μ值
    void SetOSCCController(OSCCController* controller) {
        oscc_controller_ = controller;
        NS_LOG_INFO("OSCCController set in FrameAwareWebrtcTrace");
    }
    
    // 新增：根据时间戳获取对应的μ值（从OSCC变化记录中查找）
    double GetMuAtTimestamp(double timestamp_s) const {
        if (!oscc_controller_) {
            return bandwidth_scale_factor_;
        }
        
        const auto& records = oscc_controller_->GetMuChangeRecords();
        if (records.empty()) {
            return 1.0;  // 如果没有记录，返回初始值
        }
        
        // 查找小于等于给定时间戳的最后一条记录
        double mu_at_time = 1.0;
        for (const auto& record : records) {
            if (record.timestamp.GetSeconds() <= timestamp_s) {
                mu_at_time = record.new_mu;
            } else {
                break;  // 记录是按时间排序的，可以提前退出
            }
        }
        return mu_at_time;
    }

private:
    QoEIntegrationManager* qoe_manager_;
    RLStateManager* rl_manager_;
    uint32_t total_bw_changes;
    double bandwidth_scale_factor_;
    std::vector<std::pair<uint32_t, uint32_t>> original_bw_history;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, double>> scaled_bw_history;

    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    GetTraceBandwidthCallback m_getTraceBw;  // 新增：获取trace带宽的回调
    BandwidthChanger* m_changer;  // 新增：直接存储带宽changer指针
    OSCCController* oscc_controller_ = nullptr;  // 新增：OSCC控制器指针
    
    // 新增：当前参数值（用于文件名生成）
    double current_mu;
    double current_loss_rate;
};


// 函数声明
void TriggerRLStateCalculation(RLStateManager* rl_manager,
                              BandwidthChanger* bandwidth_changer, 
                              Ptr<ExponentialRandomVariable> interval,
                              double current_loss_rate);

static void InstallWebrtcApplication(Ptr<Node> sender,
                        Ptr<Node> receiver,
                        uint16_t send_port,
                        uint16_t recv_port,
                        Time start_app,
                        Time stop_app,
                        WebrtcSessionManager *manager,
                        FrameAwareWebrtcTrace *trace = nullptr,
                        QoEIntegrationManager* qoe_manager = nullptr,
                        RLStateManager* rl_manager = nullptr,
                        double bandwidth_scale_factor = 1.0,
                        double loss_rate = 0.01,
                        BandwidthChanger* bandwidth_changer = nullptr,
                        FramePlayoutManager* frame_playout_manager = nullptr,
                        bool skip_frame_enabled = false)
{
    std::cout << "\n[DEBUG] InstallWebrtcApplication called" << std::endl;
    std::cout << "  Bandwidth scale factor: " << bandwidth_scale_factor << std::endl;
    std::cout << "  Loss rate: " << loss_rate << std::endl;
    std::cout << "  Skip frame enabled: " << (skip_frame_enabled ? "YES" : "NO") << std::endl;
    
    NS_LOG_INFO("Installing WebRTC application with RL state management and real frame analysis");
    
    // 正确创建应用程序对象
    Ptr<WebrtcSender> sendApp = CreateObject<WebrtcSender>(manager);
    Ptr<WebrtcReceiver> recvApp = CreateObject<WebrtcReceiver>(manager);
    
    // ============ 集成 FramePlayoutManager ============
    if (frame_playout_manager && recvApp) {
        recvApp->SetFramePlayoutManager(frame_playout_manager);
        // 设置跳帧开关
        frame_playout_manager->SetSkipFrameEnabled(skip_frame_enabled);
        std::cout << "[DEBUG] FramePlayoutManager set in WebrtcReceiver with skip_enabled=" << skip_frame_enabled << std::endl;
        
        // 设置跳帧回调：当接收端触发跳帧时，通知发送端
        if (sendApp) {
            frame_playout_manager->SetSkipFrameCallback([sendApp](uint32_t target_keyframe_id) {
                std::cout << "[FramePlayoutManager] Skip frame callback triggered, target keyframe: " 
                          << target_keyframe_id << std::endl;
                // 调用发送端的跳帧接口
                if (sendApp) {
                    sendApp->SkipToFrame(target_keyframe_id);
                    std::cout << "[FramePlayoutManager] Notified sender to skip to keyframe " 
                              << target_keyframe_id << std::endl;
                }
            });
            std::cout << "[DEBUG] Skip frame callback registered in FramePlayoutManager" << std::endl;
        }
        
        // 连接 QoEIntegrationManager 到 FramePlayoutManager
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
    // ============ FramePlayoutManager 集成结束 ============
    
    // 设置带宽缩放系数
    if (sendApp) {
        sendApp->SetBandwidthScaleFactor(bandwidth_scale_factor);
        NS_LOG_INFO("WebrtcSender bandwidth scale factor set to: " << bandwidth_scale_factor);
    }
    
    // 设置缩放带宽回调
    if (trace && sendApp) {
        sendApp->SetScaledBwTraceFuc(MakeCallback(&FrameAwareWebrtcTrace::OnScaledBandwidth, trace));
        NS_LOG_INFO("Scaled bandwidth callback set for WebrtcSender");
    }
    
    // 设置QoEIntegrationManager
    if (qoe_manager) {
        if (rl_manager) qoe_manager->SetRLStateManager(rl_manager);
        if (bandwidth_changer) qoe_manager->SetBandwidthChanger(bandwidth_changer);
        if (sendApp) qoe_manager->SetWebrtcSender(sendApp);
        if (trace) qoe_manager->SetFrameAwareWebrtcTrace(trace);
        
        // OSCC集成：关键修复，同时设置OSCCController的WebrtcSender
        OSCCController* oscc = qoe_manager->GetOSCCController();
        if (oscc && sendApp) {
            oscc->SetWebrtcSender(sendApp);
            std::cout << "[DEBUG] WebrtcSender set in OSCCController for direct mu application" << std::endl;
        }
    }
    
    // 设置trace带宽changer - 更简单的方法
    if (trace && bandwidth_changer) {
        trace->SetBandwidthChanger(bandwidth_changer);
        std::cout << "[DEBUG] BandwidthChanger set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    // 设置trace的当前参数（用于文件名生成）
    if (trace) {
        trace->SetCurrentParameters(bandwidth_scale_factor, loss_rate);
    }
    
    // RL状态计算调度 - 添加空指针检查
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Ptr<ExponentialRandomVariable> interval = CreateObject<ExponentialRandomVariable>();
        interval->SetAttribute("Mean", DoubleValue(0.01));
        Simulator::Schedule(Seconds(0.1), &TriggerRLStateCalculation,
                        rl_manager, bandwidth_changer, interval, loss_rate);
    }
    
    // 基本应用程序设置
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
    
    // 设置跟踪回调
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
    
    // 关联管理器
    if (qoe_manager && trace) {
        trace->SetQoEManager(qoe_manager);
        std::cout << "[DEBUG] QoEIntegrationManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    if (rl_manager && trace) {
        trace->SetRLStateManager(rl_manager);
        std::cout << "[DEBUG] RLStateManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    // 设置应用程序时间
    sendApp->SetStartTime(start_app);
    sendApp->SetStopTime(stop_app);
    recvApp->SetStartTime(start_app);
    recvApp->SetStopTime(stop_app + Seconds(1));
    
    std::cout << "[SUCCESS] WebRTC application installed successfully" << std::endl;
}

// 修改 TriggerRLStateCalculation 函数，使用trace中的RTT和loss数据
void TriggerRLStateCalculation(RLStateManager* rl_manager,
                              BandwidthChanger* bandwidth_changer, 
                              Ptr<ExponentialRandomVariable> interval,
                              double current_loss_rate)
{
    Time now = Simulator::Now();
    uint32_t timestamp_ms = static_cast<uint32_t>(now.GetMilliSeconds());
    
    // 从trace获取当前的RTT和loss值
    double trace_rtt_ms = 30.0;  // 默认值
    double trace_loss_rate = 0.01;  // 默认值
    
    if (bandwidth_changer != nullptr) {
        trace_rtt_ms = bandwidth_changer->GetRTTAtTime(timestamp_ms);
        trace_loss_rate = bandwidth_changer->GetLossAtTime(timestamp_ms);
    } 
    
    // 获取当前网络状态
    double current_trace_bw = 0.0;
    if (bandwidth_changer != nullptr) {
        current_trace_bw = bandwidth_changer->GetCurrentTraceBandwidth();
    } else {
        current_trace_bw = 20*1000000.0; // 20 Mbps 默认值
    }
    
    // 使用RL状态管理器中的当前延迟（这个值会在每次包到达时更新）
    double current_delay = rl_manager->GetCurrentDelay();
    
    // 更新网络状态 - 使用实际延迟和trace中的RTT、loss
    if (rl_manager != nullptr) {
        rl_manager->UpdateNetworkState(current_delay, trace_loss_rate, MilliSeconds(trace_rtt_ms));
    }
    
    // 安排下一次触发
    double next_interval = interval->GetValue();
    
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Simulator::Schedule(Seconds(next_interval), &TriggerRLStateCalculation,
                           rl_manager, bandwidth_changer, interval, trace_loss_rate);
    }
}

uint64_t kMillisPerSecond=1000;
uint64_t kMicroPerMillis=1000;

std::unique_ptr<WebrtcSessionManager> CreateWebrtcSessionManager(webrtc::TimeController *controller,
uint32_t max_rate=20000,uint32_t min_rate=100,uint32_t start_rate=500,uint32_t h=720,uint32_t w=1280, uint32_t fps=30){
    std::unique_ptr<WebrtcSessionManager> webrtc_manager(new WebrtcSessionManager(controller,min_rate,start_rate,max_rate,h,w,fps));
    webrtc_manager->CreateClients();
    return webrtc_manager;
}

static const float startTime=0.001;

// 从trace文件路径提取父文件夹名称
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

// 修复 test_app_on_p2p 函数中的调用
void test_app_on_p2p (const std::string &instance, TimeConollerType controller_type, int num, 
                     float startapptime, float endapptime, double max_bandwith,
                     TriggerRandomLoss *trigger_loss, BandwidthChanger *changer, 
                     const std::string& trace_filename = "", double bandwidth_scale_factor = 1.0,
                     double loss_rate = 0.01,
                     bool oscc_mode = false,
                     uint32_t fps = 30,
                     const std::string& frame_trace_output = "",
                     bool skip_frame_enabled = false,
                     const std::string& base_output_folder = "trace_results")
{
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
    
    // Note: VideoTraceManager removed. We rely on real frames.

    
    uint64_t bps= max_bandwith * kBwUnit;
    uint32_t link_delay=20.0;
    uint32_t buffer_delay=15.0;

    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
    pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (link_delay)));
    auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, bps * buffer_delay / 8000);
    
    int packets=bufSize/DEFAULT_PACKET_SIZE;
    pointToPoint.SetQueue ("ns3::DropTailQueue",
                           "MaxSize", StringValue (std::to_string(5)+"p"));
    NetDeviceContainer devices = pointToPoint.Install (nodes);

    InternetStackHelper stack;
    stack.Install (nodes);

    TrafficControlHelper pfifoHelper;
    uint16_t handle = pfifoHelper.SetRootQueueDisc ("ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(packets)+"p"));
    pfifoHelper.AddInternalQueues (handle, 1, "ns3::DropTailQueue", "MaxSize",StringValue (std::to_string(packets)+"p"));
    
    TrafficControlHelper tch;
    tch.SetRootQueueDisc ("ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(packets)+"p"));
    QueueDiscContainer qdiscs = tch.Install (devices);

    Ipv4AddressHelper address;
    std::string nodeip="10.1.1.0";
    address.SetBase (nodeip.c_str(), "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign (devices);
    
    if(trigger_loss){
        trigger_loss->RegisterDevice(devices.Get(1));
        std::cout << "Trigger loss registered on receiver device with rate: " 
                << trigger_loss->GetLossRate() << std::endl;
    }

    if(changer){
        changer->RegisterDevice(devices.Get(0));
        std::cout << "Bandwidth changer registered on sender device" << std::endl;
        std::cout << "BandwidthChanger object address: " << changer << std::endl;  // 调试输出
    } else {
        std::cout << "WARNING: BandwidthChanger is null!" << std::endl;
    }

    std::string webrtc_log_com("_gcc_");
    
    int64_t webrtc_start_us = static_cast<int64_t>(startapptime * 1000000);
    int64_t webrtc_stop_us = static_cast<int64_t>(endapptime * 1000000);
    
    std::cout << "WebRTC controller time: " << webrtc_start_us << "us to " << webrtc_stop_us << "us" << std::endl;
    
    webrtc::TimeController* time_controller = CreateTimeController(controller_type, webrtc_start_us, webrtc_stop_us);
    uint32_t max_rate = bps / 1000;

    std::vector<std::unique_ptr<WebrtcSessionManager>> sesssion_manager;
    uint32_t default_frame_height = 1080;
    uint32_t default_frame_width = 1920;
    for (int i=0;i<num;i++) {
        // std::unique_ptr<WebrtcSessionManager> m(CreateWebrtcSessionManager(time_controller,max_rate*0.1,max_rate*0.2,max_rate,default_frame_height,default_frame_width, fps));
        std::unique_ptr<WebrtcSessionManager> m(CreateWebrtcSessionManager(time_controller,max_rate,max_rate*0.1,max_rate*0.2,default_frame_height,default_frame_width, fps));
        sesssion_manager.push_back(std::move(m)); 
    }
    UtilCalculator *calculator=UtilCalculator::Instance();
    calculator->Enable();
    
    uint16_t sendPort=5432;
    uint16_t recvPort=5000;
    
    std::string trace_base_name = trace_filename;
    size_t last_slash = trace_base_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        trace_base_name = trace_base_name.substr(last_slash + 1);
    }
    size_t last_dot = trace_base_name.find_last_of(".");
    if (last_dot != std::string::npos) {
        trace_base_name = trace_base_name.substr(0, last_dot);
    }
    
    std::string prefix=instance + "_" + trace_base_name + webrtc_log_com;
    std::vector<FrameAwareWebrtcTrace*> trace_vec;
    
    // Create Managers
    std::vector<std::unique_ptr<QoEIntegrationManager>> qoe_managers;
    std::vector<std::unique_ptr<FramePlayoutManager>> frame_playout_managers;
    std::vector<std::unique_ptr<RLStateManager>> rl_managers;
    std::vector<std::unique_ptr<OSCCController>> oscc_controllers;
    
    // Init FramePlayoutManager
    for (int i = 0; i < num; i++) {
        auto pm = std::make_unique<FramePlayoutManager>();
        pm->SetFPS(fps);
        frame_playout_managers.push_back(std::move(pm));
    }
    
    // Init RLStateManager
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
    
    // Init OSCCController
    if (oscc_mode) {
        for (int i = 0; i < num; i++) {
            auto oscc = std::make_unique<OSCCController>();
            oscc->SetParameters(0.02, 0.5, 1.5, bandwidth_scale_factor);
            oscc->SetEnabled(true);
            rl_managers[i]->SetOSCCController(oscc.get());
            oscc_controllers.push_back(std::move(oscc));
        }
    }
    
    // Init QoEIntegrationManager
    for (int i = 0; i < num; i++) {
        auto qoe = std::make_unique<QoEIntegrationManager>();
        qoe->SetRLStateManager(rl_managers[i].get());
        qoe->SetBandwidthChanger(changer);
        if (oscc_mode && i < (int)oscc_controllers.size()) {
            qoe->SetOSCCController(oscc_controllers[i].get());
        }
        qoe_managers.push_back(std::move(qoe));
    }
        
    for (int i=0;i<num;i++) {
        std::string log=prefix+std::to_string(i+1);
        FrameAwareWebrtcTrace *trace=new FrameAwareWebrtcTrace(qoe_managers[i].get(), rl_managers[i].get(), bandwidth_scale_factor);
        trace_vec.push_back(trace);
        
        trace->SetCurrentParameters(bandwidth_scale_factor, loss_rate);
        
        if (oscc_mode && i < static_cast<int>(oscc_controllers.size()) && oscc_controllers[i]) {
            trace->SetOSCCController(oscc_controllers[i].get());
        }
        
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
    
    Simulator::Stop (Seconds(simulation_stop_time));
    uint64_t last=get_os_millis();
    
    std::cout << "Starting simulation..." << std::endl;
    Simulator::Run ();
    std::cout << "Simulation completed at: " << Simulator::Now().GetSeconds() << " seconds" << std::endl;
    
    // ============ 导出 Trace ============
    for (int i = 0; i < num; i++) {
        // Frame Playout Trace
        std::string trace_output_file = frame_trace_output;
        if (!trace_output_file.empty()) {
            trace_output_file = base_output_folder + "/" + prefix + std::to_string(i+1) + "_frame_playout_trace.csv";
        }
        frame_playout_managers[i]->ExportFrameTrace(trace_output_file);
        
        // Output Bandwidth History from QoEManager
        std::string bw_history_file = base_output_folder + "/" + prefix + std::to_string(i+1) + "_bandwidth_history.csv";
        qoe_managers[i]->OutputBandwidthHistory(bw_history_file);
        
        // Output Bandwidth Statistics from Trace
        std::string bw_stats_file = base_output_folder + "/" + prefix + std::to_string(i+1) + "_mu=" + 
                                std::to_string(bandwidth_scale_factor) + "_L=" + 
                                std::to_string(loss_rate) + "_bandwidth_statistics.csv";
        trace_vec[i]->OutputBandwidthStatistics(bw_stats_file, loss_rate);
        
        // Output RL Records
        rl_managers[i]->OutputStateRecords(base_output_folder + "/" + prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
        rl_managers[i]->OutputRtGroupRewards(base_output_folder + "/" + prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
        
        // OSCC Stats
        if (oscc_mode && i < static_cast<int>(oscc_controllers.size()) && oscc_controllers[i]) {
            std::string mu_trace_file = base_output_folder + "/" + prefix + std::to_string(i+1) + "_OSCC_mu_trace.csv";
            oscc_controllers[i]->OutputMuTrace(mu_trace_file);
            std::string qoe_file = base_output_folder + "/" + prefix + std::to_string(i+1) + "_OSCC_qoe.csv";
            oscc_controllers[i]->OutputFrameQoE(qoe_file);
        }
    }
    
    Simulator::Destroy();
    std::cout << "Simulator destroyed" << std::endl;
    
    if(time_controller){
        delete time_controller;
        time_controller = nullptr;
    }
    
    {
        int64_t last_stamp=calculator->GetLastReceiptMillis();
        int64_t channnel_bit=0;
        
        if (changer){
            changer->TotalThroughput(MilliSeconds(last_stamp),channnel_bit);
        }else{
            if(last_stamp>startapptime*1000){
                double duration_seconds = (last_stamp - startapptime*1000) / 1000.0;
                channnel_bit = static_cast<int64_t>(bps * duration_seconds);
            }
        }
        
        calculator->CalculateUtil(prefix,channnel_bit);
    }

    for(auto it=trace_vec.begin();it!=trace_vec.end();it++){
        FrameAwareWebrtcTrace *trace=(*it);
        delete trace;
    }
    trace_vec.clear();
    
    uint32_t elapse=( get_os_millis() - last);
    std::cout<<"run time millis: "<<elapse<<std::endl;
    _exit(0);
}


void run_single_trace_simulation(const std::string& trace_file, const std::string& instance, 
                               TimeConollerType controller_type, int num, double max_bandwith,
                               double loss_rate, const std::string& base_output_folder = "Trace_Result",
                               double bandwidth_scale_factor=1.0, 
                               bool oscc_mode = false,
                               uint32_t fps = 30,
                               const std::string& frame_trace_output = "",
                               bool skip_frame_enabled = false)
{
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Starting simulation for: " << trace_file << std::endl;
    std::cout << "Instance: " << instance << std::endl;
    std::cout << "Max bandwidth: " << max_bandwith << " Mbps" << std::endl;
    std::cout << "Loss rate: " << loss_rate <<  " (THIS SHOULD BE 0.01, 0.02, etc.)" << std::endl;
    std::cout << "OSCC mode: " << (oscc_mode ? "ENABLED (dynamic μ adjustment)" : "disabled") << std::endl;
    if (oscc_mode) {
        std::cout << "Initial bandwidth scale factor μ: " << bandwidth_scale_factor << " (will be dynamically adjusted)" << std::endl;
    } else {
        std::cout << "Bandwidth scale factor μ: " << bandwidth_scale_factor << std::endl;
    }
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Skip frame: " << (skip_frame_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Frame trace output: " << (frame_trace_output.empty() ? "auto-generated" : frame_trace_output) << std::endl;
    std::cout << "Base output folder: " << base_output_folder << std::endl;
    std::cout << "==========================================" << std::endl;
    
    std::string data_result_folder = base_output_folder;
    
    std::cout << "Using output path: " << data_result_folder << std::endl;
    
    // 确保输出目录存在
    std::string create_dir_cmd = "mkdir -p " + data_result_folder;
    int result = system(create_dir_cmd.c_str());
    if (result != 0) {
        std::cerr << "Warning: Failed to create directory: " << data_result_folder << std::endl;
    } else {
        std::cout << "Confirmed output directory: " << data_result_folder << std::endl;
    }
    
    // 设置输出目录
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
    
    // 使用全局配置
    Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (loss_rate));
    Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
    Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (loss_rate));
    Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));
    
    // 创建 TriggerRandomLoss 并设置 loss_rate
    triggerloss.reset(new TriggerRandomLoss(loss_rate));
    
    changer.reset(new BandwidthChanger());
    
    std::cout << "Reading and normalizing trace file..." << std::endl;
    std::pair<float,float> during_time = changer->ConfigwithReadNetworkTrace(max_bandwith * kBwUnit, trace_file);
    
    if (during_time.second <= 0) {
        std::cerr << "ERROR: Failed to read trace file or file is empty: " << trace_file << std::endl;
        return;
    }
    
    changer->Start();
    if (triggerloss) {
        // 设置 BandwidthChanger 以启用从trace读取动态loss值
        triggerloss->SetBandwidthChanger(changer.get());
        triggerloss->SetUseTraceLoss(true);  // 启用动态更新
        triggerloss->SetUpdateInterval(100);  // 每100ms更新一次loss率
        triggerloss->Start();
        std::cout << "TriggerRandomLoss started with initial rate: " << loss_rate << std::endl;
        std::cout << "  Dynamic loss update from trace: ENABLED" << std::endl;
        std::cout << "  Update interval: 100ms" << std::endl;
    }
    
    float startapptime = during_time.first;
    float endapptime = during_time.second;
    
    std::cout << "Normalized simulation time range: " << startapptime << "s to " << endapptime << "s" << std::endl;
    std::cout << "Total simulation duration: " << (endapptime - startapptime) << "s" << std::endl;
    
    // 运行仿真
    test_app_on_p2p(instance, controller_type, num, startapptime, endapptime, 
                   max_bandwith, triggerloss.get(), changer.get(), trace_file, 
                   bandwidth_scale_factor, loss_rate, oscc_mode,
                   fps, frame_trace_output, skip_frame_enabled);
    
    std::cout << "Simulation completed successfully" << std::endl;
    
    if (triggerloss) {
        triggerloss.reset();
    }
    if (changer) {
        changer.reset();
    }
}

int main(int argc, char *argv[]){
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
    std::string frame_weight("1280");//分辨率  360*640 480*800 720*1280 1080*1920 1440*2560 2160*3840
    std::string frame_height("720");//
    std::string max_bandwidth("10");
    std::string loss_rate("0.01");
    std::string folder("trace_results");
    std::string bandwidth_scale("1.0");//mu
    std::string oscc_enabled("false");  // OSCC模式：动态μ调整
    std::string fps_str("30"); // 帧率参数
    std::string frame_trace_output("");  // 帧trace输出文件路径
    std::string skip_frame_str("false"); // 跳帧参数
    
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
    cmd.AddValue("oscc", "enable OSCC dynamic mu adjustment", oscc_enabled);  // OSCC参数
    cmd.AddValue("frame_trace", "frame trace output file path", frame_trace_output);
    cmd.AddValue("fps", "frame rate", fps_str);
    cmd.AddValue("skip", "enable skip frame logic", skip_frame_str);
    
    cmd.Parse(argc, argv);
    
    // 解析OSCC模式
    bool oscc_mode = (oscc_enabled == "true" || oscc_enabled == "1" || oscc_enabled == "yes");
    // 解析跳帧模式
    bool skip_frame_enabled = (skip_frame_str == "true" || skip_frame_str == "1" || skip_frame_str == "yes");
    
    // 验证必要参数
    if (trace_file.empty()) {
        std::cerr << "ERROR: No trace file specified. Use --trace=<file_path>" << std::endl;
        std::cerr << "Usage: ./waf --run \"scratch/webrtc-TFMN(RTT) --trace=<path> [--it=<instance> --folder=<output_dir> --mb=<bandwidth> --ls=<loss_rate> --mu=<scale_factor>]\"" << std::endl;
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
    TimeConollerType controller_type = TimeConollerType::SIMU_CONTROLLER;
    if (mode == "simu") {
        webrtc_register_clock();
        std::cout << "Using SIMU controller" << std::endl;
    } else if (mode == "emu") {
        controller_type = TimeConollerType::EMU_CONTROLLER;
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
    if (!frame_trace_output.empty()) {
        frame_trace_output = folder + "/" + instance + "_frame_trace.csv";
    }

    std::cout << "Starting single trace simulation with real frame analysis..." << std::endl;
    std::cout << "Max bandwidth: " << mb << " Mbps" << std::endl;
    std::cout << "Loss rate: " << ls << std::endl;
    std::cout << "OSCC mode: " << (oscc_mode ? "ENABLED" : "disabled") << std::endl;
    if (oscc_mode) {
        std::cout << "Initial bandwidth scale factor μ: " << mu << " (will be dynamically adjusted)" << std::endl;
    } else {
        std::cout << "Bandwidth scale factor μ: " << mu << std::endl;
    }
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "Skip frame: " << (skip_frame_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Frame trace output: " << frame_trace_output << std::endl;
    
    // 使用run_single_trace_simulation函数
    run_single_trace_simulation(trace_file, instance, controller_type, 1, mb, ls, folder, mu, oscc_mode,
                               fps, frame_trace_output, skip_frame_enabled);
    
    std::cout << "=== WebRTC TraceAll-Frame with Real Frame Analysis Completed Successfully ===" << std::endl;
    
    // 恢复std::cout并关闭日志文件
    std::cout.rdbuf(cout_buffer);
    log_file.close();
    
    _exit(0);
    return 0;
}

// hjt@ubuntu-Precision-Tower-5810:~/OSCC/ns-allinone-3.31/ns-3.31$ ./waf --run "scratch/webrtc-TFMN(QoE) --trace=/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/AItrans_2.log --ls=0.01 --skip=true --oscc=true --folder=trace_results/AItrans_test --it=AItrans_case1" > webrtc_ns3.log 2>&1
// hjt@ubuntu-Precision-Tower-5810:~/OSCC/ns-allinone-3.31/ns-3.31$ ./waf --run "scratch/webrtc-TFMN(GCC) --trace=/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/AItrans_2.log --ls=0.01 --skip=true --oscc=true --folder=trace_results/AItrans_test --it=AItrans_case1" > webrtc_ns3.log 2>&1