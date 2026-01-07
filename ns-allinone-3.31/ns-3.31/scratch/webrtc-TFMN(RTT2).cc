#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <utility>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <cmath>
#include <numeric>
#include <deque>
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

// 移除原有的固定帧配置，改为从trace文件读取
const uint32_t MAX_PACKETS_PER_FRAME = 150; // 最大包数，根据最大帧大小设置

uint64_t get_os_millis()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// 视频帧数据结构
struct VideoFrame {
    uint32_t frame_id;                    // 帧ID
    Time deadline;                        // 播放截止时间
    uint32_t frame_size;                  // 帧大小（字节）
    uint32_t frame_type;                  // 帧类型：1=关键帧，0=P帧
    uint32_t total_packets;               // 总包数
    uint32_t packets_received;            // 已接收包数
    Time first_packet_arrival_time;       // 第一个包到达时间
    Time last_packet_arrival_time;        // 最后一个包到达时间
    uint32_t packets_before_deadline;     // 截止时间前到达的包数
    uint32_t missed_deadline;             // 是否错过截止时间 (0/1)
    Time stall_duration;                  // 卡顿时间
    std::vector<Time> packet_arrival_times; // 每个包的到达时间
    bool frame_completed;                 // 帧是否完成
    std::vector<bool> packet_received;    // 每个包是否已接收
    
    // 默认构造函数
    VideoFrame() 
        : frame_id(0), deadline(Seconds(0)), frame_size(0), frame_type(0),
          total_packets(0), packets_received(0), first_packet_arrival_time(Seconds(0)),
          last_packet_arrival_time(Seconds(0)), packets_before_deadline(0), 
          missed_deadline(0), stall_duration(Seconds(0)), frame_completed(false) {}
    
    // 参数化构造函数
    VideoFrame(uint32_t id, Time dl, uint32_t size, uint32_t type) 
        : frame_id(id), deadline(dl), frame_size(size), frame_type(type),
          total_packets(0), packets_received(0), first_packet_arrival_time(Seconds(0)),
          last_packet_arrival_time(Seconds(0)), packets_before_deadline(0), 
          missed_deadline(0), stall_duration(Seconds(0)), frame_completed(false) {
        
        // 计算需要的包数（每个包DEFAULT_PACKET_SIZE字节）
        total_packets = (frame_size + DEFAULT_PACKET_SIZE - 1) / DEFAULT_PACKET_SIZE;
        packet_received.resize(total_packets, false);
        packet_arrival_times.resize(total_packets, Seconds(0));
        
        NS_LOG_DEBUG("Created frame " << frame_id << " with " << total_packets 
                     << " packets, deadline: " << deadline.GetSeconds() << "s");
    }
};

// 视频trace管理器
class VideoTraceManager {
public:
    VideoTraceManager() : frames_loaded(false) {}
    
    // 从文件加载视频trace
    bool LoadVideoTrace(const std::string& trace_file) {
        std::ifstream file(trace_file);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open video trace file: " << trace_file);
            return false;
        }
        
        frames.clear();
        float time_sec;
        float frame_size;
        uint32_t frame_type;
        uint32_t frame_id = 0;
        
        while (file >> time_sec >> frame_size >> frame_type) {
            Time deadline = Seconds(time_sec)+ Seconds(0.1);  // 加上0.1秒延迟
            VideoFrame frame(frame_id, deadline, static_cast<uint32_t>(frame_size), frame_type);
            frames.push_back(frame);
            frame_id++;
        }
        
        file.close();
        frames_loaded = true;
        
        NS_LOG_INFO("Loaded " << frames.size() << " frames from " << trace_file);
        
        // 输出前几帧信息用于调试
        for (size_t i = 0; i < std::min(frames.size(), size_t(5)); i++) {
            const VideoFrame& frame = frames[i];
            NS_LOG_INFO("Frame " << frame.frame_id << ": deadline=" << frame.deadline.GetSeconds() 
                       << "s, size=" << frame.frame_size << " bytes, type=" << frame.frame_type
                       << ", packets=" << frame.total_packets);
        }
        
        return true;
    }
    
    // 获取指定帧的信息
    const VideoFrame* GetFrame(uint32_t frame_id) const {
        if (frame_id < frames.size()) {
            return &frames[frame_id];
        }
        return nullptr;
    }
    
    // 获取总帧数
    size_t GetTotalFrames() const {
        return frames.size();
    }
    
    // 检查trace是否已加载
    bool IsLoaded() const {
        return frames_loaded;
    }
    
    // 获取最后一帧的截止时间（用于确定仿真时长）
    Time GetLastFrameDeadline() const {
        if (frames.empty()) {
            return Seconds(0);
        }
        return frames.back().deadline;
    }
    
private:
    std::vector<VideoFrame> frames;
    bool frames_loaded;
};

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

// 强化学习状态管理器类
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

class TriggerRandomLoss{
public:
    TriggerRandomLoss(double loss_rate = 0.0) : m_loss_rate(loss_rate) {
        NS_LOG_INFO("TriggerRandomLoss initialized with loss rate: " << m_loss_rate);
    }
    
    ~TriggerRandomLoss(){
        if(m_timer.IsRunning()){
            m_timer.Cancel();
        }
    }
    
    void RegisterDevice(Ptr<NetDevice> dev){
        m_dev = dev;
        NS_LOG_INFO("Device registered for random loss with rate: " << m_loss_rate);
    }
    
    void Start(){
        Time next = MilliSeconds(10);
        m_timer = Simulator::Schedule(next, &TriggerRandomLoss::ConfigureRandomLoss, this);
        NS_LOG_INFO("Scheduled random loss configuration at " << next.GetSeconds() << "s with rate: " << m_loss_rate);
    }
    
    void ConfigureRandomLoss(){
        if (m_dev) {
            std::string errorModelType = "ns3::RateErrorModel";
            ObjectFactory factory;
            factory.SetTypeId (errorModelType);
            Ptr<ErrorModel> em = factory.Create<ErrorModel> ();
            
            em->SetAttribute ("ErrorRate", DoubleValue (m_loss_rate));
            em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
            
            m_dev->SetAttribute ("ReceiveErrorModel", PointerValue (em));            
            NS_LOG_INFO("Random loss configured at " << Simulator::Now().GetSeconds() 
                       << "s with rate: " << m_loss_rate);
        } else {
            NS_LOG_ERROR("No device registered for random loss configuration");
        }
        
        m_timer.Cancel();
    }
    
    void SetLossRate(double loss_rate) {
        m_loss_rate = loss_rate;
        NS_LOG_INFO("Loss rate updated to: " << m_loss_rate);
    }
    
    double GetLossRate() const {
        return m_loss_rate;
    }
    
private:
    Ptr<NetDevice> m_dev;
    EventId m_timer;
    double m_loss_rate;
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
            trace_data.bandwidth = bandwidth * 1000000.0; // 转换为bps
            trace_data.rtt = rtt;
            trace_data.loss = loss;
            m_trace_data.push_back(trace_data);
            
            // 仅带宽变化用于带宽调度
            int64_t FIX_bandwidth_bps = static_cast<int64_t>(bandwidth * 1000000.0);
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

// FrameManager 类定义放在 BandwidthChanger 之后
class FrameManager {
public:
    // 带宽记录结构体 - 放在类定义最前面
    struct BandwidthRecord {
        Time timestamp;
        double trace_bandwidth;
        double gcc_bandwidth;
        double scaled_bandwidth;
        double mu_value;
        
        BandwidthRecord() : timestamp(Seconds(0)), trace_bandwidth(0.0), 
                           gcc_bandwidth(0.0), scaled_bandwidth(0.0), mu_value(1.0) {}
        
        BandwidthRecord(Time ts, double trace_bw, double gcc_bw, double scaled_bw, double mu)
            : timestamp(ts), trace_bandwidth(trace_bw), gcc_bandwidth(gcc_bw), 
              scaled_bandwidth(scaled_bw), mu_value(mu) {}
    };
    
    // 帧统计数据结构（在FrameManager类内部定义）
    struct FrameStatistics {
        uint32_t frame_id;
        Time deadline;
        uint32_t frame_size;
        uint32_t frame_type;
        uint32_t total_packets;
        uint32_t packets_received;
        Time first_packet_arrival_time;
        Time last_packet_arrival_time;
        uint32_t packets_before_deadline;
        uint32_t missed_deadline;
        Time stall_duration;
        std::vector<Time> packet_arrival_times;
        std::vector<bool> packet_received;
        bool frame_completed;
        
