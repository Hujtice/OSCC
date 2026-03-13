#include "rl_state_manager.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iomanip>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("RLStateManager");

RLStateManager::RLStateManager() 
    : current_mu_(1.0), max_loss_rate_(0.05), 
      current_loss_rate_(0.01), last_packet_Rt_(1), current_rtt_(MilliSeconds(30)),
      current_delay_(20.0), mu_learner_(nullptr), learner_enabled_(false) {
    std::cout << "=== RLStateManager Constructor ===" << std::endl;
    std::cout << "Default values:" << std::endl;
    std::cout << "  current_mu: " << current_mu_ << std::endl;
    std::cout << "  max_loss_rate: " << max_loss_rate_ << std::endl;
    std::cout << "  current_loss_rate: " << current_loss_rate_ << std::endl;
    std::cout << "==================================" << std::endl;
    NS_LOG_INFO("RLStateManager initialized with default mu=1.0, Lmax=0.05");
}

void RLStateManager::SetParameters(double initial_mu, double lmax, Time rtt) {
    std::cout << "=== RLStateManager::SetParameters ===" << std::endl;
    std::cout << "Received parameters:" << std::endl;
    std::cout << "  initial_mu: " << initial_mu << std::endl;
    std::cout << "  lmax: " << lmax << " (THIS IS THE LOSS RATE PARAMETER)" << std::endl;
    std::cout << "  rtt: " << rtt.GetMilliSeconds() << "ms" << std::endl;
    
    current_mu_ = initial_mu;
    max_loss_rate_ = lmax;
    current_rtt_ = rtt;
    
    std::cout << "After setting:" << std::endl;
    std::cout << "  current_mu: " << current_mu_ << std::endl;
    std::cout << "  max_loss_rate: " << max_loss_rate_ << std::endl;
    std::cout << "  current_loss_rate: " << current_loss_rate_ << " (NOT CHANGED)" << std::endl;
    std::cout << "=====================================" << std::endl;
    NS_LOG_INFO("RLStateManager parameters set: mu=" << current_mu_ 
               << ", Lmax=" << max_loss_rate_ << ", RTT=" << rtt.GetMilliSeconds() << "ms");
}

uint32_t RLStateManager::CalculateTransmissionOpportunities(Time current_time, Time frame_deadline, 
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

    uint32_t Rt = static_cast<uint32_t>(std::floor(available_time.GetSeconds() / current_rtt_.GetSeconds()));
    
    // NS_LOG_DEBUG("Transmission opportunities calculation:");
    // NS_LOG_DEBUG("  Current time: " << current_time.GetSeconds() << "s");
    // NS_LOG_DEBUG("  Frame deadline: " << frame_deadline.GetSeconds() << "s");
    // NS_LOG_DEBUG("  T_remain: " << T_remain.GetSeconds() << "s");
    // NS_LOG_DEBUG("  Packet size: " << packet_size << " bytes");
    // NS_LOG_DEBUG("  Trace bandwidth: " << trace_bandwidth_bps << " bps");
    // NS_LOG_DEBUG("  Packet send time: " << packet_send_time.GetSeconds() << "s");
    // NS_LOG_DEBUG("  Available time: " << available_time.GetSeconds() << "s");
    // NS_LOG_DEBUG("  RTT: " << current_rtt_.GetSeconds() << "s");
    // NS_LOG_DEBUG("  Rt: " << Rt);
    
    std::cout << "计算传输机会："
              << "当前时间: " << current_time.GetSeconds() << "s" << std::endl
              << "截止时间: " << frame_deadline.GetSeconds() << "s" << std::endl
              << "剩余时间: " << T_remain.GetSeconds() << "s" << std::endl
              << "包大小: " << packet_size << " bytes" << std::endl
              << "带宽: " << trace_bandwidth_bps << " bps" << std::endl
              << "包发送时间: " << packet_send_time.GetSeconds() << "s" << std::endl
              << "可用时间: " << available_time.GetSeconds() << "s" << std::endl
              << "RTT的值: " << current_rtt_.GetSeconds() << "s" << std::endl
              << "Rt的值: " << Rt << std::endl;
    
    return Rt;
}

