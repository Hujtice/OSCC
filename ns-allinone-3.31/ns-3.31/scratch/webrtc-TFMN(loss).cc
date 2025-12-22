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
#include <queue>
#include <set>
#include <mutex>
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
        
        // NS_LOG_DEBUG("Created frame " << frame_id << " with " << total_packets 
        //              << " packets, deadline: " << deadline.GetSeconds() << "s");
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

// 发送端丢包统计管理器
// 发送端丢包统计管理器 - 完整版本
class SenderLossStatistics {
public:
    SenderLossStatistics() : window_duration(Seconds(1.0)), sequence_number(0), total_sent(0), total_acked(0) {
        NS_LOG_INFO("SenderLossStatistics initialized with 1-second window");
        std::cout << "=== SenderLossStatistics Constructor ===" << std::endl;
        std::cout << "Initial window duration: " << window_duration.GetSeconds() << "s" << std::endl;
        std::cout << "Initial sequence number: " << sequence_number << std::endl;
        std::cout << "========================================" << std::endl;
    }
    
    ~SenderLossStatistics() {
        std::cout << "=== SenderLossStatistics Destructor ===" << std::endl;
        std::cout << "Total sent packets: " << total_sent << std::endl;
        std::cout << "Total ACKed packets: " << total_acked << std::endl;
        if (total_sent > 0) {
            std::cout << "Overall loss rate: " << static_cast<double>(total_sent - total_acked) / total_sent << std::endl;
        }
        std::cout << "======================================" << std::endl;
    }
    
    // 记录发送的包
    void RecordPacketSent(uint32_t seq_num, Time send_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 清理过期记录
        CleanOldRecordsNoLock(send_time);
        
        // 记录发送
        sent_packets_[seq_num] = send_time;
        total_sent++;
        
        NS_LOG_DEBUG("Recorded sent packet: seq=" << seq_num 
                     << ", time=" << send_time.GetSeconds() << "s"
                     << ", total_sent=" << total_sent);
        
        // 调试：定期输出状态
        DebugPrintStatusNoLock(send_time, "After RecordPacketSent");
    }
    
    // 生成新的序列号并记录发送
    uint32_t GenerateAndRecordPacketSent(Time send_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        uint32_t seq_num = ++sequence_number;
        CleanOldRecordsNoLock(send_time);
        sent_packets_[seq_num] = send_time;
        total_sent++;
        
        NS_LOG_DEBUG("Generated and recorded packet: seq=" << seq_num 
                     << ", time=" << send_time.GetSeconds() << "s"
                     << ", total_sent=" << total_sent);
        
        DebugPrintStatusNoLock(send_time, "After GenerateAndRecordPacketSent");
        
        return seq_num;
    }
    
    // 记录接收到的ACK
    void RecordPacketAcked(uint32_t seq_num, Time ack_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查这个包是否被发送过
        auto sent_it = sent_packets_.find(seq_num);
        if (sent_it == sent_packets_.end()) {
            NS_LOG_WARN("Received ACK for unknown packet: seq=" << seq_num 
                       << " at time " << ack_time.GetSeconds() << "s");
            // 仍然记录ACK，但标记为警告
            acked_packets_.insert(seq_num);
            total_acked++;
            DebugPrintStatusNoLock(ack_time, "After RecordPacketAcked (unknown packet)");
            return;
        }
        
        // 记录ACK
        acked_packets_.insert(seq_num);
        total_acked++;
        
        // 计算RTT（用于调试）
        Time rtt = ack_time - sent_it->second;
        NS_LOG_DEBUG("Recorded ACK for packet: seq=" << seq_num 
                     << ", send_time=" << sent_it->second.GetSeconds() << "s"
                     << ", ack_time=" << ack_time.GetSeconds() << "s"
                     << ", RTT=" << rtt.GetMilliSeconds() << "ms"
                     << ", total_acked=" << total_acked);
        
        // 如果是重传包，记录为重传成功
        if (retransmitted_packets_.find(seq_num) != retransmitted_packets_.end()) {
            retransmission_success_.insert(seq_num);
            NS_LOG_DEBUG("Retransmission succeeded for packet: seq=" << seq_num);
        }
        
        DebugPrintStatusNoLock(ack_time, "After RecordPacketAcked");
    }
    
    // 记录重传包
    void RecordPacketRetransmitted(uint32_t seq_num, Time retransmit_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        retransmitted_packets_.insert(seq_num);
        NS_LOG_DEBUG("Recorded retransmission: seq=" << seq_num 
                     << ", time=" << retransmit_time.GetSeconds() << "s");
        
        DebugPrintStatusNoLock(retransmit_time, "After RecordPacketRetransmitted");
    }
    
    // 计算当前丢包率（基于滑动窗口）
    double CalculateCurrentLossRate(Time current_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 清理过期记录
        CleanOldRecordsNoLock(current_time);
        
        // 统计窗口内的发送和ACK
        uint32_t sent_in_window = 0;
        uint32_t acked_in_window = 0;
        
        for (const auto& entry : sent_packets_) {
            if ((current_time - entry.second) <= window_duration) {
                sent_in_window++;
                if (acked_packets_.find(entry.first) != acked_packets_.end()) {
                    acked_in_window++;
                }
            }
        }
        
        // 调试输出
        DebugPrintStatusNoLock(current_time, "In CalculateCurrentLossRate");
        
        if (sent_in_window == 0) {
            NS_LOG_DEBUG("No packets sent in window, loss rate = 0");
            return 0.0;
        }
        
        double loss_rate = static_cast<double>(sent_in_window - acked_in_window) / sent_in_window;
        
        NS_LOG_INFO("Loss rate calculation at " << current_time.GetSeconds() << "s: "
                   << "sent=" << sent_in_window 
                   << ", acked=" << acked_in_window 
                   << ", loss_rate=" << loss_rate);
        
        return loss_rate;
    }
    
    // 计算特定时间段的丢包率
    double CalculateLossRateInInterval(Time start_time, Time end_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        uint32_t sent_in_interval = 0;
        uint32_t acked_in_interval = 0;
        
        for (const auto& entry : sent_packets_) {
            if (entry.second >= start_time && entry.second <= end_time) {
                sent_in_interval++;
                if (acked_packets_.find(entry.first) != acked_packets_.end()) {
                    acked_in_interval++;
                }
            }
        }
        
        NS_LOG_INFO("Interval loss rate [" << start_time.GetSeconds() << "s - " 
                   << end_time.GetSeconds() << "s]: sent=" << sent_in_interval 
                   << ", acked=" << acked_in_interval);
        
        if (sent_in_interval == 0) {
            NS_LOG_DEBUG("No packets sent in interval, loss rate = 0");
            return 0.0;
        }
        
        double loss_rate = static_cast<double>(sent_in_interval - acked_in_interval) / sent_in_interval;
        
        return loss_rate;
    }
    
    // 获取重传率
    double CalculateRetransmissionRate(Time current_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        CleanOldRecordsNoLock(current_time);
        
        uint32_t total_retransmitted = 0;
        uint32_t total_sent_in_window = 0;
        
        Time window_start = current_time - window_duration;
        for (const auto& entry : sent_packets_) {
            if (entry.second >= window_start) {
                total_sent_in_window++;
                if (retransmitted_packets_.find(entry.first) != retransmitted_packets_.end()) {
                    total_retransmitted++;
                }
            }
        }
        
        if (total_sent_in_window == 0) {
            return 0.0;
        }
        
        return static_cast<double>(total_retransmitted) / total_sent_in_window;
    }
    
    // 设置滑动窗口时长
    void SetWindowDuration(Time duration) {
        std::unique_lock<std::mutex> lock(mutex_);
        window_duration = duration;
        NS_LOG_INFO("Loss statistics window duration set to " << duration.GetSeconds() << "s");
        std::cout << "Window duration changed to: " << duration.GetSeconds() << "s" << std::endl;
    }
    
    // 获取发送的包数量（用于调试）
    size_t GetSentPacketCount() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return sent_packets_.size();
    }
    
    // 获取ACK的包数量（用于调试）
    size_t GetAckedPacketCount() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return acked_packets_.size();
    }
    
    // 获取总发送数
    uint32_t GetTotalSent() const {
        return total_sent;
    }
    
    // 获取总ACK数
    uint32_t GetTotalAcked() const {
        return total_acked;
    }
    
    // 获取总体丢包率
    double GetOverallLossRate() const {
        if (total_sent == 0) return 0.0;
        return static_cast<double>(total_sent - total_acked) / total_sent;
    }
    
    // 调试方法：打印当前状态
    void DebugPrintStatus(Time current_time, const std::string& context = "") {
        std::unique_lock<std::mutex> lock(mutex_);
        DebugPrintStatusNoLock(current_time, context);
    }
    
    // 重置统计（用于测试）
    void Reset() {
        std::unique_lock<std::mutex> lock(mutex_);
        sent_packets_.clear();
        acked_packets_.clear();
        retransmitted_packets_.clear();
        retransmission_success_.clear();
        sequence_number = 0;
        total_sent = 0;
        total_acked = 0;
        NS_LOG_INFO("SenderLossStatistics reset to initial state");
    }
    
    // 输出详细统计信息到文件
    void OutputDetailedStatistics(const std::string& filename, Time current_time) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open statistics file: " << filename);
            return;
        }
        
        std::unique_lock<std::mutex> lock(mutex_);
        
        file << "=== SenderLossStatistics Detailed Report ===" << std::endl;
        file << "Report time: " << current_time.GetSeconds() << "s" << std::endl;
        file << "Window duration: " << window_duration.GetSeconds() << "s" << std::endl;
        file << "Total sent packets: " << total_sent << std::endl;
        file << "Total ACKed packets: " << total_acked << std::endl;
        file << "Overall loss rate: " << GetOverallLossRate() << std::endl;
        file << std::endl;
        
        // 当前窗口统计
        Time window_start = current_time - window_duration;
        uint32_t sent_in_window = 0;
        uint32_t acked_in_window = 0;
        
        for (const auto& entry : sent_packets_) {
            if (entry.second >= window_start) {
                sent_in_window++;
                if (acked_packets_.find(entry.first) != acked_packets_.end()) {
                    acked_in_window++;
                }
            }
        }
        
        file << "Current window [" << window_start.GetSeconds() << "s - " 
             << current_time.GetSeconds() << "s]:" << std::endl;
        file << "  Sent: " << sent_in_window << std::endl;
        file << "  ACKed: " << acked_in_window << std::endl;
        if (sent_in_window > 0) {
            file << "  Loss rate: " << static_cast<double>(sent_in_window - acked_in_window) / sent_in_window << std::endl;
        }
        file << std::endl;
        
        // 最近的包信息（最多100个）
        file << "Recent packets (up to 100):" << std::endl;
        file << "Seq,SendTime,Acked,RTT(ms)" << std::endl;
        
        int count = 0;
        for (const auto& entry : sent_packets_) {
            if (entry.second >= window_start && count++ < 100) {
                bool acked = (acked_packets_.find(entry.first) != acked_packets_.end());
                file << entry.first << "," 
                     << entry.second.GetSeconds() << ","
                     << (acked ? "YES" : "NO") << ",";
                
                if (acked) {
                    // 需要计算RTT，这里简化处理
                    file << "N/A";
                } else {
                    file << "N/A";
                }
                file << std::endl;
            }
        }
        
        file.close();
        NS_LOG_INFO("Detailed statistics saved to: " << filename);
    }
    