        FrameStatistics() 
            : frame_id(0), deadline(Seconds(0)), frame_size(0), frame_type(0),
              total_packets(0), packets_received(0), first_packet_arrival_time(Seconds(0)),
              last_packet_arrival_time(Seconds(0)), packets_before_deadline(0), 
              missed_deadline(0), stall_duration(Seconds(0)), frame_completed(false) {}
              
        // 从VideoFrame初始化
        FrameStatistics(const VideoFrame& video_frame)
            : frame_id(video_frame.frame_id), deadline(video_frame.deadline),
              frame_size(video_frame.frame_size), frame_type(video_frame.frame_type),
              total_packets(video_frame.total_packets), packets_received(0),
              first_packet_arrival_time(Seconds(0)), last_packet_arrival_time(Seconds(0)),
              packets_before_deadline(0), missed_deadline(0), stall_duration(Seconds(0)),
              frame_completed(false) {
            
            packet_arrival_times.resize(total_packets, Seconds(0));
            packet_received.resize(total_packets, false);
        }
    };
    
    FrameManager(VideoTraceManager* trace_manager = nullptr, RLStateManager* rl_manager = nullptr) 
        : current_trace_bandwidth_(0.0),
          current_gcc_bandwidth_(0.0),
          current_scaled_bandwidth_(0.0),
          m_bandwidth_changer(nullptr),
          current_frame_id(0), 
          last_frame_complete_time(Seconds(0)), 
          packet_counter(0), 
          trace_manager(trace_manager), 
          rl_manager_(rl_manager) {
        
        if (trace_manager && trace_manager->IsLoaded()) {
            NS_LOG_INFO("FrameManager initialized with video trace, total frames: " 
                       << trace_manager->GetTotalFrames());
        } else {
            NS_LOG_WARN("FrameManager initialized without video trace!");
        }
    }
    
    // 处理数据包到达 - 修改为使用真实发送时间
    // 参数 real_send_time_ms: 从 WebrtcTag 中获取的真实发送时间（毫秒）
    // 如果 real_send_time_ms < 0，则使用估计值（兼容旧代码）
    void ProcessPacketArrival(Time arrival_time, int64_t real_send_time_ms = -1) {
        uint32_t packet_id = packet_counter++;
        
        // 计算当前包属于哪个帧和该帧内的包索引
        uint32_t frame_id = 0;
        uint32_t packet_index_in_frame = 0;
        
        if (!FindFrameForPacket(packet_id, frame_id, packet_index_in_frame)) {
            NS_LOG_DEBUG("Packet " << packet_id << " does not belong to any known frame");
            return;
        }
        
        NS_LOG_DEBUG("Processing packet " << packet_id << " for frame " << frame_id 
                     << " (index " << packet_index_in_frame << ") at time " 
                     << arrival_time.GetSeconds() << "s");
        
        // 获取或创建帧统计
        FrameStatistics& frame = GetOrCreateFrameStatistics(frame_id);
        
        // 检查包索引是否有效
        if (packet_index_in_frame >= frame.total_packets) {
            NS_LOG_WARN("Packet index " << packet_index_in_frame << " exceeds frame " 
                        << frame_id << " total packets " << frame.total_packets);
            return;
        }
        
        // 更新帧统计
        frame.packets_received++;
        
        // 记录包到达时间
        if (frame.packet_arrival_times.size() <= packet_index_in_frame) {
            frame.packet_arrival_times.resize(packet_index_in_frame + 1, Seconds(0));
        }
        frame.packet_arrival_times[packet_index_in_frame] = arrival_time;
        
        // 标记包已接收
        if (frame.packet_received.size() <= packet_index_in_frame) {
            frame.packet_received.resize(packet_index_in_frame + 1, false);
        }
        frame.packet_received[packet_index_in_frame] = true;
        
        // 更新第一个和最后一个包到达时间
        if (frame.first_packet_arrival_time == Seconds(0) || 
            arrival_time < frame.first_packet_arrival_time) {
            frame.first_packet_arrival_time = arrival_time;
        }
        if (arrival_time > frame.last_packet_arrival_time) {
            frame.last_packet_arrival_time = arrival_time;
        }
        
        // 检查是否在截止时间前到达
        if (arrival_time <= frame.deadline) {
            frame.packets_before_deadline++;
        }
        
        NS_LOG_DEBUG("Frame " << frame_id << ": packets_received=" << frame.packets_received 
                     << "/" << frame.total_packets << ", packets_before_deadline=" 
                     << frame.packets_before_deadline << ", last_packet_time=" 
                     << frame.last_packet_arrival_time.GetSeconds() << "s, deadline=" 
                     << frame.deadline.GetSeconds() << "s");
        
        // 计算发送时间：使用真实发送时间或估计值
        Time send_time = GetSendTime(arrival_time, real_send_time_ms);
        
        // 触发RL状态记录 - 使用真实延迟
        if (rl_manager_ != nullptr) {
            TriggerRLStateForPacket(frame_id, packet_index_in_frame, send_time, arrival_time);
        }
        
        // 检查帧是否完成（收到所有包）
        if (frame.packets_received >= frame.total_packets && !frame.frame_completed) {
            CompleteFrame(frame_id);
        }
    }
    
    // 获取数据包的发送时间
    // 优先使用真实发送时间（来自 WebrtcTag），否则使用估计值
    Time GetSendTime(Time arrival_time, int64_t real_send_time_ms) {
        if (real_send_time_ms >= 0) {
            // 使用真实发送时间（从 WebrtcTag 中获取）
            Time send_time = MilliSeconds(real_send_time_ms);
            NS_LOG_DEBUG("Using REAL send time: " << send_time.GetMilliSeconds() << "ms");
            return send_time;
        } else {
            // 兼容旧代码：使用估计值
            // 注意：这是后备方案，正常情况下不应该走到这里
            Time network_delay = MilliSeconds(20.0);
            Time send_time = arrival_time - network_delay;
            
            if (send_time < Seconds(0)) {
                send_time = Seconds(0);
            }
            
            NS_LOG_WARN("Using ESTIMATED send time (real_send_time not available): " 
                       << send_time.GetMilliSeconds() << "ms");
            return send_time;
        }
    }
    
    // 完成帧处理
    void CompleteFrame(uint32_t frame_id) {
        auto it = frames.find(frame_id);
        if (it == frames.end()) {
            NS_LOG_ERROR("Attempted to complete non-existent frame " << frame_id);
            return;
        }
        
        FrameStatistics& frame = it->second;
        frame.frame_completed = true;
        
        // 检查是否错过截止时间
        if (frame.last_packet_arrival_time > frame.deadline) {
            frame.missed_deadline = 1;
            frame.stall_duration = frame.last_packet_arrival_time - frame.deadline;
        } else {
            frame.missed_deadline = 0;
            frame.stall_duration = Seconds(0);
        }
        
        // 更新上一帧完成时间
        last_frame_complete_time = frame.last_packet_arrival_time;
        
        NS_LOG_INFO("Frame " << frame_id << " completed: " 
                   << "deadline_miss=" << frame.missed_deadline 
                   << ", stall=" << frame.stall_duration.GetMilliSeconds() << "ms"
                   << ", delivery_ratio=" << (frame.packets_before_deadline * 100.0 / frame.total_packets) << "%"
                   << ", type=" << (frame.frame_type == 1 ? "I-frame" : "P-frame"));
        
        // 移动到下一帧
        current_frame_id = frame_id + 1;
    }
    
    // 强制完成所有未完成的帧（在仿真结束时调用）
    void CompleteAllFrames() {
        NS_LOG_INFO("Completing all unfinished frames at simulation end");
        for (auto& pair : frames) {
            FrameStatistics& frame = pair.second;
            if (!frame.frame_completed && frame.packets_received > 0) {
                CompleteFrame(frame.frame_id);
            }
        }
    }
    
    // 获取所有帧统计
    const std::map<uint32_t, FrameStatistics>& GetFrameStatistics() const {
        return frames;
    }
    
    // 输出帧统计到文件
    void OutputFrameStatistics(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open frame statistics file: " << filename);
            return;
        }
        
        // 写入表头
        file << "frame_id,frame_type,deadline_time,first_packet_time,last_packet_time,"
             << "frame_size,total_packets,packets_received,packets_before_deadline,"
             << "delivery_ratio,missed_deadline,stall_duration_ms" << std::endl;
        
