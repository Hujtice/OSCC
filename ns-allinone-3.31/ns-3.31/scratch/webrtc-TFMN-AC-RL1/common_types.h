#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sys/time.h>
#include "ns3/core-module.h"

namespace oscc {

// ============================================================================
// 常量定义
// ============================================================================
const uint32_t DEFAULT_PACKET_SIZE = 1500;
const uint32_t kBwUnit = 1000000;
const uint64_t kMillisPerSecond = 1000;
const uint64_t kMicroPerMillis = 1000;

// ============================================================================
// 工具函数
// ============================================================================
inline uint64_t get_os_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// ============================================================================
// 包级别的状态记录
// ============================================================================
struct PacketStateRecord {
    uint32_t frame_id;
    uint32_t packet_index;
    double mu_used;
    uint32_t Rt;
    double loss_rate;
    double reward;
    ns3::Time send_time;
    ns3::Time recivied_time;
    ns3::Time deadline;
    double bandwidth_utilization;
    double p_delay_value;
    double p_loss_value;
    double p_mddl_value;
    double current_delay;
    
    PacketStateRecord() : frame_id(0), packet_index(0), mu_used(1.0), 
                         Rt(0), loss_rate(0.0), reward(0.0),
                         bandwidth_utilization(0.0), p_delay_value(0.0),
                         p_loss_value(0.0), p_mddl_value(0.0), current_delay(0.0) {}
};

// ============================================================================
// Rt分组奖励记录结构
// ============================================================================
struct RtGroupRewardRecord {
    uint32_t frame_id;
    uint32_t Rt_value;
    double loss_rate;
    double mu_used;
    double avg_reward;
    uint32_t packet_count;
    ns3::Time start_time;
    ns3::Time end_time;
    
    RtGroupRewardRecord() : frame_id(0), Rt_value(0), loss_rate(0.0), 
                           mu_used(1.0), avg_reward(0.0), packet_count(0), 
                           start_time(ns3::Seconds(0)), end_time(ns3::Seconds(0)) {}
    
    RtGroupRewardRecord(uint32_t fid, uint32_t rt, double loss, double mu, 
                       double reward, uint32_t count, ns3::Time start, ns3::Time end)
        : frame_id(fid), Rt_value(rt), loss_rate(loss), mu_used(mu),
          avg_reward(reward), packet_count(count), 
          start_time(start), end_time(end) {}
};

// ============================================================================
// Trace数据结构，包含四列数据
// ============================================================================
struct TraceData {
    ns3::Time timestamp;
    double bandwidth;
    double rtt;
    double loss;
    
    TraceData() : timestamp(ns3::Seconds(0)), bandwidth(0.0), rtt(0.0), loss(0.0) {}
    
    TraceData(ns3::Time ts, double bw, double rt, double l) 
        : timestamp(ts), bandwidth(bw), rtt(rt), loss(l) {}
};

// ============================================================================
// MuLearner 相关数据结构
// ============================================================================

// 状态结构：输入仅 Rt 和 loss
struct MuState {
    double rt;
    double loss;
    
    MuState() : rt(0.0), loss(0.0) {}
    MuState(double r, double l) : rt(r), loss(l) {}
};

// 动作结构：输出 mu
struct MuAction {
    double mu;
    double log_prob;
    
    MuAction() : mu(1.0), log_prob(0.0) {}
    MuAction(double m, double lp = 0.0) : mu(m), log_prob(lp) {}
};

// 经验结构：用于组级更新
struct MuExperience {
    MuState state;
    MuAction action;
    double reward;
    uint32_t frame_id;
    uint32_t rt_value;
    
    MuExperience() : reward(0.0), frame_id(0), rt_value(0) {}
    MuExperience(const MuState& s, const MuAction& a, double r, uint32_t fid, uint32_t rt)
        : state(s), action(a), reward(r), frame_id(fid), rt_value(rt) {}
};

// 学习器配置结构
struct MuLearnerConfig {
    double mu_min = 0.5;
    double mu_max = 1.5;
    double rt_max = 20.0;
    double loss_max = 0.1;
    double learning_rate = 0.01;
    double baseline_decay = 0.95;
    double grad_clip = 1.0;
    double exploration_sigma = 0.05;
    bool exploration_enabled = true;
    std::vector<double> initial_theta = {0.0, 0.0, 0.0};
    
    MuLearnerConfig() {}
};

// ============================================================================
// 强化学习状态数据结构
// ============================================================================
struct RLState {
    double mu;
    double reward;
    double bandwidth_utilization;
    double p_delay;
    double p_loss;
    double p_mddl;
    double current_delay;
    double current_loss_rate;
    double miss_deadline_time;
    uint32_t transmission_opportunities;
    ns3::Time packet_send_time;
    ns3::Time frame_deadline;
    
    RLState() : mu(1.0), reward(0.0), bandwidth_utilization(0.0), 
                p_delay(0.0), p_loss(0.0), p_mddl(0.0),
                current_delay(0.0), current_loss_rate(0.0), 
                miss_deadline_time(0.0), transmission_opportunities(0) {}
};

} // namespace oscc

#endif // COMMON_TYPES_H