private:
    // 清理过期记录（内部使用，不锁定）
    void CleanOldRecordsNoLock(Time current_time) {
        Time threshold = current_time - window_duration;
        
        // 收集要删除的键
        std::vector<uint32_t> keys_to_erase;
        
        for (const auto& entry : sent_packets_) {
            if (entry.second < threshold) {
                keys_to_erase.push_back(entry.first);
            }
        }
        
        // 删除过期记录
        for (uint32_t key : keys_to_erase) {
            sent_packets_.erase(key);
            acked_packets_.erase(key);
            retransmitted_packets_.erase(key);
            retransmission_success_.erase(key);
        }
        
        if (!keys_to_erase.empty()) {
            NS_LOG_DEBUG("Cleaned " << keys_to_erase.size() << " old records at time " 
                         << current_time.GetSeconds() << "s");
        }
    }
    
    // 内部调试打印（不锁定，由调用者确保锁）
    void DebugPrintStatusNoLock(Time current_time, const std::string& context) {
        static Time last_debug_time = Seconds(-100);
        
        // 每0.5秒打印一次，避免日志过多
        if (current_time - last_debug_time < Seconds(0.5) && !context.empty()) {
            return;
        }
        
        last_debug_time = current_time;
        
        // 统计窗口内的情况
        Time window_start = current_time - window_duration;
        uint32_t sent_in_window = 0;
        uint32_t acked_in_window = 0;
        
        for (const auto& entry : sent_packets_) {
            if (entry.second >= window_start) {
                sent_in_window++;
                if (acked_packets_.find(entry.first) != acked_packets_.end()) {
                    acked_in_window++;
                }
            }
        }
        
        NS_LOG_INFO("=== SenderLossStatistics Debug [" << context << "] ===");
        NS_LOG_INFO("Time: " << current_time.GetSeconds() << "s");
        NS_LOG_INFO("Window: [" << window_start.GetSeconds() << "s - " 
                   << current_time.GetSeconds() << "s] (" 
                   << window_duration.GetSeconds() << "s)");
        NS_LOG_INFO("Total sent (all time): " << total_sent);
        NS_LOG_INFO("Total ACKed (all time): " << total_acked);
        NS_LOG_INFO("Active sent packets: " << sent_packets_.size());
        NS_LOG_INFO("Active ACKed packets: " << acked_packets_.size());
        NS_LOG_INFO("Sent in window: " << sent_in_window);
        NS_LOG_INFO("ACKed in window: " << acked_in_window);
        
        if (sent_in_window > 0) {
            double window_loss_rate = static_cast<double>(sent_in_window - acked_in_window) / sent_in_window;
            NS_LOG_INFO("Window loss rate: " << window_loss_rate);
        }
        
        if (total_sent > 0) {
            double overall_loss_rate = static_cast<double>(total_sent - total_acked) / total_sent;
            NS_LOG_INFO("Overall loss rate: " << overall_loss_rate);
        }
        
        NS_LOG_INFO("======================================");
        
        // 在控制台也输出关键信息
        if (context.find("CalculateCurrentLossRate") != std::string::npos) {
            std::cout << "[LossStats] Time=" << current_time.GetSeconds() << "s, "
                     << "Window=[" << window_start.GetSeconds() << "s-" << current_time.GetSeconds() << "s], "
                     << "Sent=" << sent_in_window << ", ACKed=" << acked_in_window;
            
            if (sent_in_window > 0) {
                double loss_rate = static_cast<double>(sent_in_window - acked_in_window) / sent_in_window;
                std::cout << ", LossRate=" << loss_rate << std::endl;
            } else {
                std::cout << ", LossRate=N/A (no packets)" << std::endl;
            }
        }
    }
    
    mutable std::mutex mutex_;
    Time window_duration;
    std::map<uint32_t, Time> sent_packets_;          // 发送的包：序列号->发送时间
    std::set<uint32_t> acked_packets_;              // 已确认的包
    std::set<uint32_t> retransmitted_packets_;      // 重传的包
    std::set<uint32_t> retransmission_success_;     // 重传成功的包
    uint32_t sequence_number;                       // 序列号生成器
    uint32_t total_sent;                            // 总发送数
    uint32_t total_acked;                           // 总ACK数
};


struct RtGroupRewardRecord {
    uint32_t frame_id;
    uint32_t Rt_value;
    double loss_rate;          // 组内实时丢包率
    double mu_used;
    double avg_reward;
    double group_delay;        // 组延迟：最后一个包到达时间 - 第一个包发送时间
    uint32_t packet_count;
    Time group_start_time;     // 组内第一个包发送时间
    Time group_end_time;       // 组内最后一个包到达时间
    double bandwidth_utilization;
    double p_delay_value;
    double p_loss_value;
    double p_mddl_value;
    
    RtGroupRewardRecord() : frame_id(0), Rt_value(0), loss_rate(0.0), 
                           mu_used(1.0), avg_reward(0.0), group_delay(0.0),
                           packet_count(0), group_start_time(Seconds(0)), 
                           group_end_time(Seconds(0)), bandwidth_utilization(0.0),
                           p_delay_value(0.0), p_loss_value(0.0), p_mddl_value(0.0) {}
    
    RtGroupRewardRecord(uint32_t fid, uint32_t rt, double loss, double mu, 
                       double reward, double delay, uint32_t count, 
                       Time start, Time end, double bw_util, double p_delay,
                       double p_loss, double p_mddl)
        : frame_id(fid), Rt_value(rt), loss_rate(loss), mu_used(mu),
          avg_reward(reward), group_delay(delay), packet_count(count), 
          group_start_time(start), group_end_time(end), 
          bandwidth_utilization(bw_util), p_delay_value(p_delay),
          p_loss_value(p_loss), p_mddl_value(p_mddl) {}
};

// Trace数据结构，包含四列数据
struct TraceData {
    Time timestamp;
    double bandwidth;  // 带宽数据
    double rtt;        // RTT数据（毫秒）
    double loss;       // loss数据
    
    TraceData() : timestamp(Seconds(0)), bandwidth(0.0), rtt(0.0), loss(0.0) {}
    
    TraceData(Time ts, double bw, double rt, double l) 
        : timestamp(ts), bandwidth(bw), rtt(rt), loss(l) {}
};

// 强化学习状态管理器类 - 完整版本
class RLStateManager {
public:
    RLStateManager() : current_mu(1.0), max_loss_rate(0.05), 
                      current_loss_rate(0.01), last_packet_Rt(1), current_rtt(MilliSeconds(30)),
                      current_delay(20.0), loss_statistics_(nullptr),
                      total_packets_processed(0), use_hybrid_loss_(true),
                      hybrid_weight_(0.3) {
        std::cout << "=== RLStateManager Constructor ===" << std::endl;
        std::cout << "Default values:" << std::endl;
        std::cout << "  current_mu: " << current_mu << std::endl;
        std::cout << "  max_loss_rate: " << max_loss_rate << std::endl;
        std::cout << "  current_loss_rate: " << current_loss_rate << std::endl;
        std::cout << "  hybrid_loss_enabled: " << use_hybrid_loss_ << std::endl;
        std::cout << "  hybrid_weight: " << hybrid_weight_ << std::endl;
        std::cout << "==================================" << std::endl;
        NS_LOG_INFO("RLStateManager initialized with default mu=1.0, Lmax=0.05");
    }
    