        int frames_with_data = 0;
        for (const auto& pair : frames) {
            const FrameStatistics& frame = pair.second;
            if (frame.total_packets == 0) continue;
            
            double delivery_ratio = (frame.packets_before_deadline * 100.0) / frame.total_packets;
            
            file << frame.frame_id << ","
                 << frame.frame_type << ","
                 << frame.deadline.GetSeconds() << ","
                 << frame.first_packet_arrival_time.GetSeconds() << ","
                 << frame.last_packet_arrival_time.GetSeconds() << ","
                 << frame.frame_size << ","
                 << frame.total_packets << ","
                 << frame.packets_received << ","
                 << frame.packets_before_deadline << ","
                 << delivery_ratio << ","
                 << frame.missed_deadline << ","
                 << frame.stall_duration.GetMilliSeconds() << std::endl;
            
            frames_with_data++;
        }
        
        file.close();
        NS_LOG_INFO("Frame statistics saved to: " << filename << " with " << frames_with_data << " frames containing data");
        
        if (frames_with_data == 0) {
            NS_LOG_WARN("No frame data was recorded! Check if packet callbacks are working.");
            std::cout << "WARNING: No frame data was recorded in " << filename << std::endl;
        }
    }
    
    // 获取总包数（用于调试）
    uint32_t GetTotalPackets() const {
        return packet_counter;
    }
    
    // 设置视频trace管理器
    void SetVideoTraceManager(VideoTraceManager* manager) {
        trace_manager = manager;
        if (manager && manager->IsLoaded()) {
            NS_LOG_INFO("Video trace manager set with " << manager->GetTotalFrames() << " frames");
        }
    }
    
    // 新增：获取帧统计摘要
    void GetFrameStatsSummary(uint32_t& total_frames, uint32_t& missed_deadline_frames, 
                             double& avg_delivery_ratio, double& avg_stall_ms) {
        total_frames = 0;
        missed_deadline_frames = 0;
        double total_delivery_ratio = 0.0;
        double total_stall_ms = 0.0;
        
        for (const auto& pair : frames) {
            const FrameStatistics& frame = pair.second;
            if (frame.total_packets == 0) continue;
            
            total_frames++;
            if (frame.missed_deadline) {
                missed_deadline_frames++;
            }
            
            double delivery_ratio = (frame.packets_before_deadline * 100.0) / frame.total_packets;
            total_delivery_ratio += delivery_ratio;
            total_stall_ms += frame.stall_duration.GetMilliSeconds();
        }
        
        if (total_frames > 0) {
            avg_delivery_ratio = total_delivery_ratio / total_frames;
            avg_stall_ms = total_stall_ms / total_frames;
        } else {
            avg_delivery_ratio = 0.0;
            avg_stall_ms = 0.0;
        }
    }
    
    // 设置RL状态管理器
    void SetRLStateManager(RLStateManager* rl_manager) {
        rl_manager_ = rl_manager;
        NS_LOG_INFO("RLStateManager set in FrameManager");
    }

    // 新增：添加带宽记录
    void AddBandwidthRecord(Time timestamp, double trace_bw, double gcc_bw, double scaled_bw, double mu) {
        // 清理旧记录（只保留最近60秒的记录）
        Time cleanup_threshold = timestamp - Seconds(60);
        while (!bandwidth_history_.empty() && bandwidth_history_.front().timestamp < cleanup_threshold) {
            bandwidth_history_.pop_front();
        }
        
        // 添加新记录
        bandwidth_history_.push_back(BandwidthRecord(timestamp, trace_bw, gcc_bw, scaled_bw, mu));
        
        // 更新当前值
        current_trace_bandwidth_ = trace_bw;
        current_gcc_bandwidth_ = gcc_bw;
        current_scaled_bandwidth_ = scaled_bw;
        
        NS_LOG_DEBUG("Added bandwidth record at " << timestamp.GetSeconds() 
                    << "s: trace=" << trace_bw << " bps, gcc=" << gcc_bw 
                    << " bps, scaled=" << scaled_bw << " bps, μ=" << mu);
    }
    
    // 获取最近带宽记录
    BandwidthRecord GetLatestBandwidthRecord() const {
        if (!bandwidth_history_.empty()) {
            return bandwidth_history_.back();
        }
        return BandwidthRecord();
    }
    
    // 根据时间获取带宽记录（找到最接近的时间戳）
    BandwidthRecord GetBandwidthAtTime(Time timestamp) const {
        std::cout << "[FrameManager-DEBUG] GetBandwidthAtTime called for time: " 
                  << timestamp.GetSeconds() << "s (" << timestamp.GetMilliSeconds() << "ms)" << std::endl;
        std::cout << "  Bandwidth history size: " << bandwidth_history_.size() << std::endl;
        
        if (bandwidth_history_.empty()) {
            // 关键修改：如果历史记录为空，使用BandwidthChanger获取trace带宽
            double trace_bw = 0.0;
            double gcc_bw = 0.0;
            double scaled_bw = 0.0;
            double mu_value = 1.0;
            
            if (rl_manager_) {
                mu_value = rl_manager_->GetCurrentMu();
                std::cout << "[DEBUG] Got mu from RL manager: " << mu_value << std::endl;
            }
            
            // 尝试从BandwidthChanger获取trace带宽
            if (m_bandwidth_changer) {
                uint32_t timestamp_ms = static_cast<uint32_t>(timestamp.GetMilliSeconds());
                TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
                trace_bw = trace_data.bandwidth;
                std::cout << "[DEBUG] Got trace bandwidth from BandwidthChanger: " << trace_bw << " bps" << std::endl;
            }
            
            // 如果没有trace带宽，使用默认值
            if (trace_bw <= 0.0) {
                trace_bw = current_trace_bandwidth_;
                std::cout << "[WARNING] Using current_trace_bandwidth_: " << trace_bw << std::endl;
                if (trace_bw <= 0.0) {
                    trace_bw = 20 * 1000000.0; // 默认20Mbps
                    std::cout << "[WARNING] Using default trace bandwidth: " << trace_bw << std::endl;
                }
            }
            
            // 关键修改：从FrameManager中获取最近的GCC带宽记录
            gcc_bw = current_gcc_bandwidth_;
            
            // 如果没有GCC带宽记录，使用trace带宽作为估计
            if (gcc_bw <= 0.0) {
                // 如果有带宽历史记录，尝试找到最近的GCC带宽
                if (!bandwidth_history_.empty()) {
                    // 查找最近1秒内的GCC带宽记录
                    Time recent_threshold = timestamp - Seconds(1);
                    for (auto it = bandwidth_history_.rbegin(); it != bandwidth_history_.rend(); ++it) {
                        if (it->timestamp >= recent_threshold && it->gcc_bandwidth > 0) {
                            gcc_bw = it->gcc_bandwidth;
                            std::cout << "[DEBUG] Found recent GCC bandwidth in history: " << gcc_bw << " bps" << std::endl;
                            break;
                        }
                    }
                }
                
                // 如果还是没有找到GCC带宽，使用trace带宽作为估计
                if (gcc_bw <= 0.0) {
                    gcc_bw = trace_bw;
                    std::cout << "[WARNING] Using trace bandwidth for GCC bandwidth: " << gcc_bw << std::endl;
                }
            }
            
            scaled_bw = gcc_bw * mu_value;
            
            BandwidthRecord default_record(timestamp, trace_bw, gcc_bw, scaled_bw, mu_value);
            
            std::cout << "[FrameManager-DEBUG] History empty, returning calculated:" << std::endl;
            std::cout << "  trace=" << default_record.trace_bandwidth 
                      << ", gcc=" << default_record.gcc_bandwidth
                      << ", scaled=" << default_record.scaled_bandwidth 
                      << ", μ=" << default_record.mu_value << std::endl;
            return default_record;
        }
        
        // 找到最接近的时间戳的记录
        BandwidthRecord closest_record = bandwidth_history_.front();
        Time min_difference = Abs(timestamp - closest_record.timestamp);
        
        for (const auto& record : bandwidth_history_) {
            Time difference = Abs(timestamp - record.timestamp);
            if (difference < min_difference) {
                min_difference = difference;
                closest_record = record;
            }
        }
        
        std::cout << "[FrameManager-DEBUG] Closest record found:" << std::endl;
        std::cout << "  Time diff: " << min_difference.GetSeconds() << "s" << std::endl;
        std::cout << "  Record time: " << closest_record.timestamp.GetSeconds() << "s" << std::endl;
        std::cout << "  trace=" << closest_record.trace_bandwidth 
                  << ", gcc=" << closest_record.gcc_bandwidth
                  << ", scaled=" << closest_record.scaled_bandwidth 
                  << ", μ=" << closest_record.mu_value << std::endl;
        
        // 如果最接近的记录与查询时间相差超过1秒，尝试使用BandwidthChanger
        if (min_difference > Seconds(1.0)) {
            double trace_bw = 0.0;
            if (m_bandwidth_changer) {
                uint32_t timestamp_ms = static_cast<uint32_t>(timestamp.GetMilliSeconds());
                TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
                trace_bw = trace_data.bandwidth;
                std::cout << "[DEBUG] Got fallback trace bandwidth from BandwidthChanger: " 
                          << trace_bw << " bps" << std::endl;
            }
            
            if (trace_bw <= 0.0) {
                trace_bw = closest_record.trace_bandwidth;
            }
            
            // 使用最近的GCC带宽
            double gcc_bw = closest_record.gcc_bandwidth;
            if (gcc_bw <= 0.0) {
                gcc_bw = trace_bw;
            }
            
            BandwidthRecord fallback_record(timestamp, trace_bw, gcc_bw,
                                          gcc_bw * closest_record.mu_value, closest_record.mu_value);
            
            return fallback_record;
        }
        
        std::cout << "[FrameManager-DEBUG] Returning closest record" << std::endl;
        return closest_record;
    }
    
