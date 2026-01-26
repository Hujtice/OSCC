#ifndef NETWORK_COMPONENTS_H
#define NETWORK_COMPONENTS_H

#include "common_types.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include <fstream>

namespace oscc {

using namespace ns3;

// 前向声明
class BandwidthChanger;

// ============================================================================
// TriggerRandomLoss 类
// ============================================================================
class TriggerRandomLoss {
public:
    TriggerRandomLoss(double loss_rate = 0.0);
    ~TriggerRandomLoss();
    
    void RegisterDevice(Ptr<NetDevice> dev);
    void SetBandwidthChanger(BandwidthChanger* changer);
    void SetUseTraceLoss(bool use_trace);
    void SetUpdateInterval(uint32_t interval_ms);
    void Start();
    void ConfigureRandomLoss();
    void UpdateLossFromTrace();
    void ScheduleNextUpdate();
    void SetLossRate(double loss_rate);
    double GetLossRate() const { return m_loss_rate; }
    double GetLastAppliedLoss() const { return m_last_applied_loss; }
    
private:
    Ptr<NetDevice> m_dev;
    Ptr<RateErrorModel> m_error_model;
    EventId m_timer;
    EventId m_update_timer;
    double m_loss_rate;
    double m_last_applied_loss;
    BandwidthChanger* m_bandwidth_changer;
    bool m_use_trace_loss;
    uint32_t m_update_interval_ms;
};

// ============================================================================
// CompareV 比较器
// ============================================================================
struct CompareV {
    bool operator() (const std::pair<Time,int64_t>& a, const std::pair<Time,int64_t>& b) {
        return a.first < b.first;
    }
};

// ============================================================================
// BandwidthChanger 类
// ============================================================================
class BandwidthChanger {
public:
    BandwidthChanger();
    ~BandwidthChanger();
    
    void RegisterDevice(Ptr<NetDevice> dev);
    void Config(int64_t initial_bps, std::vector<std::pair<Time,int64_t>>& info);
    std::pair<float,float> ConfigwithReadNetworkTrace(int64_t initial_bps, const std::string& trace_file);
    void ResetBandwidthFromTrace();
    void TotalThroughput(Time stop, int64_t& channel_bit);
    void Start();
    int lower_bound_index(Time point);
    
    // 获取当前trace带宽
    double GetCurrentTraceBandwidth() const { return current_trace_bandwidth_bps_; }
    
    // 根据时间戳获取trace带宽
    uint32_t GetTraceBandwidthAtTime(uint32_t timestamp_ms) const;
    
    // 根据时间获取RTT值
    double GetRTTAtTime(uint32_t timestamp_ms) const;
    
    // 根据时间获取Loss值
    double GetLossAtTime(uint32_t timestamp_ms) const;
    
    // 根据时间获取TraceData
    TraceData GetTraceDataAtTime(uint32_t timestamp_ms) const;
    
private:
    int64_t m_initialRate;
    std::vector<std::pair<Time,int64_t>> m_info;
    std::vector<TraceData> m_trace_data;
    uint32_t m_index;
    Ptr<NetDevice> m_dev;
    EventId m_timer;
    double current_trace_bandwidth_bps_;
};

} // namespace oscc

#endif // NETWORK_COMPONENTS_H