    // 设置发送端丢包统计器
    void SetSenderLossStatistics(SenderLossStatistics* stats) {
        loss_statistics_ = stats;
        NS_LOG_INFO("SenderLossStatistics set in RLStateManager");
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
    
    // 计算单个包的Reward
    double CalculatePacketReward(double mu_prev, double gcc_bandwidth_bps, double trace_bandwidth_bps,
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
        
        // // Rt越小，对延迟越敏感
        // double delay_sensitivity = 1.0 + (1.0 - Rt_used / 10.0) * 0.3;
        // p_delay *= delay_sensitivity;
        

        

        // (3) 丢包率惩罚 - 使用混合丢包率
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



                // (1) 带宽利用率 U
        double U =(1-Ltol)*(mu_prev * gcc_bandwidth_bps) / trace_bandwidth_bps;


        double U_eff = (mu_prev * gcc_bandwidth_bps/ fmax(trace_bandwidth_bps, 1e-9)) * (1.0 - Ltol) * (current_delay_ms <= 33);
        U = std::min(std::max(U, 0.0), 1.0);
        
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


        //(5)激进因子
        double P_mu = pow(mu_prev / fmax(Rt_prev, 1.0), 2.0) * fmax(1.0, Ltol);



        // double U_weight = 4;
        // double delay_weight = 10/(1 + 0.3 * Rt_prev);
        // double loss_weight = 8;
        // double mddl_weight = 10;

        double U_weight = 1;
        double delay_weight = 0.5;
        double loss_weight = 1;
        double mddl_weight = 1;
        double mu_weight = 1;

        double reward = U_weight * U 
                      - delay_weight * p_delay 
                      - loss_weight * p_loss 
                      - mddl_weight * p_mddl
                      - mu_weight * P_mu;

        NS_LOG_DEBUG("Packet Reward - Rt_used: " << Rt_used << ", mu_prev: " << mu_prev << ", U: " << U);
        NS_LOG_DEBUG("p_delay: " << p_delay << ", p_loss: " << p_loss << ", p_mddl: " << p_mddl);
        NS_LOG_DEBUG("Reward: " << reward);
        
        return reward;
    }
    
    // 记录包状态
    void RecordPacketState(uint32_t frame_id, uint32_t packet_index, double mu_used,
                          uint32_t Rt, double loss_rate, double reward,
                          Time send_time, Time recivied_time, Time deadline,
                          double bandwidth_utilization, double p_delay, 
                          double p_loss, double p_mddl, double current_delay_ms) {
        total_packets_processed++;
        
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
        
        packet_records.push_back(record);
        last_packet_Rt = Rt;
        
        // 将包记录添加到Rt分组管理器中
        AddPacketToRtGroup(frame_id, packet_index, Rt, loss_rate, reward, send_time, 
                          recivied_time, mu_used, bandwidth_utilization, p_delay, p_loss, p_mddl);
        
        NS_LOG_INFO("Recorded packet state: frame=" << frame_id << ", packet=" << packet_index
                   << ", mu=" << mu_used << ", Rt=" << Rt << ", loss_rate=" << loss_rate 
                   << ", reward=" << reward << ", send_time=" << send_time.GetSeconds() 
                   << "s, recv_time=" << recivied_time.GetSeconds() 
                   << "s, delay=" << current_delay_ms << "ms"
                   << ", total_packets=" << total_packets_processed);
    }
    
    // 将包添加到Rt分组管理器
    void AddPacketToRtGroup(uint32_t frame_id, uint32_t packet_index, uint32_t Rt, 
                           double loss_rate, double reward, Time send_time, Time recv_time,
                           double mu_used, double bw_util, double p_delay, double p_loss, double p_mddl) {
        // 如果这是该帧的第一个包，或者Rt值发生了变化，结束前一个Rt组
        if (current_rt_group.frame_id != frame_id || current_rt_group.Rt_value != Rt) {
            // 如果前一个Rt组有数据，计算组Reward并保存
            if (current_rt_group.packet_count > 0) {
                FinalizeCurrentRtGroup();
            }
            
            // 开始新的Rt组
            current_rt_group.frame_id = frame_id;
            current_rt_group.Rt_value = Rt;
            current_rt_group.mu_used = mu_used;
            current_rt_group.packet_count = 0;
            current_rt_group.reward_sum = 0.0;
            current_rt_group.bw_util_sum = 0.0;
            current_rt_group.p_delay_sum = 0.0;
            current_rt_group.p_loss_sum = 0.0;
            current_rt_group.p_mddl_sum = 0.0;
            current_rt_group.group_start_time = send_time;
            current_rt_group.first_packet_send_time = send_time;
            current_rt_group.last_packet_recv_time = recv_time;
        }
        
        // 更新当前Rt组
        current_rt_group.packet_count++;
        current_rt_group.reward_sum += reward;
        current_rt_group.bw_util_sum += bw_util;
        current_rt_group.p_delay_sum += p_delay;
        current_rt_group.p_loss_sum += p_loss;
        current_rt_group.p_mddl_sum += p_mddl;
        
        // 更新组的时间范围
        if (send_time < current_rt_group.first_packet_send_time) {
            current_rt_group.first_packet_send_time = send_time;
        }
        if (recv_time > current_rt_group.last_packet_recv_time) {
            current_rt_group.last_packet_recv_time = recv_time;
        }
    }
    
    // 完成当前Rt组并保存
    void FinalizeCurrentRtGroup() {
        if (current_rt_group.packet_count > 0) {
            // 计算组延迟：最后一个包到达时间 - 第一个包发送时间
            double group_delay = (current_rt_group.last_packet_recv_time - 
                                 current_rt_group.first_packet_send_time).GetSeconds() * 1000.0; // 转换为毫秒
            
            // 计算组内实时丢包率（使用发送端统计）
            double group_loss_rate = 0.0;
            if (loss_statistics_) {
                group_loss_rate = loss_statistics_->CalculateLossRateInInterval(
                    current_rt_group.first_packet_send_time,
                    current_rt_group.last_packet_recv_time);
                
                // 如果统计的丢包率是0，使用混合方法
                if (use_hybrid_loss_ && group_loss_rate < 0.001) {
                    Time mid_time = current_rt_group.first_packet_send_time + 
                                   (current_rt_group.last_packet_recv_time - 
                                    current_rt_group.first_packet_send_time) / 2.0;
                    
                    // 获取trace的loss率
                    double trace_loss = GetTraceLossRateAtTime(mid_time);
                    
                    // 使用加权混合
                    double weight = CalculateHybridWeight();
                    group_loss_rate = weight * group_loss_rate + (1 - weight) * trace_loss;
                    
                    NS_LOG_DEBUG("Using hybrid loss rate for Rt group: "
                               << "statistical=" << group_loss_rate 
                               << ", trace=" << trace_loss 
                               << ", weight=" << weight
                               << ", final=" << group_loss_rate);
                }
                
                NS_LOG_INFO("Calculated group loss rate: " << group_loss_rate 
                           << " from " << current_rt_group.first_packet_send_time.GetSeconds() 
                           << "s to " << current_rt_group.last_packet_recv_time.GetSeconds() << "s");
            } else {
                NS_LOG_WARN("No loss statistics available for Rt group");
            }
            
            // 计算平均奖励和各项指标
            double avg_reward = current_rt_group.reward_sum / current_rt_group.packet_count;
            double avg_bw_util = current_rt_group.bw_util_sum / current_rt_group.packet_count;
            double avg_p_delay = current_rt_group.p_delay_sum / current_rt_group.packet_count;
            double avg_p_loss = current_rt_group.p_loss_sum / current_rt_group.packet_count;
            double avg_p_mddl = current_rt_group.p_mddl_sum / current_rt_group.packet_count;
            
            RtGroupRewardRecord record(
                current_rt_group.frame_id,
                current_rt_group.Rt_value,
                group_loss_rate,           // 使用混合丢包率
                current_rt_group.mu_used,
                avg_reward,
                group_delay,
                current_rt_group.packet_count,
                current_rt_group.group_start_time,
                current_rt_group.last_packet_recv_time,
                avg_bw_util,
                avg_p_delay,
                avg_p_loss,
                avg_p_mddl
            );
            
            rt_group_records.push_back(record);
            
            NS_LOG_INFO("Finalized Rt group: frame=" << record.frame_id 
                       << ", Rt=" << record.Rt_value 
                       << ", mu_used=" << record.mu_used
                       << ", packets=" << record.packet_count
                       << ", group_delay=" << record.group_delay << "ms"
                       << ", group_loss_rate=" << record.loss_rate
                       << ", avg_reward=" << record.avg_reward);
        }
        
        // 重置当前Rt组
        current_rt_group.frame_id = 0;
        current_rt_group.Rt_value = 0;
        current_rt_group.mu_used = 1.0;
        current_rt_group.packet_count = 0;
        current_rt_group.reward_sum = 0.0;
        current_rt_group.bw_util_sum = 0.0;
        current_rt_group.p_delay_sum = 0.0;
        current_rt_group.p_loss_sum = 0.0;
        current_rt_group.p_mddl_sum = 0.0;
        current_rt_group.group_start_time = Seconds(0);
        current_rt_group.first_packet_send_time = Seconds(0);
        current_rt_group.last_packet_recv_time = Seconds(0);
    }
    
    // 获取所有Rt组记录
    const std::vector<RtGroupRewardRecord>& GetRtGroupRecords() const {
        return rt_group_records;
    }
    
    // 输出状态记录到文件
    void OutputStateRecords(const std::string& filename_prefix, double initial_mu, double initial_loss_rate) {
        std::string filename = filename_prefix + "_mu=" + std::to_string(initial_mu) + 
                              "_L=" + std::to_string(initial_loss_rate) + "_RL_log.csv";
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open RL log file: " << filename);
            return;
        }
        
        file << "# RL State Records - Total packets: " << total_packets_processed << std::endl;
        file << "# Initial μ: " << initial_mu << ", Initial loss rate: " << initial_loss_rate << std::endl;
        file << "# Hybrid loss enabled: " << use_hybrid_loss_ << ", Hybrid weight: " << hybrid_weight_ << std::endl;
        file << "frame_id,packet_index,mu_used,Rt,loss_rate,reward,send_time,recivied_time,deadline,"
             << "bandwidth_utilization,p_delay,p_loss,p_mddl,current_delay" << std::endl;
        
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
                 << record.current_delay << std::endl;
        }
        
        file.close();
        