    // 获取当前带宽值（使用最近记录）
    double GetCurrentTraceBandwidth() const { 
        if (!bandwidth_history_.empty()) {
            return bandwidth_history_.back().trace_bandwidth;
        }
        return current_trace_bandwidth_; 
    }
    
    double GetCurrentGccBandwidth() const { 
        if (!bandwidth_history_.empty()) {
            return bandwidth_history_.back().gcc_bandwidth;
        }
        return current_gcc_bandwidth_; 
    }
    
    double GetCurrentScaledBandwidth() const { 
        if (!bandwidth_history_.empty()) {
            return bandwidth_history_.back().scaled_bandwidth;
        }
        return current_scaled_bandwidth_; 
    }
    
    // 设置BandwidthChanger（关键修改）
    void SetBandwidthChanger(BandwidthChanger* changer) {
        m_bandwidth_changer = changer;
        if (changer) {
            std::cout << "[DEBUG] BandwidthChanger set in FrameManager" << std::endl;
        }
    }
    
    // 输出带宽历史记录（用于调试）
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
    
    // 提供获取带宽历史记录的方法（新增）
    const std::deque<BandwidthRecord>& GetBandwidthHistory() const {
        return bandwidth_history_;
    }
    
    // 新增方法：获取指定时间附近的真实GCC带宽
    double GetNearestGccBandwidth(Time timestamp) const {
        if (bandwidth_history_.empty()) {
            return current_gcc_bandwidth_;
        }
        
        // 查找最近1秒内的GCC带宽记录
        Time recent_threshold = timestamp - Seconds(1);
        double nearest_gcc = 0.0;
        Time min_diff = Seconds(1000); // 大值
        
        for (const auto& record : bandwidth_history_) {
            if (record.timestamp >= recent_threshold && record.gcc_bandwidth > 0) {
                Time diff = Abs(timestamp - record.timestamp);
                if (diff < min_diff) {
                    min_diff = diff;
                    nearest_gcc = record.gcc_bandwidth;
                }
            }
        }
        
        if (nearest_gcc > 0.0) {
            return nearest_gcc;
        }
        
        // 如果没有找到最近的，返回最近的GCC带宽
        for (const auto& record : bandwidth_history_) {
            if (record.gcc_bandwidth > 0.0) {
                return record.gcc_bandwidth;
            }
        }
        
        return current_gcc_bandwidth_;
    }
    
private:
    // 新增：为每个包触发RL状态记录 - 在类内部定义，可以访问BandwidthChanger
    void TriggerRLStateForPacket(uint32_t frame_id, uint32_t packet_index, Time send_time, Time arrival_time) {
        std::cout << "\n[FRAME_DEBUG] TriggerRLStateForPacket called at " << arrival_time.GetSeconds() << "s" << std::endl;
        std::cout << "  Frame: " << frame_id << ", Packet: " << packet_index << std::endl;
        std::cout << "  Send time: " << send_time.GetSeconds() << "s, Arrival time: " << arrival_time.GetSeconds() << "s" << std::endl;
        
        if (!rl_manager_) {
            std::cout << "[ERROR] RL manager is null!" << std::endl;
            return;
        }
        
        // 获取当前帧信息
        auto frame_it = frames.find(frame_id);
        if (frame_it == frames.end()) {
            std::cout << "[ERROR] Frame " << frame_id << " not found!" << std::endl;
            return;
        }
        
        const FrameStatistics& frame = frame_it->second;
        
        // 计算实际延迟（接收时间 - 发送时间）
        double current_delay_ms = (arrival_time - send_time).GetMilliSeconds();
        std::cout << "[DEBUG] Actual packet delay: " << current_delay_ms << "ms" << std::endl;
        
        // 从trace获取当前的RTT和loss值
        double trace_rtt_ms = 30.0;  // 默认值
        double trace_loss_rate = 0.01;  // 默认值
        
        if (m_bandwidth_changer) {
            uint32_t timestamp_ms = static_cast<uint32_t>(arrival_time.GetMilliSeconds());
            TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
            trace_rtt_ms = trace_data.rtt;
            trace_loss_rate = trace_data.loss;
            
            std::cout << "[DEBUG] Got trace data: RTT=" << trace_rtt_ms << "ms, Loss=" << trace_loss_rate << std::endl;
        }
        
        // 使用实际的延迟和trace中的RTT、loss更新RL状态管理器
        rl_manager_->UpdateNetworkState(current_delay_ms, trace_loss_rate, MilliSeconds(trace_rtt_ms));
        
        // 获取当前网络参数
        double mu_used = rl_manager_->GetCurrentMu();
        double current_loss_rate = trace_loss_rate;  // 使用trace中的loss值
        double Rt_prev = rl_manager_->GetLastPacketRt();
        
        std::cout << "[DEBUG] Network parameters:" << std::endl;
        std::cout << "  mu_used: " << mu_used << std::endl;
        std::cout << "  current_loss_rate (from trace): " << current_loss_rate << std::endl;
        std::cout << "  Rt_prev: " << Rt_prev << std::endl;
        std::cout << "  current_delay: " << current_delay_ms << "ms" << std::endl;
        std::cout << "  RTT (from trace): " << trace_rtt_ms << "ms" << std::endl;
        
        // 获取带宽信息 - 根据时间戳获取最接近的带宽记录
        BandwidthRecord bw_record = GetBandwidthAtTime(arrival_time);
        double trace_bandwidth = bw_record.trace_bandwidth;
        double gcc_bandwidth = bw_record.gcc_bandwidth;
        double scaled_gcc_bandwidth = bw_record.scaled_bandwidth;
        
        // 调试输出
        std::cout << "[DEBUG] Bandwidth information:" << std::endl;
        std::cout << "  trace_bandwidth: " << trace_bandwidth << " bps (" 
                  << (trace_bandwidth / 1000000.0) << " Mbps)" << std::endl;
        std::cout << "  gcc_bandwidth: " << gcc_bandwidth << " bps (" 
                  << (gcc_bandwidth / 1000000.0) << " Mbps)" << std::endl;
        std::cout << "  scaled_gcc_bandwidth: " << scaled_gcc_bandwidth << " bps (" 
                  << (scaled_gcc_bandwidth / 1000000.0) << " Mbps)" << std::endl;
        std::cout << "  mu_used: " << mu_used << std::endl;
        std::cout << "  Record mu: " << bw_record.mu_value << std::endl;
        
        // 关键修改：如果GCC带宽为0或不可信，尝试获取最近的GCC带宽
        if (gcc_bandwidth <= 0.0 || gcc_bandwidth == trace_bandwidth) {
            gcc_bandwidth = GetNearestGccBandwidth(arrival_time);
            std::cout << "[DEBUG] Using nearest GCC bandwidth: " << gcc_bandwidth << " bps" << std::endl;
            
            // 重新计算缩放后的带宽
            scaled_gcc_bandwidth = gcc_bandwidth * mu_used;
            std::cout << "[DEBUG] Recalculated scaled bandwidth: " << scaled_gcc_bandwidth 
                      << " (gcc=" << gcc_bandwidth << " * mu=" << mu_used << ")" << std::endl;
        }
        
        // 如果带宽值为0，尝试使用BandwidthChanger获取真实带宽
        if (trace_bandwidth <= 0.0 && m_bandwidth_changer) {
            uint32_t timestamp_ms = static_cast<uint32_t>(arrival_time.GetMilliSeconds());
            TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
            trace_bandwidth = trace_data.bandwidth;
            std::cout << "[DEBUG] Got trace bandwidth from BandwidthChanger: " 
                     << trace_bandwidth << " bps" << std::endl;
            
            if (trace_bandwidth <= 0.0) {
                trace_bandwidth = 20 * 1000000.0; // 默认20Mbps
                std::cout << "[WARNING] Using default trace bandwidth: " << trace_bandwidth << std::endl;
            }
        }

        // 确保GCC带宽有效且不等于trace带宽（除非它们确实相同）
        if (gcc_bandwidth <= 0.0 || gcc_bandwidth == trace_bandwidth) {
            // 使用一个合理的默认值，比如trace带宽的70%
            gcc_bandwidth = trace_bandwidth * 0.7;
            std::cout << "[WARNING] GCC bandwidth was 0 or equal to trace, using scaled value: " 
                      << gcc_bandwidth << " bps (70% of trace)" << std::endl;
        }
        
        // 重新计算缩放后的带宽（使用当前的μ）
        if (scaled_gcc_bandwidth <= 0.0 || mu_used != bw_record.mu_value) {
            scaled_gcc_bandwidth = gcc_bandwidth * mu_used;
            std::cout << "[DEBUG] Recalculated scaled bandwidth: " << scaled_gcc_bandwidth 
                      << " (gcc=" << gcc_bandwidth << " * mu=" << mu_used << ")" << std::endl;
        }

        // 计算传输机会 - 使用trace中的RTT
        uint32_t Rt = rl_manager_->CalculateTransmissionOpportunities(
            arrival_time, frame.deadline, DEFAULT_PACKET_SIZE, trace_bandwidth);
        
        std::cout << "[DEBUG] Calculated Rt: " << Rt << " using RTT=" << trace_rtt_ms << "ms" << std::endl;

        double miss_deadline_time = 0.0;
        if (arrival_time > frame.deadline) {
            miss_deadline_time = (arrival_time - frame.deadline).GetSeconds();
            std::cout << "[DEBUG] Missed deadline by: " << miss_deadline_time << "s" << std::endl;
        }

        // 计算实际的奖励 - 使用实际延迟
        double reward = rl_manager_->CalculateReward(
            mu_used, gcc_bandwidth, trace_bandwidth, current_delay_ms, current_loss_rate, 
            miss_deadline_time, Rt, Rt_prev, frame_id, packet_index);
        
        std::cout << "[DEBUG] Calculated reward: " << reward << std::endl;

        double bandwidth_utilization = 0.0;
        if (trace_bandwidth > 0.0) {
            bandwidth_utilization = scaled_gcc_bandwidth / trace_bandwidth;
            bandwidth_utilization = std::min(std::max(bandwidth_utilization, 0.0), 1.0);
            std::cout << "[DEBUG] Bandwidth utilization: " << bandwidth_utilization * 100 << "%" << std::endl;
        }
        
        // 计算延迟惩罚 - 使用实际延迟
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
        double delay_sensitivity = 1.0 + (1.0 - Rt_prev  / 10.0) * 0.3;
        p_delay *= delay_sensitivity;
        
        // (3) 丢包率惩罚
        double Ptget = 1.0 - rl_manager_->GetMaxLossRate();  // 使用max_loss_rate
        double Ltol;
        if (Rt_prev == 0){
            Ltol = 0.01;
        } else {
            Ltol = std::pow(1.0 - Ptget, 1.0 / Rt_prev);
        }
        double adaptive_tolerance = Ltol * (1.0 + 0.5 * (Rt_prev / 10.0));
        double p_loss = current_loss_rate / adaptive_tolerance;
        p_loss = std::min(p_loss, 1.0);
        
        // (4) 错过截止时间惩罚
        double p_mddl = 0.0;
        double current_rtt = trace_rtt_ms / 1000.0; // 转换为秒，使用trace中的RTT
        
        if (packet_index == 0) {
            if (Rt > 0) {
                double denominator = (Rt - Rt_prev + 1) * current_rtt;
                if (denominator > 0.001) {
                    p_mddl = miss_deadline_time / denominator;
                    p_mddl = std::min(std::max(p_mddl, 0.0), 1.0);
                }
            }
        } else {
            uint32_t Rt_frame_first = Rt + packet_index;
            if (Rt_frame_first > 0) {
                double denominator = (Rt_frame_first - Rt_prev + 1) * current_rtt;
                if (denominator > 0.001) {
                    p_mddl = miss_deadline_time / denominator;
                    p_mddl = std::min(std::max(p_mddl, 0.0), 1.0);
                }
            }
        }

        // 记录真实的包状态 - 包括接收时间和实际延迟
        rl_manager_->RecordPacketState(
            frame_id, packet_index, mu_used, Rt, current_loss_rate, reward,
            send_time, arrival_time, frame.deadline, bandwidth_utilization, p_delay, p_loss, p_mddl, current_delay_ms);
        
        std::cout << "[SUCCESS] RL State Recorded for Frame " << frame_id 
                  << " Packet " << packet_index << std::endl;
        std::cout << "  Send time: " << send_time.GetSeconds() << "s" << std::endl;
        std::cout << "  Recv time: " << arrival_time.GetSeconds() << "s" << std::endl;
        std::cout << "  Actual delay: " << current_delay_ms << "ms" << std::endl;
        std::cout << "  Trace BW: " << trace_bandwidth << " bps" << std::endl;
        std::cout << "  GCC BW: " << gcc_bandwidth << " bps" << std::endl;
        std::cout << "  Scaled BW: " << scaled_gcc_bandwidth << " bps (μ=" << mu_used << ")" << std::endl;
        std::cout << "  Bandwidth Utilization: " << (bandwidth_utilization * 100) << "%" << std::endl;
        std::cout << "  Rt: " << Rt << ", Reward: " << reward << std::endl;
        std::cout << "  RTT from trace: " << trace_rtt_ms << "ms" << std::endl;
        std::cout << "  Loss from trace: " << trace_loss_rate << std::endl;
        std::cout << "========================================" << std::endl;
        
        NS_LOG_DEBUG("Recorded REAL RL state for frame " << frame_id << " packet " << packet_index
                   << " at time " << arrival_time.GetSeconds() << "s, delay=" << current_delay_ms 
                   << "ms, Rt=" << Rt << ", reward=" << reward);
    }
    
