#ifndef OSCC_CONTROLLER_H
#define OSCC_CONTROLLER_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
#include <deque>
#include <fstream>

#include "common_types.h"
#include "ns3/webrtc-defines.h"
#include "ns3/ex-webrtc-module.h"

namespace oscc {

using namespace ns3;

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
        std::string trigger_type;
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
        double delay_avg;
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

    OSCCController();
    
    // 设置OSCC参数
    void SetParameters(double epsilon, double mu_min, double mu_max, double initial_mu);
    
    // 启用/禁用OSCC
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return oscc_enabled_; }
    
    // 滑动窗口丢包率相关方法
    double GetCurrentWindowLoss() const;
    void UpdateLossWindow(bool packet_lost);
    
    // 核心查表逻辑 - 基于Rt的mu决策
    double GetMuForPacket(uint32_t frame_id, uint32_t Rt);
    
    // Rt组完成时调用
    void OnRtGroupComplete(uint32_t frame_id, uint32_t Rt, double group_loss);
    
    // 帧完成时调用
    void OnFrameComplete(uint32_t frame_id, double frame_qoe,
                        double bandwidth_utilization, double loss_rate,
                        double delay_metric, double delay_avg, double ddl_miss_rate,
                        double qoe_recv, double qoe_delay,
                        double qoe_loss, double qoe_ddl);
    
    // 获取当前μ
    double GetCurrentMu() const;
    
    // 获取μ变化记录
    const std::vector<MuChangeRecord>& GetMuChangeRecords() const { return mu_change_records_; }
    
    // 获取帧QoE历史
    const std::map<uint32_t, double>& GetFrameQoEHistory() const { return frame_qoe_history_; }
    
    // 输出日志到文件
    void OutputMuTrace(const std::string& filename) const;
    void OutputFrameQoE(const std::string& filename) const;
    
    // 获取统计信息
    uint32_t GetTotalAdjustments() const { return total_adjustments_; }
    
    // 发送端 Rt 预估
    void SetRtEstimationParams(Time rtt, double bandwidth_bps);
    void SetCurrentFrameDeadline(Time deadline);
    uint32_t EstimateRt(uint32_t packet_size);
    double GetMuForPacketWithEstimation(uint32_t frame_id, uint32_t packet_size);
    
    // 设置WebrtcSender引用
    void SetWebrtcSender(Ptr<WebrtcSender> sender);
    
private:
    // 核心查表算法
    double CalculateMuFromHistory(uint32_t Rt, double L_curr);
    double AdjustMuByLoss(double mu_prev, double L_prev, double L_curr);
    
    // 帧边界处理
    void OnNewFrameStart(uint32_t new_frame_id);
    
    // 记录mu变化
    void RecordMuChange(uint32_t frame_id, uint32_t Rt, double new_mu, double loss_rate);
    
    // 约束μ在有效范围内
    double ClipMu(double mu) const;
    
    // 直接应用μ值到WebrtcSender
    void ApplyMuToSender(double new_mu);
    
    // 核心参数
    double epsilon_;
    double mu_min_;
    double mu_max_;
    
    // 基于Rt的历史查表数据结构
    std::map<uint32_t, RtHistoryEntry> history_map_;
    std::map<uint32_t, double> current_frame_cache_;
    std::deque<bool> loss_window_;
    
    // QoE统计
    std::map<uint32_t, double> frame_qoe_history_;
    std::map<uint32_t, FrameQoEDetail> frame_qoe_details_;
    
    // 状态变量
    uint32_t current_frame_id_;
    bool oscc_enabled_;
    std::vector<MuChangeRecord> mu_change_records_;
    uint32_t total_adjustments_;
    
    // Rt 预估相关参数
    Time estimated_rtt_;
    double estimated_bandwidth_bps_;
    Time current_frame_deadline_;
    
    // WebrtcSender引用
    Ptr<WebrtcSender> webrtc_sender_;
};

} // namespace oscc

#endif // OSCC_CONTROLLER_H