        NS_LOG_INFO("RL state records saved to: " << filename << " with " << packet_records.size() << " records");
        std::cout << "[RLStateManager] State records saved: " << filename 
                  << " (" << packet_records.size() << " records)" << std::endl;
    }
    
    // 输出Rt组奖励记录到文件
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
        
        // 写入头部信息
        file << "# Rt Group Reward Records - Total groups: " << rt_group_records.size() << std::endl;
        file << "# Initial μ: " << initial_mu << ", Initial loss rate: " << initial_loss_rate << std::endl;
        file << "# Hybrid loss enabled: " << use_hybrid_loss_ << std::endl;
        
        // 写入表头 - 包含所有组级指标
        file << "frame_id,Rt_value,loss_rate,mu_used,avg_reward,group_delay_ms,packet_count,"
             << "group_start_time,group_end_time,bandwidth_utilization,p_delay,p_loss,p_mddl" << std::endl;
        
        for (const auto& record : rt_group_records) {
            file << record.frame_id << ","
                 << record.Rt_value << ","
                 << record.loss_rate << ","
                 << record.mu_used << ","
                 << record.avg_reward << ","
                 << record.group_delay << ","
                 << record.packet_count << ","
                 << record.group_start_time.GetSeconds() << ","
                 << record.group_end_time.GetSeconds() << ","
                 << record.bandwidth_utilization << ","
                 << record.p_delay_value << ","
                 << record.p_loss_value << ","
                 << record.p_mddl_value << std::endl;
        }
        
        file.close();
        
        NS_LOG_INFO("Rt group reward records saved to: " << filename << " with " 
                   << rt_group_records.size() << " Rt groups");
        std::cout << "[RLStateManager] Rt group records saved: " << filename 
                  << " (" << rt_group_records.size() << " groups)" << std::endl;
    }
    
    // 更新当前mu值
    void SetCurrentMu(double mu) {
        double old_mu = current_mu;
        current_mu = mu;
        NS_LOG_INFO("RLStateManager μ updated: " << old_mu << " -> " << current_mu);
    }
    
    double GetCurrentMu() const {
        return current_mu;
    }
    
    // 更新网络状态 - 使用混合丢包率
    void UpdateNetworkState(double delay_ms, double loss_rate, Time rtt) {
        double old_delay = current_delay;
        double old_loss = current_loss_rate;
        
        current_delay = delay_ms;
        current_loss_rate = loss_rate;
        current_rtt = rtt;
        
        NS_LOG_DEBUG("Network state updated: delay=" << old_delay << "ms->" << delay_ms << "ms, "
                   << "loss_rate=" << old_loss << "->" << loss_rate 
                   << ", RTT=" << rtt.GetMilliSeconds() << "ms");
    }
    
    // 更新实时丢包率（从发送端统计获取，使用混合方法）
    void UpdateRealTimeLossRate(Time current_time) {
        if (loss_statistics_) {
            double statistical_loss = loss_statistics_->CalculateCurrentLossRate(current_time);
            
            // 获取trace的loss率
            double trace_loss = GetTraceLossRateAtTime(current_time);
            
            // 使用混合丢包率
            double hybrid_loss = CalculateHybridLossRate(statistical_loss, trace_loss, current_time);
            
            current_loss_rate = hybrid_loss;
            
            NS_LOG_DEBUG("Updated real-time loss rate: statistical=" << statistical_loss
                       << ", trace=" << trace_loss
                       << ", hybrid=" << hybrid_loss
                       << " at time " << current_time.GetSeconds() << "s");
        } else {
            NS_LOG_WARN("No loss statistics available for real-time loss rate update");
        }
    }
    
    // 获取混合丢包率（主要方法）
    double CalculateHybridLossRate(double statistical_loss, double trace_loss, Time current_time) {
        if (!use_hybrid_loss_) {
            return statistical_loss;
        }
        
        // 计算混合权重
        double weight = CalculateHybridWeight();
        
        // 如果统计loss为0但trace loss较高，进行混合
        if (statistical_loss < 0.001 && trace_loss > 0.01) {
            double hybrid_loss = weight * statistical_loss + (1 - weight) * trace_loss;
            
            // 确保不会低于trace的loss率太多
            hybrid_loss = std::max(hybrid_loss, trace_loss * 0.5);
            
            NS_LOG_DEBUG("Hybrid loss calculation: statistical=" << statistical_loss
                       << ", trace=" << trace_loss
                       << ", weight=" << weight
                       << ", hybrid=" << hybrid_loss);
            
            return hybrid_loss;
        }
        
        // 正常情况：统计loss可信
        return statistical_loss;
    }
    
    // 计算混合权重
    double CalculateHybridWeight() {
        if (!loss_statistics_) {
            return 0.0; // 完全信任trace
        }
        
        // 基于样本数量计算权重
        size_t sent_count = loss_statistics_->GetSentPacketCount();
        
        // 权重公式：随着样本增加，逐渐信任统计值
        // 当样本数达到200时，权重达到0.8
        double weight = std::min(0.8, sent_count / 250.0);
        
        // 确保至少有一些权重给trace
        weight = std::max(weight, 0.2);
        
        return weight;
    }
    
    // 从trace获取loss率
    double GetTraceLossRateAtTime(Time time) {
        // 默认值
        double trace_loss = 0.01;
        
        // 这里需要访问BandwidthChanger或FrameManager来获取trace数据
        // 由于这是静态方法，我们将在外部调用时传递trace_loss
        // 或者，可以存储一个指向BandwidthChanger的指针
        
        return trace_loss;
    }
    
    // 获取最后一个包的传输机会
    uint32_t GetLastPacketRt() const {
        return last_packet_Rt;
    }
    
    // 设置当前 loss_rate
    void SetCurrentLossRate(double loss_rate) {
        double old_loss = current_loss_rate;
        current_loss_rate = loss_rate;
        NS_LOG_INFO("RLStateManager current_loss_rate updated: " << old_loss << " -> " << current_loss_rate);
    }
    
    // 获取当前 loss_rate（可能是混合的）
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
    
    // 获取当前RTT
    Time GetCurrentRTT() const {
        return current_rtt;
    }
    
    // 获取发送端丢包统计器
    SenderLossStatistics* GetSenderLossStatistics() const {
        return loss_statistics_;
    }
    
    // 启用/禁用混合丢包率
    void SetHybridLossEnabled(bool enabled) {
        use_hybrid_loss_ = enabled;
        NS_LOG_INFO("Hybrid loss calculation " << (enabled ? "enabled" : "disabled"));
    }
    
    // 设置混合权重
    void SetHybridWeight(double weight) {
        hybrid_weight_ = std::max(0.0, std::min(1.0, weight));
        NS_LOG_INFO("Hybrid weight set to: " << hybrid_weight_);
    }
    
    // 获取处理的总包数
    uint32_t GetTotalPacketsProcessed() const {
        return total_packets_processed;
    }
    
    // 获取包记录数量
    size_t GetPacketRecordCount() const {
        return packet_records.size();
    }
    
    // 获取Rt组记录数量
    size_t GetRtGroupRecordCount() const {
        return rt_group_records.size();
    }
    
    // 输出统计摘要
    void PrintStatisticsSummary() const {
        std::cout << "\n=== RLStateManager Statistics Summary ===" << std::endl;
        std::cout << "Total packets processed: " << total_packets_processed << std::endl;
        std::cout << "Packet records: " << packet_records.size() << std::endl;
        std::cout << "Rt group records: " << rt_group_records.size() << std::endl;
        std::cout << "Current μ: " << current_mu << std::endl;
        std::cout << "Current loss rate: " << current_loss_rate << std::endl;
        std::cout << "Max loss rate (Lmax): " << max_loss_rate << std::endl;
        std::cout << "Current RTT: " << current_rtt.GetMilliSeconds() << "ms" << std::endl;
        std::cout << "Hybrid loss enabled: " << use_hybrid_loss_ << std::endl;
        
        if (loss_statistics_) {
            std::cout << "Sender statistics:" << std::endl;
            std::cout << "  Total sent: " << loss_statistics_->GetTotalSent() << std::endl;
            std::cout << "  Total ACKed: " << loss_statistics_->GetTotalAcked() << std::endl;
            std::cout << "  Overall loss rate: " << loss_statistics_->GetOverallLossRate() << std::endl;
        }
        
        std::cout << "=======================================\n" << std::endl;
    }
    
private:
    double current_mu;
    double max_loss_rate;
    double current_loss_rate;
    uint32_t last_packet_Rt;
    Time current_rtt;
    double current_delay;
    std::vector<PacketStateRecord> packet_records;
    SenderLossStatistics* loss_statistics_;
    
    // 混合丢包率参数
    uint32_t total_packets_processed;
    bool use_hybrid_loss_;
    double hybrid_weight_;
    
    // Rt分组管理
    struct RtGroup {
        uint32_t frame_id;
        uint32_t Rt_value;
        double mu_used;
        uint32_t packet_count;
        double reward_sum;
        double bw_util_sum;
        double p_delay_sum;
        double p_loss_sum;
        double p_mddl_sum;
        Time group_start_time;
        Time first_packet_send_time;
        Time last_packet_recv_time;
        
        RtGroup() : frame_id(0), Rt_value(0), mu_used(1.0),
                   packet_count(0), reward_sum(0.0), bw_util_sum(0.0),
                   p_delay_sum(0.0), p_loss_sum(0.0), p_mddl_sum(0.0),
                   group_start_time(Seconds(0)), first_packet_send_time(Seconds(0)),
                   last_packet_recv_time(Seconds(0)) {}
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
    
    // 根据时间戳获取trace带宽
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
        
        return static_cast<uint32_t>(trace_bw);
    }
    
    // 根据时间获取RTT值
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
        
        return rtt;
    }
    
    // 根据时间获取Loss值
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
        
        return loss;
    }
    
    // 根据时间获取TraceData
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
        
        return *closest_data;
    }
    
private:
    int64_t m_initialRate=0;
    std::vector<std::pair<Time,int64_t> > m_info;
    std::vector<TraceData> m_trace_data;  // 存储完整的trace数据
    uint32_t m_index{0};
    Ptr<NetDevice> m_dev;
    EventId m_timer;
    double current_trace_bandwidth_bps_;
};

// FrameManager 类定义
class FrameManager {
public:
    // 带宽记录结构体
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
    
    // 帧统计数据结构
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
          current_frame_id(0), 
          last_frame_complete_time(Seconds(0)), 
          packet_counter(0), 
          trace_manager(trace_manager), 
          rl_manager_(rl_manager),
          m_bandwidth_changer(nullptr) {
        
        if (trace_manager && trace_manager->IsLoaded()) {
            NS_LOG_INFO("FrameManager initialized with video trace, total frames: " 
                       << trace_manager->GetTotalFrames());
        } else {
            NS_LOG_WARN("FrameManager initialized without video trace!");
        }
    }
    
    // 处理数据包到达
    void ProcessPacketArrival(Time arrival_time, uint32_t packet_size = 0, uint32_t seq_num = 0) {
        uint32_t packet_id = packet_counter++;
        
        // 记录序列号（用于调试）
        if (seq_num > 0) {
            packet_sequence_map_[seq_num] = packet_id;
            NS_LOG_DEBUG("Mapped seq " << seq_num << " to packet_id " << packet_id);
        }
        
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
        
        // 估计发送时间
        Time send_time = EstimateSendTime(arrival_time);
        
        // 触发RL状态记录
        if (rl_manager_ != nullptr) {
            TriggerRLStateForPacket(frame_id, packet_index_in_frame, send_time, arrival_time, seq_num);
        }
        
        // 检查帧是否完成
        if (frame.packets_received >= frame.total_packets && !frame.frame_completed) {
            CompleteFrame(frame_id);
        }
    }
    
    // 估计数据包的发送时间
    Time EstimateSendTime(Time arrival_time) {
        // 使用固定的网络延迟来估计发送时间
        Time network_delay = MilliSeconds(20.0);
        Time send_time = arrival_time - network_delay;
        
        if (send_time < Seconds(0)) {
            send_time = Seconds(0);
        }
        
        return send_time;
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
    
    // 强制完成所有未完成的帧
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
    }
    
    // 获取总包数
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
    
    // 获取帧统计摘要
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

    // 添加带宽记录
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
    
    // 根据时间获取带宽记录
    BandwidthRecord GetBandwidthAtTime(Time timestamp) const {
        if (bandwidth_history_.empty()) {
            double trace_bw = 0.0;
            double gcc_bw = 0.0;
            double scaled_bw = 0.0;
            double mu_value = 1.0;
            
            if (rl_manager_) {
                mu_value = rl_manager_->GetCurrentMu();
            }
            
            if (m_bandwidth_changer) {
                uint32_t timestamp_ms = static_cast<uint32_t>(timestamp.GetMilliSeconds());
                TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
                trace_bw = trace_data.bandwidth;
            }
            
            if (trace_bw <= 0.0) {
                trace_bw = current_trace_bandwidth_;
                if (trace_bw <= 0.0) {
                    trace_bw = 20 * 1000000.0;
                }
            }
            
            gcc_bw = current_gcc_bandwidth_;
            
            if (gcc_bw <= 0.0) {
                if (!bandwidth_history_.empty()) {
                    Time recent_threshold = timestamp - Seconds(1);
                    for (auto it = bandwidth_history_.rbegin(); it != bandwidth_history_.rend(); ++it) {
                        if (it->timestamp >= recent_threshold && it->gcc_bandwidth > 0) {
                            gcc_bw = it->gcc_bandwidth;
                            break;
                        }
                    }
                }
                
                if (gcc_bw <= 0.0) {
                    gcc_bw = trace_bw;
                }
            }
            
            scaled_bw = gcc_bw * mu_value;
            
            BandwidthRecord default_record(timestamp, trace_bw, gcc_bw, scaled_bw, mu_value);
            return default_record;
        }
        
        BandwidthRecord closest_record = bandwidth_history_.front();
        Time min_difference = Abs(timestamp - closest_record.timestamp);
        
        for (const auto& record : bandwidth_history_) {
            Time difference = Abs(timestamp - record.timestamp);
            if (difference < min_difference) {
                min_difference = difference;
                closest_record = record;
            }
        }
        
        return closest_record;
    }
    