double RLStateManager::CalculateReward(double mu_prev, double gcc_bandwidth_bps, double trace_bandwidth_bps,
                                       double current_delay_ms, double current_loss_rate, 
                                       double miss_deadline_time, uint32_t Rt_current, uint32_t Rt_prev,
                                       uint32_t frame_id, uint32_t packet_index,
                                       double* out_U, double* out_p_delay,
                                       double* out_p_loss, double* out_p_mddl) {
    // 确定使用的Rt值
    uint32_t Rt_used = Rt_prev;
    if (frame_id == 0 && packet_index == 0) {
        Rt_used = 1;
    } else if (packet_index == 0 && frame_id > 0) {
        Rt_used = last_packet_Rt_;
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
    double Ptget = 1.0 - max_loss_rate_;
    double Ltol;
    if (Rt_used == 0) {
        Ltol = 0.01;
    } else {
        Ltol = std::pow(1.0 - Ptget, 1.0 / Rt_used);
    }

    // 自适应丢包容忍度
    double adaptive_tolerance = Ltol * (1.0 + 0.5 * (Rt_used / 10.0));
    double p_loss = current_loss_rate / adaptive_tolerance;
    p_loss = std::min(p_loss, 1.0);
    
    // (4) 错过截止时间惩罚
    double p_mddl = 0.0;
    double rtt_seconds = current_rtt_.GetSeconds();
    
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

    if (out_U) *out_U = U;
    if (out_p_delay) *out_p_delay = p_delay;
    if (out_p_loss) *out_p_loss = p_loss;
    if (out_p_mddl) *out_p_mddl = p_mddl;

    NS_LOG_DEBUG("Rt_used: " << Rt_used << ", mu_prev: " << mu_prev << ", U: " << U);
    NS_LOG_DEBUG("p_delay: " << p_delay << ", p_loss: " << p_loss << ", p_mddl: " << p_mddl);
    
    return reward;
}

void RLStateManager::RecordPacketState(uint32_t frame_id, uint32_t packet_index, double mu_used,
                                       uint32_t Rt, double loss_rate, double reward,
                                       Time send_time, Time recivied_time, Time deadline,
                                       double bandwidth_utilization, double p_delay,
                                       double p_loss, double p_mddl, double current_delay_ms,
                                       double miss_deadline_s,
                                       double real_throughput_bps, double gcc_bw_bps,
                                       double trace_bw_bps, double scaled_bw_bps) {
    PacketStateRecord record;
    record.frame_id = frame_id;
    record.packet_index = packet_index;
    record.mu_used = mu_used;
    record.Rt = Rt;
    record.loss_rate = loss_rate;
    record.reward = reward;
    record.send_time = send_time;
    record.recivied_time = recivied_time;
    record.deadline = deadline;
    record.bandwidth_utilization = bandwidth_utilization;
    record.p_delay_value = p_delay;
    record.p_loss_value = p_loss;
    record.p_mddl_value = p_mddl;
    record.current_delay = current_delay_ms;
    record.real_throughput_bps = real_throughput_bps;
    record.gcc_bw_bps = gcc_bw_bps;
    record.trace_bw_bps = trace_bw_bps;
    record.scaled_bw_bps = scaled_bw_bps;

    packet_records_.push_back(record);
    last_packet_Rt_ = Rt;
    
    AddPacketToRtGroup(frame_id, packet_index, Rt, loss_rate, reward, send_time, mu_used,
                       bandwidth_utilization, p_delay, p_loss, p_mddl,
                       current_delay_ms, loss_rate, miss_deadline_s, gcc_bw_bps, trace_bw_bps);
    
    NS_LOG_INFO("Recorded REAL packet state: frame=" << frame_id << ", packet=" << packet_index
               << ", mu=" << mu_used << ", Rt=" << Rt << ", loss_rate=" << loss_rate 
               << ", reward=" << reward << ", send_time=" << send_time.GetSeconds() 
               << "s, recv_time=" << recivied_time.GetSeconds() 
               << "s, delay=" << current_delay_ms << "ms");
}

void RLStateManager::AddPacketToRtGroup(uint32_t frame_id, uint32_t packet_index, uint32_t Rt, 
                                        double loss_rate, double reward, Time send_time, double mu_used,
                                        double U, double p_delay, double p_loss, double p_mddl,
                                        double raw_delay_ms, double raw_loss_rate,
                                        double raw_miss_deadline_s, double gcc_bw_bps, double trace_bw_bps) {
    if (current_rt_group_.frame_id != frame_id || current_rt_group_.Rt_value != Rt) {
        if (current_rt_group_.packet_count > 0) {
            FinalizeCurrentRtGroup();
        }
        
        current_rt_group_.frame_id = frame_id;
        current_rt_group_.Rt_value = Rt;
        current_rt_group_.loss_rate = loss_rate;
        current_rt_group_.mu_used = mu_used;
        current_rt_group_.avg_reward = 0.0;
        current_rt_group_.packet_count = 0;
        current_rt_group_.reward_sum = 0.0;
        current_rt_group_.sum_U = 0.0;
        current_rt_group_.sum_p_delay = 0.0;
        current_rt_group_.sum_p_loss = 0.0;
        current_rt_group_.sum_p_mddl = 0.0;
        current_rt_group_.sum_raw_delay_ms = 0.0;
        current_rt_group_.sum_raw_loss_rate = 0.0;
        current_rt_group_.sum_raw_miss_deadline_s = 0.0;
        current_rt_group_.sum_gcc_bw_bps = 0.0;
        current_rt_group_.sum_trace_bw_bps = 0.0;
        current_rt_group_.start_time = send_time;
    }
    
    current_rt_group_.packet_count++;
    current_rt_group_.reward_sum += reward;
    current_rt_group_.sum_U += U;
    current_rt_group_.sum_p_delay += p_delay;
    current_rt_group_.sum_p_loss += p_loss;
    current_rt_group_.sum_p_mddl += p_mddl;
    current_rt_group_.sum_raw_delay_ms += raw_delay_ms;
    current_rt_group_.sum_raw_loss_rate += raw_loss_rate;
    current_rt_group_.sum_raw_miss_deadline_s += raw_miss_deadline_s;
    current_rt_group_.sum_gcc_bw_bps += gcc_bw_bps;
    current_rt_group_.sum_trace_bw_bps += trace_bw_bps;
    current_rt_group_.end_time = send_time;
}

void RLStateManager::FinalizeCurrentRtGroup() {
    if (current_rt_group_.packet_count > 0) {
        current_rt_group_.avg_reward = current_rt_group_.reward_sum / current_rt_group_.packet_count;
        
        RtGroupRewardRecord record(
            current_rt_group_.frame_id,
            current_rt_group_.Rt_value,
            current_rt_group_.loss_rate,
            current_rt_group_.mu_used,
            current_rt_group_.avg_reward,
            current_rt_group_.packet_count,
            current_rt_group_.start_time,
            current_rt_group_.end_time
        );
        
        rt_group_records_.push_back(record);
        
        NS_LOG_INFO("Finalized Rt group: frame=" << record.frame_id 
                   << ", Rt=" << record.Rt_value 
                   << ", mu_used=" << record.mu_used
                   << ", packets=" << record.packet_count
                   << ", avg_reward=" << record.avg_reward
                   << ", loss_rate=" << record.loss_rate);
        
        // MuLearner更新
        if (learner_enabled_ && mu_learner_) {
            double n = static_cast<double>(current_rt_group_.packet_count);
            MuState state(static_cast<double>(current_rt_group_.Rt_value), current_rt_group_.loss_rate);
            MuAction action(current_rt_group_.mu_used);
            MuExperience exp(state, action, current_rt_group_.avg_reward, 
                            current_rt_group_.frame_id, current_rt_group_.Rt_value);
            exp.U = current_rt_group_.sum_U / n;
            exp.p_delay = current_rt_group_.sum_p_delay / n;
            exp.p_loss = current_rt_group_.sum_p_loss / n;
            exp.p_mddl = current_rt_group_.sum_p_mddl / n;
            exp.raw_delay_ms = current_rt_group_.sum_raw_delay_ms / n;
            exp.raw_loss_rate = current_rt_group_.sum_raw_loss_rate / n;
            exp.raw_miss_deadline_s = current_rt_group_.sum_raw_miss_deadline_s / n;
            exp.gcc_bw_bps = current_rt_group_.sum_gcc_bw_bps / n;
            exp.trace_bw_bps = current_rt_group_.sum_trace_bw_bps / n;
            
            mu_learner_->Observe(exp);
            mu_learner_->MaybeUpdate();
            
            current_mu_ = mu_learner_->CurrentMu();
            
            NS_LOG_INFO("MuLearner updated: Rt=" << current_rt_group_.Rt_value 
                       << ", reward=" << current_rt_group_.avg_reward
                       << ", " << mu_learner_->GetStatusString());
                       
            std::cout << "[MuLearner] Rt group update: frame=" << current_rt_group_.frame_id
                      << ", Rt=" << current_rt_group_.Rt_value
                      << ", loss=" << current_rt_group_.loss_rate
                      << ", mu_used=" << current_rt_group_.mu_used
                      << ", avg_reward=" << current_rt_group_.avg_reward
                      << ", " << mu_learner_->GetStatusString() << std::endl;
        }
    }
    
    // 重置当前Rt组
    current_rt_group_ = RtGroup();
}

void RLStateManager::OutputStateRecords(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
    std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                          "_L=" + std::to_string(initial_loss_rate) + "_RL_log.csv";
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open RL log file: " << filename);
        return;
    }
    
    file << "frame_id,packet_index,mu_used,Rt,loss_rate,reward,send_time,recivied_time,deadline,"
         << "bandwidth_utilization,p_delay,p_loss,p_mddl,current_delay,"
         << "real_throughput_bps,gcc_bw_bps,trace_bw_bps,scaled_bw_bps" << std::endl;

    std::vector<PacketStateRecord> sorted_records = packet_records_;
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
             << record.current_delay << ","
             << record.real_throughput_bps << ","
             << record.gcc_bw_bps << ","
             << record.trace_bw_bps << ","
             << record.scaled_bw_bps << std::endl;
    }
    
    file.close();
    
    NS_LOG_INFO("RL state records saved to: " << filename << " with " << packet_records_.size() << " REAL records");
    
    if (packet_records_.empty()) {
        std::cout << "WARNING: No RL state records were generated!" << std::endl;
        std::cout << "This means no real packets were processed." << std::endl;
    } else {
        std::cout << "Successfully recorded " << packet_records_.size() << " REAL RL state entries" << std::endl;
    }
}

