#ifndef QOE_MANAGER_H
#define QOE_MANAGER_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <deque>
#include <functional>
#include <random>

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

private:
    RLStateManager* rl_manager_;
    BandwidthChanger* bw_changer_;
    Ptr<WebrtcSender> webrtc_sender_;
    std::deque<BandwidthRecord> bandwidth_history_;
    std::function<void(uint32_t)> skip_callback_;
    
    // MuLearner
    IMuLearner* mu_learner_;
    bool use_learner_;
};

} // namespace oscc

#endif // QOE_MANAGER_H