    // 获取当前带宽值
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
    
    // 设置BandwidthChanger
    void SetBandwidthChanger(BandwidthChanger* changer) {
        m_bandwidth_changer = changer;
        if (changer) {
            NS_LOG_DEBUG("BandwidthChanger set in FrameManager");
        }
    }
    
    // 输出带宽历史记录
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
    
    // 获取带宽历史记录
    const std::deque<BandwidthRecord>& GetBandwidthHistory() const {
        return bandwidth_history_;
    }
    
    // 获取指定时间附近的真实GCC带宽
    double GetNearestGccBandwidth(Time timestamp) const {
        if (bandwidth_history_.empty()) {
            return current_gcc_bandwidth_;
        }
        
        Time recent_threshold = timestamp - Seconds(1);
        double nearest_gcc = 0.0;
        Time min_diff = Seconds(1000);
        
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
        
        for (const auto& record : bandwidth_history_) {
            if (record.gcc_bandwidth > 0.0) {
                return record.gcc_bandwidth;
            }
        }
        
        return current_gcc_bandwidth_;
    }
    
private:
    // 为每个包触发RL状态记录
    void TriggerRLStateForPacket(uint32_t frame_id, uint32_t packet_index, Time send_time, Time arrival_time, uint32_t seq_num = 0) {
        NS_LOG_DEBUG("TriggerRLStateForPacket called at " << arrival_time.GetSeconds() << "s"
                     << " Frame: " << frame_id << ", Packet: " << packet_index << ", Seq: " << seq_num);
        
        if (!rl_manager_) {
            NS_LOG_ERROR("RL manager is null!");
            return;
        }
        
        auto frame_it = frames.find(frame_id);
        if (frame_it == frames.end()) {
            NS_LOG_ERROR("Frame " << frame_id << " not found!");
            return;
        }
        
        const FrameStatistics& frame = frame_it->second;
        
        // 更新RL状态管理器中的实时丢包率
        rl_manager_->UpdateRealTimeLossRate(arrival_time);
        
        // 计算实际延迟
        double current_delay_ms = (arrival_time - send_time).GetMilliSeconds();
        
        // 从trace获取RTT和loss值
        double trace_rtt_ms = 30.0;
        double trace_loss_rate = 0.01;
        
        if (m_bandwidth_changer) {
            uint32_t timestamp_ms = static_cast<uint32_t>(arrival_time.GetMilliSeconds());
            TraceData trace_data = m_bandwidth_changer->GetTraceDataAtTime(timestamp_ms);
            trace_rtt_ms = trace_data.rtt;
            trace_loss_rate = trace_data.loss;
        }
        
        // 更新网络状态（使用实时丢包率）
        rl_manager_->UpdateNetworkState(current_delay_ms, rl_manager_->GetCurrentLossRate(), 
                                      MilliSeconds(trace_rtt_ms));
        
        // 获取当前网络参数
        double mu_used = rl_manager_->GetCurrentMu();
        double current_loss_rate = rl_manager_->GetCurrentLossRate();  // 使用实时统计的丢包率
        double Rt_prev = rl_manager_->GetLastPacketRt();
        
        NS_LOG_DEBUG("Network parameters: mu=" << mu_used << ", loss_rate=" << current_loss_rate
                     << ", Rt_prev=" << Rt_prev << ", delay=" << current_delay_ms << "ms");
        
        // 获取带宽信息
        BandwidthRecord bw_record = GetBandwidthAtTime(arrival_time);
        double trace_bandwidth = bw_record.trace_bandwidth;
        double gcc_bandwidth = bw_record.gcc_bandwidth;
        double scaled_gcc_bandwidth = bw_record.scaled_bandwidth;
        
        // 如果GCC带宽为0或不可信，尝试获取最近的GCC带宽
        if (gcc_bandwidth <= 0.0 || gcc_bandwidth == trace_bandwidth) {
            gcc_bandwidth = GetNearestGccBandwidth(arrival_time);
            scaled_gcc_bandwidth = gcc_bandwidth * mu_used;
        }
        
        // 计算传输机会
        uint32_t Rt = rl_manager_->CalculateTransmissionOpportunities(
            arrival_time, frame.deadline, DEFAULT_PACKET_SIZE, trace_bandwidth);
        
        double miss_deadline_time = 0.0;
        if (arrival_time > frame.deadline) {
            miss_deadline_time = (arrival_time - frame.deadline).GetSeconds();
        }

        // 计算单个包的奖励
        double reward = rl_manager_->CalculatePacketReward(
            mu_used, gcc_bandwidth, trace_bandwidth, current_delay_ms, current_loss_rate, 
            miss_deadline_time, Rt, Rt_prev, frame_id, packet_index);
        
        double bandwidth_utilization = 0.0;
        if (trace_bandwidth > 0.0) {
            bandwidth_utilization = scaled_gcc_bandwidth / trace_bandwidth;
            bandwidth_utilization = std::min(std::max(bandwidth_utilization, 0.0), 1.0);
        }
    

        // 计算延迟惩罚
        double p_delay = 0.0;
        if (current_delay_ms < 30.0) {
            p_delay = current_delay_ms / 200.0;
        } else if (current_delay_ms < 80) {
            p_delay = 0.15 + current_delay_ms / 100.0;
        } else {
            p_delay = current_delay_ms / 50.0 + 0.65;
        }
        p_delay = std::min(p_delay, 1.0);
        
        double delay_sensitivity = 1.0 + (1.0 - Rt_prev  / 10.0) * 0.3;
        p_delay *= delay_sensitivity;
        
        // 丢包率惩罚
        double Ptget = 1.0 - rl_manager_->GetMaxLossRate();
        double Ltol;
        if (Rt_prev == 0){
            Ltol = 0.01;
        } else {
            Ltol = std::pow(1.0 - Ptget, 1.0 / Rt_prev);
        }
        double adaptive_tolerance = Ltol * (1.0 + 0.5 * (Rt_prev / 10.0));
        double p_loss = current_loss_rate / adaptive_tolerance;
        p_loss = std::min(p_loss, 1.0);
        
        // 错过截止时间惩罚
        double p_mddl = 0.0;
        double current_rtt = trace_rtt_ms / 1000.0;
        
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

        // 记录包状态
        rl_manager_->RecordPacketState(
            frame_id, packet_index, mu_used, Rt, current_loss_rate, reward,
            send_time, arrival_time, frame.deadline, bandwidth_utilization, p_delay, p_loss, p_mddl, current_delay_ms);
        
        NS_LOG_DEBUG("Recorded RL state for Frame " << frame_id << " Packet " << packet_index
                   << " delay=" << current_delay_ms << "ms, Rt=" << Rt << ", reward=" << reward);
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
        
        FrameStatistics default_frame;
        default_frame.frame_id = frame_id;
        frames[frame_id] = default_frame;
        return frames[frame_id];
    }
    
private:
    double current_trace_bandwidth_;
    double current_gcc_bandwidth_;
    double current_scaled_bandwidth_;
    BandwidthChanger* m_bandwidth_changer;
    std::deque<BandwidthRecord> bandwidth_history_;
    
    std::map<uint32_t, FrameStatistics> frames;
    std::map<uint32_t, uint32_t> packet_sequence_map_;  // 序列号到packet_id的映射
    uint32_t current_frame_id;
    Time last_frame_complete_time;
    uint32_t packet_counter;
    VideoTraceManager* trace_manager;
    RLStateManager* rl_manager_;
};

// 增强的WebrtcTrace类 - 完整版本
class FrameAwareWebrtcTrace : public WebrtcTrace {
public:
    typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;
    typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;
    typedef Callback<uint32_t, uint32_t> GetTraceBandwidthCallback;
    
    FrameAwareWebrtcTrace(FrameManager* frame_manager = nullptr, RLStateManager* rl_manager = nullptr, 
                         double bandwidth_scale_factor = 1.0) 
        : frame_manager_(frame_manager), rl_manager_(rl_manager), 
          total_bw_changes(0), bandwidth_scale_factor_(bandwidth_scale_factor),
          m_changer(nullptr), webrtc_seq_counter_(1000000) {
        NS_LOG_INFO("FrameAwareWebrtcTrace created with bandwidth scale factor: " << bandwidth_scale_factor_);
        std::cout << "[FrameAwareWebrtcTrace] Created with:" << std::endl;
        std::cout << "  - FrameManager: " << (frame_manager ? "Yes" : "No") << std::endl;
        std::cout << "  - RLStateManager: " << (rl_manager ? "Yes" : "No") << std::endl;
        std::cout << "  - μ: " << bandwidth_scale_factor_ << std::endl;
        std::cout << "  - WebRTC seq start: " << webrtc_seq_counter_ << std::endl;
    }
    
    virtual ~FrameAwareWebrtcTrace() {
        std::cout << "[FrameAwareWebrtcTrace] Destructor called" << std::endl;
        std::cout << "  Total bandwidth changes: " << total_bw_changes << std::endl;
        std::cout << "  Original BW records: " << original_bw_history.size() << std::endl;
        std::cout << "  Scaled BW records: " << scaled_bw_history.size() << std::endl;
        std::cout << "  Packet arrivals: " << packet_arrival_count_ << std::endl;
        std::cout << "  Sent records created: " << sent_records_created_ << std::endl;
    }
    
    void SetBwTraceFuc(TraceBandwidth cb) {
        m_traceBw = cb;
        NS_LOG_INFO("Bandwidth trace callback set in FrameAwareWebrtcTrace");
    }
    