void RLStateManager::OutputRtGroupRewards(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
    FinalizeCurrentRtGroup();
    
    std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                          "_L=" + std::to_string(initial_loss_rate) + "_Frame-Rt-Reward.csv";
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open Rt group reward file: " << filename);
        return;
    }
    
    file << "# Rt Group Reward Records" << std::endl;
    file << "# Initial mu: " << initial_mu << std::endl;
    file << "# Initial loss rate: " << initial_loss_rate << std::endl;
    file << "# Learner enabled: " << (learner_enabled_ ? "yes" : "no") << std::endl;
    if (learner_enabled_ && mu_learner_) {
        file << "# Learner final status: " << mu_learner_->GetStatusString() << std::endl;
        file << "# Learner theta_norm: " << mu_learner_->GetThetaNorm() << std::endl;
        file << "# Learner baseline: " << mu_learner_->GetBaseline() << std::endl;
    }
    file << "#" << std::endl;
    
    file << "frame_id,Rt_value,loss_rate,mu_used,avg_reward,packet_count,start_time,end_time" << std::endl;
    
    for (const auto& record : rt_group_records_) {
        file << record.frame_id << ","
             << record.Rt_value << ","
             << record.loss_rate << ","
             << record.mu_used << ","
             << record.avg_reward << ","
             << record.packet_count << ","
             << record.start_time.GetSeconds() << ","
             << record.end_time.GetSeconds() << std::endl;
    }
    
    file.close();
    
    NS_LOG_INFO("Rt group reward records saved to: " << filename << " with " 
               << rt_group_records_.size() << " Rt groups");
    
    if (rt_group_records_.empty()) {
        std::cout << "WARNING: No Rt group reward records were generated!" << std::endl;
    } else {
        std::cout << "Successfully recorded " << rt_group_records_.size() << " Rt group reward entries" << std::endl;
        
        std::cout << "First few Rt group records:" << std::endl;
        int count = 0;
        for (const auto& record : rt_group_records_) {
            if (count++ >= 5) break;
            std::cout << "  Frame " << record.frame_id 
                      << ", Rt=" << record.Rt_value 
                      << ", μ=" << record.mu_used
                      << ", Reward=" << record.avg_reward 
                      << ", Packets=" << record.packet_count << std::endl;
        }
        
        if (learner_enabled_ && mu_learner_) {
            std::cout << "Learner final status: " << mu_learner_->GetStatusString() << std::endl;
        }
    }
}