    // 查找包所属的帧
    bool FindFrameForPacket(uint32_t packet_id, uint32_t& frame_id, uint32_t& packet_index_in_frame) {
        if (!trace_manager || !trace_manager->IsLoaded()) {
            return false;
        }
        
        uint32_t current_packet = 0;
        for (uint32_t i = 0; i < trace_manager->GetTotalFrames(); i++) {
            const VideoFrame* frame = trace_manager->GetFrame(i);
            if (!frame) continue;
            
            if (packet_id >= current_packet && packet_id < current_packet + frame->total_packets) {
                frame_id = i;
                packet_index_in_frame = packet_id - current_packet;
                return true;
            }
            current_packet += frame->total_packets;
        }
        
        return false;
    }
    
    // 获取或创建帧统计
    FrameStatistics& GetOrCreateFrameStatistics(uint32_t frame_id) {
        auto it = frames.find(frame_id);
        if (it != frames.end()) {
            return it->second;
        }
        
        // 创建新的帧统计
        if (trace_manager && trace_manager->IsLoaded()) {
            const VideoFrame* video_frame = trace_manager->GetFrame(frame_id);
            if (video_frame) {
                FrameStatistics new_frame(*video_frame);
                frames[frame_id] = new_frame;
                NS_LOG_DEBUG("Created frame statistics for frame " << frame_id 
                           << " with " << new_frame.total_packets << " packets");
                return frames[frame_id];
            }
        }
        
        // 如果没有trace信息，创建默认帧统计
        FrameStatistics default_frame;
        default_frame.frame_id = frame_id;
        frames[frame_id] = default_frame;
        return frames[frame_id];
    }
    
private:
    double current_trace_bandwidth_;
    double current_gcc_bandwidth_;
    double current_scaled_bandwidth_;
    BandwidthChanger* m_bandwidth_changer;  // 新增：存储BandwidthChanger指针
    std::deque<BandwidthRecord> bandwidth_history_;  // 带宽历史记录
    
    std::map<uint32_t, FrameStatistics> frames;
    uint32_t current_frame_id;
    Time last_frame_complete_time;
    uint32_t packet_counter;
    VideoTraceManager* trace_manager;
    RLStateManager* rl_manager_;   // 新增：指向 RL 状态管理器
};