    void SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
        m_traceScaledBw = cb;
        NS_LOG_INFO("Scaled bandwidth trace callback set in FrameAwareWebrtcTrace");
    }
    
    void SetTraceBandwidthCallback(GetTraceBandwidthCallback cb) {
        m_getTraceBw = cb;
        NS_LOG_INFO("Trace bandwidth callback set in FrameAwareWebrtcTrace");
    }
    
    void SetBandwidthChanger(BandwidthChanger* changer) {
        m_changer = changer;
        NS_LOG_INFO("BandwidthChanger set in FrameAwareWebrtcTrace");
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
    
// 修改FrameAwareWebrtcTrace::OnReceiptPktInfo中的调用
    virtual void OnReceiptPktInfo(uint32_t now, uint32_t webrtc_seq, uint32_t owd) {
        packet_arrival_count_++;
        
        NS_LOG_INFO("=== OnReceiptPktInfo START ===");
        NS_LOG_INFO("Time: " << now << "ms, WebRTC seq: " << webrtc_seq << ", OWD: " << owd);
        
        // 调用父类处理
        WebrtcTrace::OnReceiptPktInfo(now, webrtc_seq, owd);
        
        Time arrival_time = MilliSeconds(now);
        
        // ====== 关键修复：处理丢包统计 ======
        if (rl_manager_ && rl_manager_->GetSenderLossStatistics()) {
            SenderLossStatistics* loss_stats = rl_manager_->GetSenderLossStatistics();
            
            // 方法1：使用WebRTC序列号直接记录
            // 为每个WebRTC序列号创建对应的发送记录
            if (webrtc_seq_map_.find(webrtc_seq) == webrtc_seq_map_.end()) {
                // 新序列号，创建映射
                uint32_t internal_seq = webrtc_seq_counter_++;
                webrtc_seq_map_[webrtc_seq] = internal_seq;
                
                // 创建发送记录（估计在20ms前发送）
                Time estimated_send_time = arrival_time - MilliSeconds(20);
                loss_stats->RecordPacketSent(internal_seq, estimated_send_time);
                sent_records_created_++;
                
                NS_LOG_INFO("Created new mapping: WebRTC seq=" << webrtc_seq 
                        << " -> Internal seq=" << internal_seq);
            }
            
            // 获取内部序列号并记录ACK
            uint32_t internal_seq = webrtc_seq_map_[webrtc_seq];
            
            // 检查是否已经有ACK记录（避免重复）
            static std::set<uint32_t> processed_acks;
            if (processed_acks.find(webrtc_seq) == processed_acks.end()) {
                // 修改这里：调用双参数版本
                loss_stats->RecordPacketAcked(internal_seq, arrival_time);
                processed_acks.insert(webrtc_seq);
                
                // 计算并记录实时丢包率
                double current_loss = loss_stats->CalculateCurrentLossRate(arrival_time);
                NS_LOG_INFO("ACK recorded - WebRTC seq=" << webrtc_seq 
                        << ", Internal seq=" << internal_seq
                        << ", Loss rate: " << current_loss);
                
                // 将实时丢包率传递给RL管理器
                rl_manager_->SetCurrentLossRate(current_loss);
            } else {
                NS_LOG_DEBUG("Duplicate ACK for WebRTC seq=" << webrtc_seq);
            }
        } else {
            NS_LOG_WARN("No loss statistics available for packet arrival");
        }
            
        // ====== 处理帧管理器 ======
        if (frame_manager_) {
            frame_manager_->ProcessPacketArrival(arrival_time, 0, webrtc_seq);
        } else {
            NS_LOG_WARN("Frame manager is null in OnReceiptPktInfo");
        }
        
        // ====== 更新网络状态 ======
        double trace_rtt_ms = 30.0;
        double trace_loss_rate = 0.01;
        
        if (m_changer) {
            TraceData trace_data = m_changer->GetTraceDataAtTime(now);
            trace_rtt_ms = trace_data.rtt;
            trace_loss_rate = trace_data.loss;
            
            NS_LOG_DEBUG("Trace data: RTT=" << trace_rtt_ms << "ms, Loss=" << trace_loss_rate);
        }
        
        if (rl_manager_) {
            double current_delay_ms = static_cast<double>(owd) / 1000.0;
            
            // 使用统计的丢包率，而不是trace的loss
            double actual_loss_rate = 0.0;
            if (rl_manager_->GetSenderLossStatistics()) {
                actual_loss_rate = rl_manager_->GetSenderLossStatistics()->CalculateCurrentLossRate(arrival_time);
            }
            
            // 如果统计的丢包率不可用，使用trace的值
            if (actual_loss_rate == 0.0 && packet_arrival_count_ > 10) {
                actual_loss_rate = trace_loss_rate;
            }
            
            rl_manager_->UpdateNetworkState(current_delay_ms, actual_loss_rate, MilliSeconds(trace_rtt_ms));
            
            NS_LOG_DEBUG("Updated network state: delay=" << current_delay_ms << "ms, "
                       << "loss=" << actual_loss_rate << ", RTT=" << trace_rtt_ms << "ms");
        }
        
        // 调试输出
        if (packet_arrival_count_ % 100 == 0) {
            std::cout << "[PacketArrival] Count=" << packet_arrival_count_ 
                     << ", Time=" << now << "ms, Seq=" << webrtc_seq 
                     << ", SentRecords=" << sent_records_created_ << std::endl;
        }
        
        NS_LOG_INFO("=== OnReceiptPktInfo END ===");
    }
    
    // 带宽估计回调 - 修复版本
    virtual void OnBW(uint32_t now, uint32_t bps) {
        NS_LOG_INFO("=== OnBW START ===");
        NS_LOG_INFO("Time: " << now << "ms, Bandwidth: " << bps << " bps (" 
                   << (bps / 1000000.0) << " Mbps)");
        
        // 确保调用父类方法
        WebrtcTrace::OnBW(now, bps);
        
        total_bw_changes++;
        original_bw_history.push_back(std::make_pair(now, bps));
        
        // 获取当前μ值
        double current_mu = 1.0;
        if (rl_manager_) {
            current_mu = rl_manager_->GetCurrentMu();
            NS_LOG_INFO("Current μ from RL manager: " << current_mu);
        } else {
            NS_LOG_WARN("RL manager is null, using default μ=1.0");
        }
        
        // 计算缩放后的带宽
        uint32_t scaled_bw = static_cast<uint32_t>(bps * current_mu);
        
        // 记录缩放带宽历史
        scaled_bw_history.push_back(std::make_tuple(now, bps, scaled_bw, current_mu));
        
        // 获取trace数据
        TraceData trace_data;
        if (m_changer) {
            trace_data = m_changer->GetTraceDataAtTime(now);
            NS_LOG_INFO("Trace data: bw=" << trace_data.bandwidth << " bps, "
                       << "rtt=" << trace_data.rtt << "ms, loss=" << trace_data.loss);
        } else {
            trace_data.bandwidth = bps;
            trace_data.rtt = 30.0;
            trace_data.loss = 0.01;
            NS_LOG_WARN("No BandwidthChanger available, using default trace data");
        }
        
        // 更新帧管理器的带宽记录
        if (frame_manager_) {
            Time timestamp = MilliSeconds(now);
            
            // 检查是否重复记录（避免时间戳太接近）
            bool should_add = true;
            const auto& history = frame_manager_->GetBandwidthHistory();
            if (!history.empty()) {
                const auto& last_record = history.back();
                if (Abs(timestamp - last_record.timestamp) < MilliSeconds(10)) {
                    should_add = false;
                    NS_LOG_DEBUG("Similar timestamp exists, skipping duplicate bandwidth record");
                }
            }
            
            if (should_add) {
                frame_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, bps, scaled_bw, current_mu);
                NS_LOG_INFO("Added bandwidth record: time=" << now << "ms, "
                          << "trace=" << trace_data.bandwidth << " bps, "
                          << "gcc=" << bps << " bps, "
                          << "scaled=" << scaled_bw << " bps, "
                          << "μ=" << current_mu);
            }
        } else {
            NS_LOG_WARN("Frame manager is null in OnBW");
        }
        
        // 调用回调函数
        if (!m_traceBw.IsNull()) {
            m_traceBw(now, bps);
            NS_LOG_DEBUG("Called bandwidth trace callback");
        }
        
        if (!m_traceScaledBw.IsNull()) {
            m_traceScaledBw(now, bps, scaled_bw, current_mu);
            NS_LOG_DEBUG("Called scaled bandwidth trace callback");
        }
        
        // 定期输出统计
        if (total_bw_changes % 10 == 0) {
            std::cout << "[BandwidthUpdate] #" << total_bw_changes 
                     << ", Time=" << now << "ms, BPS=" << bps 
                     << ", μ=" << current_mu << ", Scaled=" << scaled_bw << std::endl;
        }
        
        NS_LOG_INFO("=== OnBW END ===");
    }
    
    // 记录缩放后的带宽（供外部调用）
    void OnScaledBandwidth(uint32_t now, uint32_t original_bps, uint32_t scaled_bps, double scale_factor) {
        NS_LOG_INFO("Scaled Bandwidth - Time: " << now << "ms, "
                   << "Original: " << original_bps << " bps, "
                   << "Scaled: " << scaled_bps << " bps, "
                   << "μ: " << scale_factor);
        
        scaled_bw_history.push_back(std::make_tuple(now, original_bps, scaled_bps, scale_factor));
        
        // 更新原始带宽历史
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
        
        // 更新帧管理器
        if (frame_manager_) {
            Time timestamp = MilliSeconds(now);
            
            TraceData trace_data;
            if (m_changer) {
                trace_data = m_changer->GetTraceDataAtTime(now);
            } else {
                trace_data.bandwidth = original_bps;
                trace_data.rtt = 30.0;
                trace_data.loss = 0.01;
            }
            
            frame_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, 
                                              original_bps, scaled_bps, scale_factor);
        }
    }
    
    // 输出带宽统计
    void OutputBandwidthStatistics(const std::string& filename, double loss_rate = 0.01) {
        std::string full_filename = filename;
        if (full_filename.find("_bandwidth_statistics.csv") == std::string::npos) {
            full_filename += "_bandwidth_statistics.csv";
        }
        
        std::ofstream file(full_filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open bandwidth statistics file: " << full_filename);
            return;
        }
        
        // 写入头部信息
        file << "# FrameAwareWebrtcTrace Bandwidth Statistics" << std::endl;
        file << "# Bandwidth scale factor (μ): " << bandwidth_scale_factor_ << std::endl;
        file << "# Loss rate (L): " << loss_rate << std::endl;
        file << "# Total bandwidth changes: " << total_bw_changes << std::endl;
        file << "# Total packet arrivals: " << packet_arrival_count_ << std::endl;
        file << "# Sent records created: " << sent_records_created_ << std::endl;
        file << "timestamp_ms,trace_bandwidth_bps,trace_bandwidth_mbps,trace_rtt_ms,trace_loss,"
            << "original_bandwidth_bps,original_bandwidth_mbps,"
            << "scaled_bandwidth_bps,scaled_bandwidth_mbps,scale_factor" << std::endl;
        
        // 确保有数据
        if (scaled_bw_history.empty() && original_bw_history.empty()) {
            NS_LOG_WARN("No bandwidth data to output!");
            file << "# No data available" << std::endl;
            file.close();
            return;
        }
        
        std::set<uint32_t> processed_timestamps;
        
        // 首先处理缩放带宽历史
        for (const auto& scaled_entry : scaled_bw_history) {
            uint32_t timestamp = std::get<0>(scaled_entry);
            uint32_t original_bw = std::get<1>(scaled_entry);
            uint32_t scaled_bw = std::get<2>(scaled_entry);
            double scale_factor = std::get<3>(scaled_entry);
            
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
            
            processed_timestamps.insert(timestamp);
        }
        
        // 处理只有原始带宽数据的条目
        for (const auto& original_entry : original_bw_history) {
            uint32_t timestamp = original_entry.first;
            uint32_t original_bw = original_entry.second;
            
            if (processed_timestamps.find(timestamp) == processed_timestamps.end()) {
                uint32_t scaled_bw = original_bw;
                double scale_factor = 1.0;
                
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
        
        NS_LOG_INFO("Bandwidth statistics saved to: " << full_filename 
                   << " with " << (scaled_bw_history.size() + original_bw_history.size()) 
                   << " entries");
        std::cout << "[FrameAwareWebrtcTrace] Bandwidth statistics saved: " << full_filename 
                  << " (" << (scaled_bw_history.size() + original_bw_history.size()) 
                  << " records)" << std::endl;
    }
    
    // 输出包到达统计
    void OutputPacketArrivalStatistics(const std::string& filename) {
        std::string full_filename = filename + "_packet_arrival_statistics.csv";
        
        std::ofstream file(full_filename);
        if (!file.is_open()) {
            NS_LOG_ERROR("Cannot open packet arrival statistics file: " << full_filename);
            return;
        }
        
        file << "# FrameAwareWebrtcTrace Packet Arrival Statistics" << std::endl;
        file << "# Total packet arrivals: " << packet_arrival_count_ << std::endl;
        file << "# Sent records created: " << sent_records_created_ << std::endl;
        file << "# WebRTC seq range: " 
             << (webrtc_seq_map_.empty() ? "N/A" : std::to_string(webrtc_seq_map_.begin()->first)) 
             << " - "
             << (webrtc_seq_map_.empty() ? "N/A" : std::to_string(webrtc_seq_map_.rbegin()->first)) 
             << std::endl;
        
        // 如果有RL管理器，输出丢包统计
        if (rl_manager_ && rl_manager_->GetSenderLossStatistics()) {
            SenderLossStatistics* stats = rl_manager_->GetSenderLossStatistics();
            file << "# Overall loss rate: " << stats->GetOverallLossRate() << std::endl;
            file << "# Total sent: " << stats->GetTotalSent() << std::endl;
            file << "# Total ACKed: " << stats->GetTotalAcked() << std::endl;
        }
        
        file.close();
        
        NS_LOG_INFO("Packet arrival statistics saved to: " << full_filename);
        std::cout << "[FrameAwareWebrtcTrace] Packet arrival statistics saved: " 
                  << full_filename << std::endl;
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
    
    // 获取序列号映射（用于调试）
    const std::map<uint32_t, uint32_t>& GetWebrtcSeqMap() const {
        return webrtc_seq_map_;
    }
    
    // 获取包到达计数
    uint32_t GetPacketArrivalCount() const {
        return packet_arrival_count_;
    }
    
    // 获取带宽历史记录大小（用于调试）
    size_t GetOriginalBwHistorySize() const { 
        return original_bw_history.size(); 
    }
    
    size_t GetScaledBwHistorySize() const { 
        return scaled_bw_history.size(); 
    }
    
private:
    FrameManager* frame_manager_;
    RLStateManager* rl_manager_;
    uint32_t total_bw_changes;
    double bandwidth_scale_factor_;
    std::vector<std::pair<uint32_t, uint32_t>> original_bw_history;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, double>> scaled_bw_history;
    
    // 序列号映射
    std::map<uint32_t, uint32_t> webrtc_seq_map_;  // WebRTC seq -> Internal seq
    uint32_t webrtc_seq_counter_;
    uint32_t packet_arrival_count_;
    uint32_t sent_records_created_;
    
    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    GetTraceBandwidthCallback m_getTraceBw;
    BandwidthChanger* m_changer;
};


// 函数声明
void TriggerRLStateCalculation(RLStateManager* rl_manager,
                              BandwidthChanger* bandwidth_changer, 
                              Ptr<ExponentialRandomVariable> interval,
                              double current_loss_rate,
                              SenderLossStatistics* loss_statistics = nullptr);

// 模拟数据包发送的函数
void SimulatePacketSending(SenderLossStatistics* loss_statistics, Time interval) {
    if (!loss_statistics) {
        NS_LOG_WARN("No loss statistics available for packet sending simulation");
        return;
    }
    
    Time current_time = Simulator::Now();
    
    // 模拟发送数据包
    uint32_t seq_num = loss_statistics->GenerateAndRecordPacketSent(current_time);
    NS_LOG_DEBUG("Simulated packet sent: seq=" << seq_num << ", time=" << current_time.GetSeconds() << "s");
    
    // 模拟ACK（80%的概率收到ACK，模拟20%的丢包率）
    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    if (rand->GetValue(0.0, 1.0) < 0.8) {
        // 模拟网络延迟后收到ACK
        Time ack_delay = MilliSeconds(10 + rand->GetValue(0.0, 20.0));
        Simulator::Schedule(ack_delay, &SenderLossStatistics::RecordPacketAcked, 
                          loss_statistics, seq_num, current_time + ack_delay);
        NS_LOG_DEBUG("Scheduled ACK for seq=" << seq_num << " after " << ack_delay.GetMilliSeconds() << "ms");
    } else {
        NS_LOG_DEBUG("Packet seq=" << seq_num << " will be lost (no ACK scheduled)");
    }
    
    // 安排下一次发送
    Simulator::Schedule(interval, &SimulatePacketSending, loss_statistics, interval);
}

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
                        VideoTraceManager* video_trace_manager = nullptr,
                        SenderLossStatistics* loss_statistics = nullptr)
{
    NS_LOG_INFO("Installing WebRTC application with RL state management and video trace");
    
    // 创建应用程序对象
    Ptr<WebrtcSender> sendApp = CreateObject<WebrtcSender>(manager);
    Ptr<WebrtcReceiver> recvApp = CreateObject<WebrtcReceiver>(manager);
    
    // 设置RLStateManager的丢包统计器
    if (rl_manager && loss_statistics) {
        rl_manager->SetSenderLossStatistics(loss_statistics);
        NS_LOG_INFO("SenderLossStatistics set in RLStateManager");
        
        // 启动模拟数据包发送（用于测试）
        Time send_interval = MilliSeconds(20); // 每20ms发送一个包
        // Simulator::Schedule(MilliSeconds(100), &SimulatePacketSending, loss_statistics, send_interval);
        NS_LOG_INFO("Started simulated packet sending with interval " << send_interval.GetMilliSeconds() << "ms");
    }
    
    // 确保FrameManager知道RLStateManager
    if (frame_manager && rl_manager) {
        frame_manager->SetRLStateManager(rl_manager);
        NS_LOG_INFO("RLStateManager set in FrameManager for packet-level RL recording");
    }
    
    // 设置FrameManager的BandwidthChanger
    if (frame_manager && bandwidth_changer) {
        frame_manager->SetBandwidthChanger(bandwidth_changer);
        NS_LOG_INFO("BandwidthChanger set in FrameManager");
    }
    
    // 设置trace带宽changer
    if (trace && bandwidth_changer) {
        trace->SetBandwidthChanger(bandwidth_changer);
        NS_LOG_INFO("BandwidthChanger set in FrameAwareWebrtcTrace");
    }
    
    // RL状态计算调度
    if (rl_manager != nullptr && bandwidth_changer != nullptr) {
        Ptr<ExponentialRandomVariable> interval = CreateObject<ExponentialRandomVariable>();
        interval->SetAttribute("Mean", DoubleValue(0.01));
        
        Simulator::Schedule(Seconds(0.1), &TriggerRLStateCalculation,
                        rl_manager, bandwidth_changer, interval, loss_rate, loss_statistics);
        NS_LOG_INFO("Scheduled RL state calculation");
    } else {
        if (rl_manager == nullptr) {
            NS_LOG_WARN("RLStateManager is null, skipping RL state calculation scheduling");
        }
        if (bandwidth_changer == nullptr) {
            NS_LOG_WARN("BandwidthChanger is null, skipping RL state calculation scheduling");
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
            sendApp->SetBwTraceFuc(MakeCallback(&FrameAwareWebrtcTrace::OnBW, trace));
            NS_LOG_INFO("Bandwidth trace callback set for WebrtcSender");
        }
        if (trace->LogFlag() & (WebrtcTrace::E_WEBRTC_OWD | WebrtcTrace::E_WEBRTC_LOSS)) {
            recvApp->SetTraceReceiptPktInfo(MakeCallback(&FrameAwareWebrtcTrace::OnReceiptPktInfo, trace));
            NS_LOG_INFO("Packet receipt callback set for WebrtcReceiver");
        }
    } else {
        NS_LOG_WARN("FrameAwareWebrtcTrace is null, cannot set callbacks");
    }
    
    // 关联管理器
    if (frame_manager && trace) {
        trace->SetFrameManager(frame_manager);
        NS_LOG_INFO("FrameManager set in FrameAwareWebrtcTrace");
    }
    
    if (rl_manager && trace) {
        trace->SetRLStateManager(rl_manager);
        NS_LOG_INFO("RLStateManager set in FrameAwareWebrtcTrace");
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
    
    NS_LOG_INFO("WebRTC application installed successfully with manager and video trace");
}

void TriggerRLStateCalculation(RLStateManager* rl_manager,
                              BandwidthChanger* bandwidth_changer, 
                              Ptr<ExponentialRandomVariable> interval,
                              double current_loss_rate,
                              SenderLossStatistics* loss_statistics)
{
    Time now = Simulator::Now();
    
    // 添加最大调用次数限制
    static int call_count = 0;
    call_count++;
    
    if (call_count > 10000) { // 防止无限循环
        NS_LOG_WARN("TriggerRLStateCalculation stopped after " << call_count << " calls");
        return;
    }
    
    NS_LOG_DEBUG("TriggerRLStateCalculation #" << call_count << " at " << now.GetSeconds() << "s");
    
    // 更新实时丢包率
    if (rl_manager != nullptr) {
        rl_manager->UpdateRealTimeLossRate(now);
        
        // 简单记录，不进行复杂计算
        if (call_count % 100 == 0) { // 每100次打印一次
            NS_LOG_INFO("RL state update #" << call_count << " at " << now.GetSeconds() << "s");
        }
    }
    
    // 安排下一次触发（如果还有时间）
    if (now < Seconds(100)) { // 设置最大仿真时间
        double next_interval = 0.01; // 固定间隔0.01秒，避免随机数问题
        if (interval) {
            next_interval = interval->GetValue();
            next_interval = std::max(0.001, std::min(next_interval, 0.1)); // 限制在0.001-0.1秒之间
        }
        
        Simulator::Schedule(Seconds(next_interval), &TriggerRLStateCalculation,
                           rl_manager, bandwidth_changer, interval, current_loss_rate, loss_statistics);
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
        double initial_rtt = 30.0;
        double initial_loss = loss_rate;
        
        if (changer && !trace_filename.empty()) {
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
        rl_manager->SetCurrentLossRate(initial_loss);
        
        rl_managers.push_back(std::move(rl_manager));
    }
    
    // 创建发送端丢包统计器
    std::vector<std::unique_ptr<SenderLossStatistics>> loss_statistics;
    for (int i = 0; i < num; i++) {
        loss_statistics.push_back(std::make_unique<SenderLossStatistics>());
        std::cout << "Created SenderLossStatistics " << i+1 << std::endl;
    }
        
    for (int i=0;i<num;i++) {
        std::string log=prefix+std::to_string(i+1);
        FrameAwareWebrtcTrace *trace=new FrameAwareWebrtcTrace(frame_managers[i].get(), rl_managers[i].get(), bandwidth_scale_factor);
        trace_vec.push_back(trace);
        
        trace->Log(log, WebrtcTrace::E_WEBRTC_BW | WebrtcTrace::E_WEBRTC_LOSS | WebrtcTrace::E_WEBRTC_OWD);
        
        // 设置FrameManager的BandwidthChanger
        if (frame_managers[i] && changer) {
            frame_managers[i]->SetBandwidthChanger(changer);
        }
        
        // 设置trace带宽changer
        if (trace && changer) {
            trace->SetBandwidthChanger(changer);
        }
        
        // 安装应用程序，传递丢包统计器
        InstallWebrtcApplication(nodes.Get(0), nodes.Get(1), sendPort, recvPort,
                    Seconds(startapptime), Seconds(endapptime),
                sesssion_manager.at(i).get(), trace, 
                frame_managers[i].get(), rl_managers[i].get(), 
                bandwidth_scale_factor, loss_rate, changer,
                video_trace_manager.get(), loss_statistics[i].get());
        
        sendPort++;
        recvPort++;
        
        std::cout << "WebRTC application " << i+1 << " installed with:" << std::endl;
        std::cout << "  - Real-time loss statistics" << std::endl;
        std::cout << "  - Rt-group based reward calculation" << std::endl;
        std::cout << "  - Bandwidth scaling with μ=" << bandwidth_scale_factor << std::endl;
        std::cout << "  - Loss rate with L=" << loss_rate << " (initial)" << std::endl;
        std::cout << "  - Video trace analysis: " << (video_trace_file.empty() ? "disabled" : "enabled") << std::endl;
        std::cout << "  - BandwidthChanger: " << (changer ? "enabled" : "disabled") << std::endl;
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
        
        // 输出带宽历史记录
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
        
        // 检查trace对象是否有数据
        if (trace_vec[i]) {
            std::cout << "Trace " << i+1 << " has " << trace_vec[i]->GetOriginalBwHistorySize() 
                      << " original BW records and " << trace_vec[i]->GetScaledBwHistorySize() 
                      << " scaled BW records" << std::endl;
            trace_vec[i]->OutputBandwidthStatistics(bw_stats_file, loss_rate);
        } else {
            std::cerr << "ERROR: Trace object " << i+1 << " is null!" << std::endl;
        }
        
        // 输出RL状态记录
        std::cout << "Outputting RL state records for session " << i+1 << std::endl;
        rl_managers[i]->OutputStateRecords(prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
        
        // 输出Rt分组奖励记录
        std::cout << "Outputting Rt group reward records for session " << i+1 << std::endl;
        rl_managers[i]->OutputRtGroupRewards(prefix + std::to_string(i+1), bandwidth_scale_factor, loss_rate);
        
        // 输出丢包统计信息
        if (loss_statistics[i]) {
            std::cout << "Loss statistics for session " << i+1 << ":" << std::endl;
            std::cout << "  Sent packets: " << loss_statistics[i]->GetSentPacketCount() << std::endl;
            std::cout << "  ACKed packets: " << loss_statistics[i]->GetAckedPacketCount() << std::endl;
            double final_loss = loss_statistics[i]->CalculateCurrentLossRate(Simulator::Now());
            std::cout << "  Final loss rate: " << final_loss << std::endl;
        }
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
    std::cout << "Real-time loss statistics experiment completed" << std::endl;
    std::cout << "Rt-group reward calculation completed" << std::endl;
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
    std::cout << "Loss rate: " << loss_rate << std::endl;
    std::cout << "Bandwidth scale factor μ: " << bandwidth_scale_factor << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "none" : video_trace_file) << std::endl;
    std::cout << "Base output folder: " << base_output_folder << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // 获取当前工作目录
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) == nullptr) {
        std::cerr << "Error getting current working directory" << std::endl;
        return;
    }
    
    std::string current_dir(buffer);
    std::string data_result_folder = current_dir + "/" + base_output_folder;
    
    // 添加trace文件名的子目录
    // 从trace文件名提取基本名称
    std::string trace_name = trace_file;
    size_t last_slash = trace_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        trace_name = trace_name.substr(last_slash + 1);
    }
    size_t last_dot = trace_name.find_last_of(".");
    if (last_dot != std::string::npos) {
        trace_name = trace_name.substr(0, last_dot);
    }
    
    data_result_folder = data_result_folder + "/" + trace_name;
    
    std::cout << "Using output path: " << data_result_folder << std::endl;
    
    // 创建目录
    std::string create_dir_cmd = "mkdir -p \"" + data_result_folder + "\"";
    std::cout << "Creating directory: " << create_dir_cmd << std::endl;
    int result = system(create_dir_cmd.c_str());
    if (result != 0) {
        std::cerr << "Warning: Failed to create directory: " << data_result_folder 
                  << " (result=" << result << ")" << std::endl;
        // 尝试直接使用mkdir系统调用
        std::string cmd = "mkdir -p " + data_result_folder;
        result = system(cmd.c_str());
        if (result != 0) {
            // 最后尝试在当前目录创建
            data_result_folder = "Trace_Result_" + trace_name;
            std::cout << "Falling back to: " << data_result_folder << std::endl;
            system(("mkdir -p " + data_result_folder).c_str());
        }
    } else {
        std::cout << "Confirmed output directory: " << data_result_folder << std::endl;
    }
    
    // 设置WebRTC跟踪文件夹
    set_webrtc_trace_folder(data_result_folder);
    
    std::unique_ptr<TriggerRandomLoss> triggerloss = nullptr;
    std::unique_ptr<BandwidthChanger> changer = nullptr;

    std::cout << "Configuring packet loss rate: " << loss_rate << std::endl;
    
    Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (loss_rate));
    Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
    Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (loss_rate));
    Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));
    
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
    std::cout << "=== WebRTC TraceAll-Frame with Real-time Loss Statistics and Rt-group Reward ===" << std::endl;
    
    // 启用详细日志
    LogComponentEnable("webrtc-static", LOG_LEVEL_INFO);
    LogComponentEnable("WebrtcSender", LOG_LEVEL_INFO); 
    LogComponentEnable("WebrtcReceiver", LOG_LEVEL_INFO);
    
    // 设置默认参数
    std::string mode("simu");
    std::string topo("change");
    std::string instance("default_instance");
    std::string trace_file(""); 
    std::string video_trace_file("");
    std::string max_bandwidth("20");
    std::string loss_rate("0.01");
    std::string folder("trace_results");  // 修改为相对路径
    std::string bandwidth_scale("1.0");
    
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("m", "mode", mode);
    cmd.AddValue("topo", "topology", topo);
    cmd.AddValue("it", "instance", instance);
    cmd.AddValue("trace", "trace file path", trace_file);
    cmd.AddValue("video_trace", "video trace file path", video_trace_file);
    cmd.AddValue("mb", "max_bandwidth", max_bandwidth);
    cmd.AddValue("ls", "loss_rate", loss_rate);
    cmd.AddValue("folder", "folder name to collect data", folder);
    cmd.AddValue("mu", "bandwidth_scale_factor", bandwidth_scale);
    
    cmd.Parse(argc, argv);
    
    // 验证必要参数
    if (trace_file.empty()) {
        std::cerr << "ERROR: No trace file specified. Use --trace=<file_path>" << std::endl;
        std::cerr << "Usage: ./waf --run \"scratch/webrtc-TFMN(loss) --trace=<path> [--video_trace=<video_trace_path> --it=<instance> --folder=<output_dir> --mb=<bandwidth> --ls=<loss_rate> --mu=<scale_factor>]\"" << std::endl;
        std::cerr << "Example: ./waf --run \"scratch/webrtc-TFMN(loss) --trace=/path/to/trace.log --mb=20 --ls=0.01 --mu=1.0\"" << std::endl;
        return 1;
    }
    
    // 检查trace文件是否存在
    std::ifstream test_file(trace_file);
    if (!test_file.good()) {
        std::cerr << "ERROR: Trace file does not exist or cannot be read: " << trace_file << std::endl;
        return 1;
    }
    test_file.close();
    
    // 检查视频trace文件是否存在
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

    std::cout << "Starting single trace simulation with real-time loss statistics..." << std::endl;
    std::cout << "Max bandwidth: " << mb << " Mbps" << std::endl;
    std::cout << "Loss rate: " << ls << std::endl;
    std::cout << "Bandwidth scale factor μ: " << mu << std::endl;
    std::cout << "Video trace file: " << (video_trace_file.empty() ? "not specified" : video_trace_file) << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - Real-time loss statistics from sender" << std::endl;
    std::cout << "  - Rt-group based reward calculation" << std::endl;
    std::cout << "  - Group delay = last_packet_arrival - first_packet_send" << std::endl;
    std::cout << "  - Group loss rate = real-time loss during group transmission" << std::endl;
    std::cout << "  - Reward = average of packet rewards in same Rt group" << std::endl;
    
    run_single_trace_simulation(trace_file, instance, controller_type, 1, mb, ls, folder, mu, video_trace_file);
    
    std::cout << "=== WebRTC TraceAll-Frame with Real-time Loss Statistics Completed Successfully ===" << std::endl;
    _exit(0);
    return 0;
}