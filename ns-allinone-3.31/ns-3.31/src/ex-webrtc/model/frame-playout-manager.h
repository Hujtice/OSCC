#pragma once

#include <map>
#include <set>
#include <deque>
#include <functional>
#include <fstream>
#include <string>
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "webrtc-tag.h"

namespace ns3 {

// 帧统计信息
struct FrameStatistics {
    uint32_t frame_id;
    uint32_t frame_size;              // 累计包大小（字节）
    Time send_time;                   // 第一个包发送时间
    Time first_packet_recv_time;      // 第一个包接收时间
    Time receive_complete_time;       // 最后一个包接收时间
    Time playout_deadline;            // 播放截止时间
    Time actual_playout_time;         // 实际播放时间（按序播放时）
    uint32_t rtp_timestamp;           // RTP时间戳
    bool is_keyframe;                 // 是否关键帧
    bool is_complete;                 // 所有包都收到（marker bit收到）
    bool is_played;                   // 是否已播放
    bool played_on_time;              // 是否按时播放
    bool skipped;                     // 是否被跳过
    uint32_t packets_received;        // 已接收包数
    bool marker_received;             // 是否收到marker bit（帧结束标志）
    std::set<uint64_t> received_seqs; // 已接收的包序列号（用于去重）

    FrameStatistics()
        : frame_id(0), frame_size(0), send_time(Seconds(0)),
          first_packet_recv_time(Seconds(0)), receive_complete_time(Seconds(0)),
          playout_deadline(Seconds(0)), actual_playout_time(Seconds(0)),
          is_keyframe(false), is_complete(false), is_played(false),
          played_on_time(true), skipped(false),
          packets_received(0), marker_received(false) {}
};

// 帧播放管理器
class FramePlayoutManager {
public:
    // 跳帧回调类型：参数为目标关键帧ID
    typedef std::function<void(uint32_t)> SkipFrameCallback;
    // 包接收回调类型
    typedef std::function<void(const FramePacketInfo&, const FrameStatistics&)> PacketReceivedCallback;
    // 帧完成回调类型
    typedef std::function<void(const FrameStatistics&)> FrameCompleteCallback;
    
    FramePlayoutManager();
    ~FramePlayoutManager();
    
    // 配置播放延迟参数
    void SetPlayoutDelay(Time delay);
    Time GetPlayoutDelay() const { return playout_delay_; }

    // 设置帧率
    void SetFPS(uint32_t fps);
    uint32_t GetFPS() const { return fps_; }
    
    // 设置是否启用跳帧逻辑
    void SetSkipFrameEnabled(bool enabled);
    bool IsSkipFrameEnabled() const { return skip_frame_enabled_; }
    
    // 处理接收到的包
    void OnPacketReceived(const FramePacketInfo& info, uint32_t packet_size);
    
    // 设置回调
    void SetSkipFrameCallback(SkipFrameCallback cb);
    void SetPacketReceivedCallback(PacketReceivedCallback cb);
    void SetFrameCompleteCallback(FrameCompleteCallback cb);
    
    // 输出CSV trace
    void ExportFrameTrace(const std::string& filename);
    
    // 获取统计信息
    uint32_t GetTotalFrames() const { return total_frames_; }
    uint32_t GetCompletedFrames() const { return completed_frames_; }
    uint32_t GetOnTimeFrames() const { return on_time_frames_; }
    
    // 打印统计摘要
    void PrintStatistics();
    
    // 重置状态
    void Reset();

private:
    // 检查帧是否超时
    void CheckFrameDeadline(uint32_t frame_id);

    // 调度截止时间检查
    void ScheduleDeadlineCheck(uint32_t frame_id, Time deadline);

    // 查找下一个关键帧
    uint32_t FindNextKeyFrame(uint32_t from_frame_id);

    // 标记帧完成
    void MarkFrameComplete(uint32_t frame_id);

    // 获取或创建帧统计
    FrameStatistics& GetOrCreateFrame(uint32_t frame_id);

    // 关键帧ID记录（用于查找下一个关键帧）
    void RecordKeyFrame(uint32_t frame_id);

    // 尝试按序播放帧
    void TryPlayNextFrame();

    // 播放指定帧
    void PlayFrame(uint32_t frame_id);

    // 成员变量
    Time playout_delay_;                              // 播放延迟参数
    uint32_t fps_;                                    // 帧率
    Time first_frame_playout_time_;                   // 第一帧实际播放时间（截止时间）
    bool baseline_established_;                       // 第一帧基准是否已建立
    std::map<uint32_t, FrameStatistics> frames_;      // 帧统计映射
    std::deque<uint32_t> keyframe_ids_;               // 关键帧ID列表（有序）
    uint32_t next_playout_frame_id_;                  // 下一个应该播放的帧ID（按序播放）
    uint32_t last_played_frame_id_;                   // 上一个播放的帧ID

    // 回调函数
    SkipFrameCallback skip_frame_callback_;
    PacketReceivedCallback packet_received_callback_;
    FrameCompleteCallback frame_complete_callback_;

    // 统计计数器
    uint32_t total_frames_;
    uint32_t completed_frames_;
    uint32_t on_time_frames_;

    // 跳帧状态
    bool skip_frame_enabled_;         // 是否启用跳帧逻辑
    bool skip_frame_pending_;
    uint32_t skip_target_frame_id_;

    // 恢复帧超时机制
    Time recovery_frame_timeout_;     // 恢复帧超时时间（默认500ms）
    uint32_t locked_recovery_frame_;  // 当前锁定的恢复帧ID
    EventId recovery_timeout_event_;  // 恢复帧超时事件

    // 检查恢复帧超时
    void CheckRecoveryFrameTimeout(uint32_t frame_id);

    // 调度恢复帧超时检查
    void ScheduleRecoveryTimeout(uint32_t frame_id);

    // 取消恢复帧超时
    void CancelRecoveryTimeout();

    // 放弃当前恢复帧，请求下一个
    void AbandonRecoveryFrame(uint32_t frame_id);
};

} // namespace ns3