void RLStateManager::OutputLearnerLog(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
    if (!mu_learner_) {
        std::cout << "[RLStateManager] Cannot output learner log: learner not set" << std::endl;
        return;
    }
    
    std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                          "_L=" + std::to_string(initial_loss_rate) + "_learner_state.csv";
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open learner log file: " << filename);
        return;
    }
    
    file << "# MuLearner State Log" << std::endl;
    file << "# Initial mu: " << initial_mu << std::endl;
    file << "# Initial loss rate: " << initial_loss_rate << std::endl;
    file << "# Final status: " << mu_learner_->GetStatusString() << std::endl;
    file << "#" << std::endl;
    
    file << "metric,value" << std::endl;
    file << "theta_norm," << mu_learner_->GetThetaNorm() << std::endl;
    file << "baseline," << mu_learner_->GetBaseline() << std::endl;
    file << "current_mu," << mu_learner_->CurrentMu() << std::endl;
    file << "total_rt_groups," << rt_group_records_.size() << std::endl;
    
    // [暂时注释] BanditMuLearner 尚未实现，后续若添加 BanditMuLearner 类可恢复以下代码
    // BanditMuLearner* bandit = dynamic_cast<BanditMuLearner*>(mu_learner_);
    // if (bandit) {
    //     const auto& theta = bandit->GetTheta();
    //     file << "learner_type,bandit" << std::endl;
    //     file << "theta_rt," << (theta.size() > 0 ? theta[0] : 0.0) << std::endl;
    //     file << "theta_loss," << (theta.size() > 1 ? theta[1] : 0.0) << std::endl;
    //     file << "theta_bias," << (theta.size() > 2 ? theta[2] : 0.0) << std::endl;
    //     file << "exploration_sigma," << bandit->GetConfig().exploration_sigma << std::endl;
    //     file << "learning_rate," << bandit->GetConfig().learning_rate << std::endl;
    // }
    
    // [暂时注释] TorchMLPMuLearner 尚未实现，后续若添加 TorchMLPMuLearner 类可恢复以下代码
    // #ifdef OSCC_USE_TORCH
    // TorchMLPMuLearner* torch_mlp = dynamic_cast<TorchMLPMuLearner*>(mu_learner_);
    // if (torch_mlp) {
    //     file << "learner_type,torch_mlp" << std::endl;
    //     file << "network_structure,2-16-8-1" << std::endl;
    //     file << "update_count," << torch_mlp->GetUpdateCount() << std::endl;
    //     file << "last_loss," << torch_mlp->GetLastLoss() << std::endl;
    //     file << "exploration_sigma," << torch_mlp->GetConfig().exploration_sigma << std::endl;
    //     file << "learning_rate," << torch_mlp->GetConfig().learning_rate << std::endl;
    //     file << "grad_clip," << torch_mlp->GetConfig().grad_clip << std::endl;
    // }
    // #endif
    
    file.close();
    
    NS_LOG_INFO("Learner state log saved to: " << filename);
    std::cout << "[RLStateManager] Learner state log saved to: " << filename << std::endl;
    std::cout << "  Final learner status: " << mu_learner_->GetStatusString() << std::endl;
}

