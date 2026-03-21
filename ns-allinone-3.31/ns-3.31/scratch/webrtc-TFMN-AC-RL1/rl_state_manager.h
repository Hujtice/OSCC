#ifndef RL_STATE_MANAGER_H
#define RL_STATE_MANAGER_H

#include "common_types.h"
#include "mu_learner.h"
#include <fstream>

namespace oscc {

using namespace ns3;

// ============================================================================
// 强化学习状态管理器类
// ============================================================================
class RLStateManager {
public:
    RLStateManager();
    
    // 设置参数
    void SetParameters(double initial_mu, double lmax, Time rtt);
    
    // 计算传输机会 Rt
    uint32_t CalculateTransmissionOpportunities(Time current_time, Time frame_deadline, 
                                               uint32_t packet_size, double trace_bandwidth_bps);
    
    // 计算奖励（out_U/out_p_delay/out_p_loss/out_p_mddl 输出未加权子项）
    double CalculateReward(double real_throughput_bps, double trace_bandwidth_bps,
                          double current_delay_ms, double current_loss_rate, 
                          double miss_deadline_time, uint32_t Rt_current, uint32_t Rt_prev,
                          uint32_t frame_id, uint32_t packet_index,
                          double* out_U, double* out_p_delay,
                          double* out_p_loss, double* out_p_mddl);
    
    // 记录包状态（含 real_throughput 与 bw_util 溯源字段）
    void RecordPacketState(uint32_t frame_id, uint32_t packet_index, double mu_used,
                          uint32_t Rt, double loss_rate, double reward,
                          Time send_time, Time recivied_time, Time deadline,
                          double bandwidth_utilization, double p_delay,
                          double p_loss, double p_mddl, double current_delay_ms,
                          double miss_deadline_s,
                          double real_throughput_bps, double gcc_bw_bps,
                          double trace_bw_bps, double scaled_bw_bps,
                          uint32_t pkt_received, uint32_t pkt_expected);
    
    // Rt分组管理
    void AddPacketToRtGroup(uint32_t frame_id, uint32_t packet_index, uint32_t Rt, 
                           double loss_rate, double reward, Time send_time, double mu_used,
                           double U, double p_delay, double p_loss, double p_mddl,
                           double raw_delay_ms, double raw_loss_rate,
                           double raw_miss_deadline_s, double gcc_bw_bps, double trace_bw_bps,
                           uint32_t pkt_received, uint32_t pkt_expected);
    void FinalizeCurrentRtGroup();
    const std::vector<RtGroupRewardRecord>& GetRtGroupRecords() const { return rt_group_records_; }
    
    // 输出记录到文件
    void OutputStateRecords(const std::string& filename_prefix, double initial_mu, double initial_loss_rate);
    void OutputRtGroupRewards(const std::string& filename_prefix, double initial_mu, double initial_loss_rate);
    void OutputLearnerLog(const std::string& filename_prefix, double initial_mu, double initial_loss_rate);
    
    // Mu值管理
    void SetCurrentMu(double mu);
    double GetCurrentMu() const { return current_mu_; }
    void SetMu(double mu);
    
    // 包记录访问
    const std::vector<PacketStateRecord>& GetPacketRecords() const { return packet_records_; }
    
    // 网络状态更新
    void UpdateNetworkState(double delay_ms, double loss_rate, Time rtt);
    
    // 获取状态
    uint32_t GetLastPacketRt() const { return last_packet_Rt_; }
    void SetCurrentLossRate(double loss_rate);
    double GetCurrentLossRate() const { return current_loss_rate_; }
    double GetMaxLossRate() const { return max_loss_rate_; }
    double GetCurrentDelay() const { return current_delay_; }
    Time GetCurrentRTT() const { return current_rtt_; }
    
    // MuLearner集成
    void SetMuLearner(IMuLearner* learner);
    bool IsLearnerEnabled() const;
    IMuLearner* GetMuLearner() { return mu_learner_; }
    double GetLearnerMu(uint32_t Rt, double loss_rate);
    std::string GetLearnerStatus() const;
    
private:
    // 内部Rt分组结构
    struct RtGroup {
        uint32_t frame_id;
        uint32_t Rt_value;
        double loss_rate;
        double mu_used;
        double avg_reward;
        uint32_t packet_count;
        double reward_sum;
        Time start_time;
        Time end_time;
        double sum_U;
        double sum_p_delay;
        double sum_p_loss;
        double sum_p_mddl;
        double sum_raw_delay_ms;
        double sum_raw_loss_rate;
        double sum_raw_miss_deadline_s;
        double sum_gcc_bw_bps;
        double sum_trace_bw_bps;
        uint32_t group_received;
        uint32_t group_expected;
        
        RtGroup() : frame_id(0), Rt_value(0), loss_rate(0.0), mu_used(1.0),
                   avg_reward(0.0), packet_count(0), reward_sum(0.0), 
                   start_time(Seconds(0)), end_time(Seconds(0)),
                   sum_U(0.0), sum_p_delay(0.0), sum_p_loss(0.0), sum_p_mddl(0.0),
                   sum_raw_delay_ms(0.0), sum_raw_loss_rate(0.0),
                   sum_raw_miss_deadline_s(0.0), sum_gcc_bw_bps(0.0), sum_trace_bw_bps(0.0),
                   group_received(0), group_expected(0) {}
    };
    
    double current_mu_;
    double max_loss_rate_;
    double current_loss_rate_;
    uint32_t last_packet_Rt_;
    Time current_rtt_;
    double current_delay_;
    std::vector<PacketStateRecord> packet_records_;
    
    RtGroup current_rt_group_;
    std::vector<RtGroupRewardRecord> rt_group_records_;
    
    // MuLearner集成
    IMuLearner* mu_learner_;
    bool learner_enabled_;
};

} // namespace oscc

#endif // RL_STATE_MANAGER_H