// 增强的WebrtcTrace类来支持帧管理和带宽缩放统计
class FrameAwareWebrtcTrace : public WebrtcTrace {
public:
    // 定义回调类型
    typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;
    typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;
    typedef Callback<uint32_t, uint32_t> GetTraceBandwidthCallback;  // 新增：获取trace带宽的回调
    
    FrameAwareWebrtcTrace(FrameManager* frame_manager = nullptr, RLStateManager* rl_manager = nullptr, 
                         double bandwidth_scale_factor = 1.0) 
        : frame_manager_(frame_manager), rl_manager_(rl_manager), 
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
        
        // 如果有帧管理器，记录包到达时间，并传递真实发送时间
        if (frame_manager_) {
            Time arrival_time = MilliSeconds(now);
            
            // 计算真实发送时间：send_time = now - owd
            // owd = 接收时间 - 发送时间（毫秒）
            int64_t real_send_time_ms = static_cast<int64_t>(now) - static_cast<int64_t>(owd);
            
            NS_LOG_DEBUG("Real send time calculated: arrival=" << now 
                        << "ms, owd=" << owd << "ms, send_time=" << real_send_time_ms << "ms");
            
            frame_manager_->ProcessPacketArrival(arrival_time, real_send_time_ms);
        } else {
            NS_LOG_WARN("Frame manager is null in FrameAwareWebrtcTrace!");
        }
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
        if (frame_manager_) {
            Time timestamp = MilliSeconds(now);
            
            // 使用提供的公有方法而不是直接访问私有成员
            const auto& history = frame_manager_->GetBandwidthHistory();
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
                frame_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, bps, scaled_bw, current_mu_val);
                
                std::cout << "[FrameManager-Bandwidth] Time: " << now << "ms:" << std::endl;
                std::cout << "  Trace BW: " << trace_data.bandwidth << " bps" << std::endl;
                std::cout << "  GCC BW: " << bps << " bps" << std::endl;
                std::cout << "  Scaled BW: " << scaled_bw << " bps (μ=" << current_mu_val << ")" << std::endl;
                std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
                std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
                std::cout << "  Bandwidth history size: " << history.size() + 1 << std::endl;
            }
        } else {
            std::cout << "[ERROR] Frame manager is null in FrameAwareWebrtcTrace!" << std::endl;
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
        if (frame_manager_) {
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
            
            frame_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, original_bps, scaled_bps, scale_factor);
            
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
    
    void SetFrameManager(FrameManager* frame_manager) {
        frame_manager_ = frame_manager;
        NS_LOG_INFO("Frame manager set in FrameAwareWebrtcTrace");
    }
    
    void SetRLStateManager(RLStateManager* rl_manager) {
        rl_manager_ = rl_manager;
        NS_LOG_INFO("RLStateManager set in FrameAwareWebrtcTrace");
    }
    
    void SetBandwidthScaleFactor(double factor) {
        bandwidth_scale_factor_ = factor;
        NS_LOG_INFO("Bandwidth scale factor updated to: " << bandwidth_scale_factor_);
    }
    
private:
    FrameManager* frame_manager_;
    RLStateManager* rl_manager_;
    uint32_t total_bw_changes;
    double bandwidth_scale_factor_;
    std::vector<std::pair<uint32_t, uint32_t>> original_bw_history;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, double>> scaled_bw_history;

    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    GetTraceBandwidthCallback m_getTraceBw;  // 新增：获取trace带宽的回调
    BandwidthChanger* m_changer;  // 新增：直接存储带宽changer指针
    
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
                        FrameManager* frame_manager = nullptr,
                        RLStateManager* rl_manager = nullptr,
                        double bandwidth_scale_factor = 1.0,
                        double loss_rate = 0.01,
                        BandwidthChanger* bandwidth_changer = nullptr,
                        VideoTraceManager* video_trace_manager = nullptr)
{
    std::cout << "\n[DEBUG] InstallWebrtcApplication called" << std::endl;
    std::cout << "  Bandwidth scale factor: " << bandwidth_scale_factor << std::endl;
    std::cout << "  Loss rate: " << loss_rate << std::endl;
    std::cout << "  FrameManager pointer: " << frame_manager << std::endl;
    std::cout << "  RLStateManager pointer: " << rl_manager << std::endl;
    std::cout << "  BandwidthChanger pointer: " << bandwidth_changer << std::endl;
    
    NS_LOG_INFO("Installing WebRTC application with RL state management and video trace");
    
    // 正确创建应用程序对象
    Ptr<WebrtcSender> sendApp = CreateObject<WebrtcSender>(manager);
    Ptr<WebrtcReceiver> recvApp = CreateObject<WebrtcReceiver>(manager);
    
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
    
    // 确保FrameManager知道RLStateManager
    if (frame_manager && rl_manager) {
        frame_manager->SetRLStateManager(rl_manager);
        NS_LOG_INFO("RLStateManager set in FrameManager for packet-level RL recording");
    }
    
    // 设置FrameManager的BandwidthChanger - 关键修改
    if (frame_manager && bandwidth_changer) {
        frame_manager->SetBandwidthChanger(bandwidth_changer);
        std::cout << "[DEBUG] BandwidthChanger set in FrameManager" << std::endl;
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
        
        std::cout << "=== InstallWebrtcApplication DEBUG ===" << std::endl;
        std::cout << "Scheduling RL state calculation with loss_rate: " << loss_rate << std::endl;
        std::cout << "BandwidthChanger pointer: " << bandwidth_changer << std::endl;
        std::cout << "RLStateManager pointer: " << rl_manager << std::endl;
        std::cout << "======================================" << std::endl;
        
        Simulator::Schedule(Seconds(0.1), &TriggerRLStateCalculation,
                        rl_manager, bandwidth_changer, interval, loss_rate);
    } else {
        if (rl_manager == nullptr) {
            std::cout << "[WARNING] RLStateManager is null, skipping RL state calculation scheduling" << std::endl;
        }
        if (bandwidth_changer == nullptr) {
            std::cout << "[WARNING] BandwidthChanger is null, skipping RL state calculation scheduling" << std::endl;
        }
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
    if (frame_manager && trace) {
        trace->SetFrameManager(frame_manager);
        std::cout << "[DEBUG] FrameManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    if (rl_manager && trace) {
        trace->SetRLStateManager(rl_manager);
        std::cout << "[DEBUG] RLStateManager set in FrameAwareWebrtcTrace" << std::endl;
    }
    
    // 设置视频trace管理器
    if (frame_manager && video_trace_manager) {
        frame_manager->SetVideoTraceManager(video_trace_manager);
        NS_LOG_INFO("Video trace manager set in FrameManager");
    }
    
    // 设置应用程序时间
    sendApp->SetStartTime(start_app);
    sendApp->SetStopTime(stop_app);
    recvApp->SetStartTime(start_app);
    recvApp->SetStopTime(stop_app + Seconds(1));
    
    std::cout << "[SUCCESS] WebRTC application installed successfully" << std::endl;
    NS_LOG_INFO("WebRTC application installed successfully with manager and video trace");
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
        
        std::cout << "[DEBUG] TriggerRLStateCalculation - Trace data:" << std::endl;
        std::cout << "  Time: " << timestamp_ms << "ms" << std::endl;
        std::cout << "  RTT from trace: " << trace_rtt_ms << "ms" << std::endl;
        std::cout << "  Loss from trace: " << trace_loss_rate << std::endl;
    } else {
        std::cout << "[DEBUG] TriggerRLStateCalculation - Using default RTT: " 
                  << trace_rtt_ms << "ms and loss: " << trace_loss_rate << std::endl;
    }
    
    // 获取当前网络状态
    double current_trace_bw = 0.0;
    if (bandwidth_changer != nullptr) {
        current_trace_bw = bandwidth_changer->GetCurrentTraceBandwidth();
        std::cout << "[DEBUG] TriggerRLStateCalculation - Current trace bandwidth: " 
                  << current_trace_bw << " bps" << std::endl;
    } else {
        current_trace_bw = 20*1000000.0; // 20 Mbps 默认值
        NS_LOG_WARN("BandwidthChanger is null, using default bandwidth: " << current_trace_bw << " bps");
    }
    
    // 使用RL状态管理器中的当前延迟（这个值会在每次包到达时更新）
    double current_delay = rl_manager->GetCurrentDelay();
    
    // 更新网络状态 - 使用实际延迟和trace中的RTT、loss
    if (rl_manager != nullptr) {
        rl_manager->UpdateNetworkState(current_delay, trace_loss_rate, MilliSeconds(trace_rtt_ms));
        // 这里不再生成任何测试数据
    } else {
        NS_LOG_WARN("RLStateManager is null, cannot update network state");
    }
    
    // 安排下一次触发
    double next_interval = interval->GetValue();
    
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Simulator::Schedule(Seconds(next_interval), &TriggerRLStateCalculation,
                           rl_manager, bandwidth_changer, interval, trace_loss_rate);
        
        NS_LOG_DEBUG("Triggered RL state calculation at " << now.GetSeconds() << "s with trace_loss=" << trace_loss_rate
                   << " and actual delay=" << current_delay << "ms, trace_RTT=" << trace_rtt_ms << "ms");
    }
}

