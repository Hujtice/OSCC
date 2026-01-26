#ifndef WEBRTC_TRACE_H
#define WEBRTC_TRACE_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <set>
#include <tuple>

#include "common_types.h"
#include "qoe_manager.h"
#include "rl_state_manager.h"
#include "oscc_controller.h"
#include "network_components.h"
#include "ns3/webrtc-defines.h"
#include "ns3/ex-webrtc-module.h"

namespace oscc {

using namespace ns3;

// ============================================================================
// FrameAwareWebrtcTrace - 增强的WebrtcTrace类
// ============================================================================
class FrameAwareWebrtcTrace : public WebrtcTrace {
public:
    typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;
    typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;
    typedef Callback<uint32_t, uint32_t> GetTraceBandwidthCallback;
    
    FrameAwareWebrtcTrace(QoEIntegrationManager* qoe_manager = nullptr, 
                         RLStateManager* rl_manager = nullptr, 
                         double bandwidth_scale_factor = 1.0);
    
    virtual ~FrameAwareWebrtcTrace();
    
    // 设置参数
    void SetCurrentParameters(double mu, double loss_rate);
    
    // 设置回调
    void SetBwTraceFuc(TraceBandwidth cb);
    void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    void SetTraceBandwidthCallback(GetTraceBandwidthCallback cb);
    void SetBandwidthChanger(BandwidthChanger* changer);
    
    // 重写处理函数
    void OnReceiptPktInfo(uint32_t now, uint32_t seq, uint32_t owd);
    void Log(const std::string& name, uint32_t flags);
    void OnBW(uint32_t now, uint32_t bps);
    
    // 缩放带宽记录
    void OnScaledBandwidth(uint32_t now, uint32_t original_bps, uint32_t scaled_bps, double scale_factor);
    
    // 输出带宽统计
    void OutputBandwidthStatistics(const std::string& filename, double loss_rate = 0.01);
    
    // 获取带宽统计
    void GetBandwidthStats(uint32_t& total_changes, uint32_t& avg_original_bw, uint32_t& avg_scaled_bw) const;
    
    // 设置管理器
    void SetQoEManager(QoEIntegrationManager* qoe_manager);
    void SetRLStateManager(RLStateManager* rl_manager);
    void SetBandwidthScaleFactor(double factor);
    void SetOSCCController(OSCCController* controller);
    
    // 获取μ值
    double GetMuAtTimestamp(double timestamp_s) const;

private:
    QoEIntegrationManager* qoe_manager_;
    RLStateManager* rl_manager_;
    uint32_t total_bw_changes;
    double bandwidth_scale_factor_;
    std::vector<std::pair<uint32_t, uint32_t>> original_bw_history;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, double>> scaled_bw_history;

    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    GetTraceBandwidthCallback m_getTraceBw;
    BandwidthChanger* m_changer;
    OSCCController* oscc_controller_;
    
    double current_mu;
    double current_loss_rate;
};

} // namespace oscc

#endif // WEBRTC_TRACE_H
