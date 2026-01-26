#ifndef RL_STATE_MANAGER_H
#define RL_STATE_MANAGER_H

#include "common_types.h"
#include "mu_learner.h"
#include "oscc_controller.h"
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
    
    // 计算奖励
    double CalculateReward(double mu_prev, double gcc_bandwidth_bps, double trace_bandwidth_bps,
                          double current_delay_ms, double current_loss_rate, 
                          double miss_deadline_time, uint32_t Rt_current, uint32_t Rt_prev,
                          uint32_t frame_id, uint32_t packet_index);
    
    // 记录包状态
    void RecordPacketState(uint32_t frame_id, uint32_t packet_index, double mu_used,
                          uint32_t Rt, double loss_rate, double reward,
                          Time send_time, Time recivied_time, Time deadline,
                          double bandwidth_utilization, double p_delay, 
                          double p_loss, double p_mddl, double current_delay_ms);
    
    // Rt分组管理
    void AddPacketToRtGroup(uint32_t frame_id, uint32_t packet_index, uint32_t Rt, 
                           double loss_rate, double reward, Time send_time, double mu_used);
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
    
    // OSCC集成
    void SetOSCCController(OSCCController* controller);
    bool IsOSCCEnabled() const;
    OSCCController* GetOSCCController() { return oscc_controller_; }
    double GetAdaptiveMu(uint32_t frame_id, uint32_t Rt);
    void NotifyRtGroupComplete(uint32_t frame_id, uint32_t Rt, double loss);
    
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
        
        RtGroup() : frame_id(0), Rt_value(0), loss_rate(0.0), mu_used(1.0),
                   avg_reward(0.0), packet_count(0), reward_sum(0.0), 
                   start_time(Seconds(0)), end_time(Seconds(0)) {}
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
    
    // OSCC集成
    OSCCController* oscc_controller_;
    bool oscc_enabled_;
    
    // MuLearner集成
    IMuLearner* mu_learner_;
    bool learner_enabled_;
};

} // namespace oscc

#endif // RL_STATE_MANAGER_H