void RLStateManager::SetCurrentMu(double mu) {
    current_mu_ = mu;
}

void RLStateManager::SetMu(double mu) {
    if (mu >= 0.5 && mu <= 1.5) {
        current_mu_ = mu;
        NS_LOG_DEBUG("RLStateManager: mu set to " << mu);
    }
}

void RLStateManager::UpdateNetworkState(double delay_ms, double loss_rate, Time rtt) {
    current_delay_ = delay_ms;
    current_loss_rate_ = loss_rate;
    current_rtt_ = rtt;
    NS_LOG_DEBUG("Network state updated: delay=" << delay_ms << "ms, loss_rate=" << loss_rate 
               << ", RTT=" << rtt.GetMilliSeconds() << "ms");
}

void RLStateManager::SetCurrentLossRate(double loss_rate) {
    current_loss_rate_ = loss_rate;
    NS_LOG_INFO("RLStateManager current_loss_rate updated to: " << current_loss_rate_);
}

void RLStateManager::SetMuLearner(IMuLearner* learner) {
    mu_learner_ = learner;
    learner_enabled_ = (learner != nullptr);
    NS_LOG_INFO("RLStateManager: MuLearner " << (learner_enabled_ ? "enabled" : "disabled"));
    if (learner_enabled_) {
        std::cout << "[RLStateManager] MuLearner enabled, will use learned policy for mu" << std::endl;
    }
}

bool RLStateManager::IsLearnerEnabled() const {
    return learner_enabled_ && mu_learner_ != nullptr;
}

double RLStateManager::GetLearnerMu(uint32_t Rt, double loss_rate) {
    if (learner_enabled_ && mu_learner_) {
        MuState state(static_cast<double>(Rt), loss_rate);
        MuAction action = mu_learner_->Act(state);
        current_mu_ = action.mu;
        
        NS_LOG_DEBUG("MuLearner: Rt=" << Rt << ", loss=" << loss_rate 
                    << " -> mu=" << action.mu);
        
        return action.mu;
    }
    return current_mu_;
}

std::string RLStateManager::GetLearnerStatus() const {
    if (mu_learner_) {
        return mu_learner_->GetStatusString();
    }
    return "Learner not set";
}

} // namespace oscc