uint64_t kMillisPerSecond=1000;
uint64_t kMicroPerMillis=1000;

std::unique_ptr<WebrtcSessionManager> CreateWebrtcSessionManager(webrtc::TimeController *controller,
uint32_t max_rate=20000,uint32_t min_rate=100,uint32_t start_rate=500,uint32_t h=720,uint32_t w=1280){
    std::unique_ptr<WebrtcSessionManager> webrtc_manager(new WebrtcSessionManager(controller,min_rate,start_rate,max_rate,h,w));
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
                     double loss_rate = 0.01, const std::string& video_trace_file = "")
{
    std::cout << "\n=== test_app_on_p2p started with Video Trace Analysis ===" << std::endl;
    std::cout << "Instance: " << instance << std::endl;
    std::cout << "Normalized application time: " << startapptime << "s to " << endapptime << "s" << std::endl;
    std::cout << "Total duration: " << (endapptime - startapptime) << " seconds" << std::endl;
    std::cout << "Max bandwidth: " << max_bandwith << " Mbps" << std::endl;
    std::cout << "Trace file: " << trace_filename << std::endl;
    std::cout << "Bandwidth scale factor μ: " << bandwidth_scale_factor << std::endl;
    std::cout << "Loss rate: " << loss_rate << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "BandwidthChanger pointer: " << changer << std::endl;  // 调试输出
    
    NS_ASSERT(startapptime == 0.0);
    
    // 创建视频trace管理器
    std::unique_ptr<VideoTraceManager> video_trace_manager = nullptr;
    if (!video_trace_file.empty()) {
        video_trace_manager = std::make_unique<VideoTraceManager>();
        if (video_trace_manager->LoadVideoTrace(video_trace_file)) {
            std::cout << "Successfully loaded video trace with " << video_trace_manager->GetTotalFrames() 
                      << " frames, last frame deadline: " << video_trace_manager->GetLastFrameDeadline().GetSeconds() << "s" << std::endl;
        } else {
            std::cerr << "Failed to load video trace file: " << video_trace_file << std::endl;
            video_trace_manager.reset();
        }
    }
    
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
    for (int i=0;i<num;i++) {
        std::unique_ptr<WebrtcSessionManager> m(CreateWebrtcSessionManager(time_controller,max_rate));
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
    
    // 创建基于视频trace的帧管理器
    std::vector<std::unique_ptr<FrameManager>> frame_managers;
    for (int i=0;i<num;i++) {
        frame_managers.push_back(std::make_unique<FrameManager>(video_trace_manager.get()));
        std::cout << "Created FrameManager " << i+1 << " with video trace for WebRTC session" << std::endl;
    }
    
    // 创建RL状态管理器
    std::vector<std::unique_ptr<RLStateManager>> rl_managers;
    for (int i = 0; i < num; i++) {
        auto rl_manager = std::make_unique<RLStateManager>();
        
        // 从trace获取初始RTT和loss值
        double initial_rtt = 30.0;  // 默认值
        double initial_loss = loss_rate;  // 使用命令行参数中的loss_rate作为初始值
        
        if (changer && !trace_filename.empty()) {
            // 获取时间0的trace数据
            TraceData initial_trace_data = changer->GetTraceDataAtTime(0);
            initial_rtt = initial_trace_data.rtt;
            initial_loss = initial_trace_data.loss;
            
            std::cout << "=== RLStateManager " << i+1 << " initialized with trace data ===" << std::endl;
            std::cout << "  Initial RTT from trace: " << initial_rtt << "ms" << std::endl;
            std::cout << "  Initial loss from trace: " << initial_loss << std::endl;
            std::cout << "  Bandwidth scale factor (μ): " << bandwidth_scale_factor << std::endl;
            std::cout << "  Max loss rate (Lmax): " << loss_rate << std::endl;
            std::cout << "==========================================" << std::endl;
        }
        
        rl_manager->SetParameters(bandwidth_scale_factor, loss_rate, MilliSeconds(initial_rtt));
        rl_manager->SetCurrentLossRate(initial_loss);  // 使用trace中的初始loss值
        
        rl_managers.push_back(std::move(rl_manager));
    }
        
    for (int i=0;i<num;i++) {
        std::string log=prefix+std::to_string(i+1);
        FrameAwareWebrtcTrace *trace=new FrameAwareWebrtcTrace(frame_managers[i].get(), rl_managers[i].get(), bandwidth_scale_factor);
        trace_vec.push_back(trace);
        
        // 设置trace的当前参数
        trace->SetCurrentParameters(bandwidth_scale_factor, loss_rate);
        
        // Log函数现在会自动添加_mu=..._L=...后缀
        trace->Log(log, WebrtcTrace::E_WEBRTC_BW | WebrtcTrace::E_WEBRTC_LOSS | WebrtcTrace::E_WEBRTC_OWD);
        
        // 设置FrameManager的BandwidthChanger - 关键修改
        if (frame_managers[i] && changer) {
            frame_managers[i]->SetBandwidthChanger(changer);
            std::cout << "BandwidthChanger set for FrameManager " << i+1 << std::endl;
        }
        
        // 设置trace带宽changer
        if (trace && changer) {
            trace->SetBandwidthChanger(changer);
            std::cout << "BandwidthChanger set for session " << i+1 << std::endl;
        }
        
        // 传递正确的changer指针
        InstallWebrtcApplication(nodes.Get(0), nodes.Get(1), sendPort, recvPort,
                    Seconds(startapptime), Seconds(endapptime),
                sesssion_manager.at(i).get(), trace, 
                frame_managers[i].get(), rl_managers[i].get(), 
                bandwidth_scale_factor, loss_rate, changer,  // 直接传递原始指针
                video_trace_manager.get());
        
        sendPort++;
        recvPort++;
        
        std::cout << "WebRTC application " << i+1 << " installed with:" << std::endl;
        std::cout << "  - Bandwidth scaling with μ=" << bandwidth_scale_factor << std::endl;
        std::cout << "  - Loss rate with L=" << loss_rate << " (initial)" << std::endl;
        std::cout << "  - Video trace analysis: " << (video_trace_file.empty() ? "disabled" : "enabled") << std::endl;
        std::cout << "  - BandwidthChanger: " << (changer ? "enabled" : "disabled") << std::endl;
        std::cout << "  - Trace bandwidth recording: " << (changer ? "enabled" : "disabled") << std::endl;
        std::cout << "  - Trace RTT/loss usage: " << (changer ? "enabled" : "disabled") << std::endl;
        std::cout << "  - Output files will include _mu=" << bandwidth_scale_factor << "_L=" << loss_rate << " suffix" << std::endl;
    }

    // 如果使用视频trace，调整仿真时长以匹配视频时长
    float simulation_stop_time = endapptime + 10.0;
    if (video_trace_manager && video_trace_manager->IsLoaded()) {
        Time video_duration = video_trace_manager->GetLastFrameDeadline();
        if (video_duration > Seconds(simulation_stop_time)) {
            simulation_stop_time = video_duration.GetSeconds() + 5.0;
            std::cout << "Adjusted simulation duration to match video: " << simulation_stop_time << "s" << std::endl;
        }
    }
    
    std::cout << "Simulator will stop at: " << simulation_stop_time << " seconds" << std::endl;
    
    Simulator::Stop (Seconds(simulation_stop_time));
    uint64_t last=get_os_millis();
    
    std::cout << "Starting simulation with video trace frame analysis..." << std::endl;
    Simulator::Run ();
    std::cout << "Simulation completed at: " << Simulator::Now().GetSeconds() << " seconds" << std::endl;
    
    // 强制完成所有帧
    for (int i=0;i<num;i++) {
        std::cout << "Completing all frames for session " << i+1 << std::endl;
        frame_managers[i]->CompleteAllFrames();
        std::cout << "Total packets processed by FrameManager " << i+1 << ": " 
                  << frame_managers[i]->GetTotalPackets() << std::endl;
        
        // 输出带宽历史记录（用于调试）
        std::string bw_history_file = prefix + std::to_string(i+1) + "_bandwidth_history.csv";
        frame_managers[i]->OutputBandwidthHistory(bw_history_file);
    }
    
    // 输出帧统计和带宽统计
    for (int i=0;i<num;i++) {
        std::string frame_stats_file = prefix + std::to_string(i+1) + "_mu=" + 
                                    std::to_string(bandwidth_scale_factor) + "_L=" + 
                                    std::to_string(loss_rate) + "_frame_statistics.csv";
        std::cout << "Outputting frame statistics to: " << frame_stats_file << std::endl;
        frame_managers[i]->OutputFrameStatistics(frame_stats_file);
        
        std::string bw_stats_file = prefix + std::to_string(i+1) + "_mu=" + 
                                std::to_string(bandwidth_scale_factor) + "_L=" + 
                                std::to_string(loss_rate) + "_bandwidth_statistics.csv";
        std::cout << "Outputting bandwidth statistics to: " << bw_stats_file << std::endl;
        trace_vec[i]->OutputBandwidthStatistics(bw_stats_file, loss_rate);
        
        // 输出RL状态记录
        std::cout << "Outputting RL state records for session " << i+1 << std::endl;
        rl_managers[i]->OutputStateRecords(prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
        
        // 新增：输出Rt分组奖励记录
        std::cout << "Outputting Rt group reward records for session " << i+1 << std::endl;
        rl_managers[i]->OutputRtGroupRewards(prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
    }
    
    Simulator::Destroy();
    std::cout << "Simulator destroyed" << std::endl;
    
    if(time_controller){
        delete time_controller;
        time_controller = nullptr;
        std::cout << "Time controller deleted" << std::endl;
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
        
        std::cout << "=== Utilization Calculation Debug ===" << std::endl;
        std::cout << "Last stamp: " << last_stamp << " ms" << std::endl;
        std::cout << "Start time: " << startapptime*1000 << " ms" << std::endl;
        std::cout << "Duration: " << (last_stamp - startapptime*1000) << " ms" << std::endl;
        std::cout << "Duration (seconds): " << (last_stamp - startapptime*1000)/1000.0 << " s" << std::endl;
        std::cout << "Bandwidth: " << bps << " bps" << std::endl;
        std::cout << "Theoretical capacity: " << channnel_bit << " bits" << std::endl;
        std::cout << "Bandwidth scale factor: μ=" << bandwidth_scale_factor << std::endl;
        std::cout << "Loss rate: L=" << loss_rate << std::endl;
        
        NS_LOG_INFO("channel byte "<<(uint32_t)channnel_bit/8);
        calculator->CalculateUtil(prefix,channnel_bit);
    }

    for(auto it=trace_vec.begin();it!=trace_vec.end();it++){
        FrameAwareWebrtcTrace *trace=(*it);
        delete trace;
    }
    trace_vec.clear();
    
    uint32_t elapse=( get_os_millis() - last);
    std::cout<<"run time millis: "<<elapse<<std::endl;
    
    std::cout << "=== test_app_on_p2p completed successfully ===" << std::endl;
    std::cout << "Bandwidth scaling experiment completed with μ=" << bandwidth_scale_factor << std::endl;
    std::cout << "RL state management completed with L=" << loss_rate << std::endl;
    std::cout << "Video trace analysis completed: " << (video_trace_file.empty() ? "disabled" : "enabled") << std::endl;
    std::cout << "Trace bandwidth recording completed: " << (changer ? "enabled" : "disabled") << std::endl;
    std::cout << "Trace RTT/loss usage completed: " << (changer ? "enabled" : "disabled") << std::endl;
    _exit(0);
}

// 支持视频trace文件的运行函数
void run_single_trace_simulation(const std::string& trace_file, const std::string& instance, 
                               TimeConollerType controller_type, int num, double max_bandwith,
                               double loss_rate, const std::string& base_output_folder = "Trace_Result",
                               double bandwidth_scale_factor=1.0, const std::string& video_trace_file = "")
{
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Starting simulation for: " << trace_file << std::endl;
    std::cout << "Instance: " << instance << std::endl;
    std::cout << "Max bandwidth: " << max_bandwith << " Mbps" << std::endl;
    std::cout << "Loss rate: " << loss_rate <<  " (THIS SHOULD BE 0.01, 0.02, etc.)" << std::endl;
    std::cout << "Bandwidth scale factor μ: " << bandwidth_scale_factor << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
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
        triggerloss->Start();
        std::cout << "TriggerRandomLoss started with rate: " << loss_rate << std::endl;
    }
    
    float startapptime = during_time.first;
    float endapptime = during_time.second;
    
    std::cout << "Normalized simulation time range: " << startapptime << "s to " << endapptime << "s" << std::endl;
    std::cout << "Total simulation duration: " << (endapptime - startapptime) << "s" << std::endl;
    
    // 运行仿真，传递视频trace文件参数
    test_app_on_p2p(instance, controller_type, num, startapptime, endapptime, 
                   max_bandwith, triggerloss.get(), changer.get(), trace_file, 
                   bandwidth_scale_factor, loss_rate, video_trace_file);
    
    std::cout << "Simulation completed successfully" << std::endl;
    
    if (triggerloss) {
        triggerloss.reset();
    }
    if (changer) {
        changer.reset();
    }
}

int main(int argc, char *argv[]){
    std::cout << "=== WebRTC TraceAll-Frame with Video Trace Analysis, Bandwidth Scaling and RL State Management Starting ===" << std::endl;
    
    // 启用详细日志
    LogComponentEnable("webrtc-static", LOG_LEVEL_ALL);
    LogComponentEnable("WebrtcSender", LOG_LEVEL_ALL); 
    LogComponentEnable("WebrtcReceiver", LOG_LEVEL_ALL);
    
    // 设置默认参数
    std::string mode("simu");
    std::string topo("change");
    std::string instance("default_instance");
    std::string trace_file(""); 
    std::string video_trace_file("");  // 新增参数：视频trace文件
    std::string max_bandwidth("20");
    std::string loss_rate("0.01");
    std::string folder("trace_results");
    std::string bandwidth_scale("1.0");
    
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("m", "mode", mode);
    cmd.AddValue("topo", "topology", topo);
    cmd.AddValue("it", "instance", instance);
    cmd.AddValue("trace", "trace file path", trace_file);
    cmd.AddValue("video_trace", "video trace file path", video_trace_file);  // 新增参数
    cmd.AddValue("mb", "max_bandwidth", max_bandwidth);
    cmd.AddValue("ls", "loss_rate", loss_rate);
    cmd.AddValue("folder", "folder name to collect data", folder);
    cmd.AddValue("mu", "bandwidth_scale_factor", bandwidth_scale);
    
    cmd.Parse(argc, argv);
    
    // 验证必要参数
    if (trace_file.empty()) {
        std::cerr << "ERROR: No trace file specified. Use --trace=<file_path>" << std::endl;
        std::cerr << "Usage: ./waf --run \"scratch/webrtc-TFMN(RTT) --trace=<path> [--video_trace=<video_trace_path> --it=<instance> --folder=<output_dir> --mb=<bandwidth> --ls=<loss_rate> --mu=<scale_factor>]\"" << std::endl;
        return 1;
    }
    
    // 检查trace文件是否存在
    std::ifstream test_file(trace_file);
    if (!test_file.good()) {
        std::cerr << "ERROR: Trace file does not exist or cannot be read: " << trace_file << std::endl;
        return 1;
    }
    test_file.close();
    
    // 检查视频trace文件是否存在（如果指定了的话）
    if (!video_trace_file.empty()) {
        std::ifstream video_test_file(video_trace_file);
        if (!video_test_file.good()) {
            std::cerr << "ERROR: Video trace file does not exist or cannot be read: " << video_trace_file << std::endl;
            return 1;
        }
        video_test_file.close();
    }
    
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
    try {
        mb = std::stod(max_bandwidth);
        ls = std::stod(loss_rate);
        mu = std::stod(bandwidth_scale);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Invalid parameter format: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Starting single trace simulation with video frame analysis..." << std::endl;
    std::cout << "Max bandwidth: " << mb << " Mbps" << std::endl;
    std::cout << "Loss rate: " << ls << std::endl;
    std::cout << "Bandwidth scale factor μ: " << mu << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "not specified" : video_trace_file) << std::endl;
    
    // 使用run_single_trace_simulation函数，传递video_trace_file参数
    run_single_trace_simulation(trace_file, instance, controller_type, 1, mb, ls, folder, mu, video_trace_file);
    
    std::cout << "=== WebRTC TraceAll-Frame with Video Trace Analysis Completed Successfully ===" << std::endl;
    _exit(0);
    return 0;
}