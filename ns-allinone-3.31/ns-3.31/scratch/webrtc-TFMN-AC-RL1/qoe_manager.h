#ifndef QOE_MANAGER_H
#define QOE_MANAGER_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <vector>

#include "common_types.h"
#include "mu_learner.h"
#include "rl_state_manager.h"
#include "network_components.h"
#include "ns3/webrtc-defines.h"
#include "ns3/ex-webrtc-module.h"
#include "ns3/frame-playout-manager.h"

namespace oscc {

using namespace ns3;

// ============================================================================
// QoEIntegrationManager - QoE集成管理器
// ============================================================================
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

    // Mu change record for mu_trace.csv (AI learner updates)
    struct MuChangeRecord {
        Time timestamp;
        uint32_t frame_id;
        uint32_t Rt_value;
        double old_mu;
        double new_mu;
        double loss_rate;
        double reward;
        MuChangeRecord() : timestamp(Seconds(0)), frame_id(0), Rt_value(0),
                           old_mu(1.0), new_mu(1.0), loss_rate(0.0), reward(0.0) {}
        MuChangeRecord(Time ts, uint32_t fid, uint32_t rt, double old_m, double new_m,
                       double loss, double r)
            : timestamp(ts), frame_id(fid), Rt_value(rt), old_mu(old_m), new_mu(new_m),
              loss_rate(loss), reward(r) {}
    };

    // Per-frame QoE summary for frame_qoe.csv
    struct FrameQoESummary {
        uint32_t frame_id;
        double bandwidth_utilization;
        double loss_rate;
        double delay_avg;
        double mu;
        double reward_avg;
        Time timestamp;
        FrameQoESummary() : frame_id(0), bandwidth_utilization(0.0), loss_rate(0.0),
                            delay_avg(0.0), mu(1.0), reward_avg(0.0), timestamp(Seconds(0)) {}
    };

    QoEIntegrationManager();

    // 设置各个组件
    void SetRLStateManager(RLStateManager* manager);
    void SetBandwidthChanger(BandwidthChanger* changer);
    void SetWebrtcSender(Ptr<WebrtcSender> sender);
    
    // MuLearner集成
    void SetMuLearner(IMuLearner* learner, bool use_learner = true);
    IMuLearner* GetMuLearner() { return mu_learner_; }
    bool IsLearnerEnabled() const { return use_learner_ && mu_learner_ != nullptr; }
    void SetUseLearner(bool use) { use_learner_ = use && (mu_learner_ != nullptr); }
    
    // 回调处理
    void OnPacketReceived(const FramePacketInfo& info, const FrameStatistics& frame_stats);
    void OnFrameComplete(const FrameStatistics& stats);
    
    // RTCP 反馈处理
    void OnPacketLost(uint32_t seq_num);
    void OnTransportFeedback(const std::vector<bool>& packet_received);
    void SimulateLossFromTrace();
    
    // 带宽历史管理
    void AddBandwidthRecord(Time timestamp, double trace_bw, double gcc_bw, double scaled_bw, double mu);
    const std::deque<BandwidthRecord>& GetBandwidthHistory() const { return bandwidth_history_; }
    double GetNearestGccBandwidth(Time timestamp) const;
    double GetSmoothedGccBandwidth(Time timestamp, int window_size = 5) const;
    
    // 跳帧回调
    void SetSkipFrameCallback(std::function<void(uint32_t)> callback);
    
    // 兼容性接口
    void SetVideoTraceManager(void* unused) {}
    void SetFrameAwareWebrtcTrace(void* unused) {}
    
    // 输出带宽历史到文件
    void OutputBandwidthHistory(const std::string& filename) const;

    // 输出 mu 变化轨迹和逐帧 QoE（与 OSCC 版格式兼容，便于对比）
    void OutputMuTrace(const std::string& filename) const;
    void OutputFrameQoE(const std::string& filename) const;

private:
    // 逐帧累积（packet 级累加，OnFrameComplete 时汇总为 FrameQoESummary）
    struct FrameAccumulator {
        double sum_bw_util;
        double sum_delay;
        double sum_reward;
        double sum_mu;
        double sum_loss;
        uint32_t count;
        FrameAccumulator() : sum_bw_util(0), sum_delay(0), sum_reward(0), sum_mu(0), sum_loss(0), count(0) {}
    };

private:
    RLStateManager* rl_manager_;
    BandwidthChanger* bw_changer_;
    Ptr<WebrtcSender> webrtc_sender_;
    std::deque<BandwidthRecord> bandwidth_history_;
    std::function<void(uint32_t)> skip_callback_;
    
    // MuLearner
    IMuLearner* mu_learner_;
    bool use_learner_;

    // Mu trace and per-frame QoE output
    std::vector<MuChangeRecord> mu_change_records_;
    std::map<uint32_t, FrameAccumulator> frame_accumulator_;
    std::map<uint32_t, FrameQoESummary> frame_qoe_map_;
};

} // namespace oscc

#endif // QOE_MANAGER_H
