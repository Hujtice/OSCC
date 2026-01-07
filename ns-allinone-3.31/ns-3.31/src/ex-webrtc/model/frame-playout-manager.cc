#include "frame-playout-manager.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("FramePlayoutManager");

FramePlayoutManager::FramePlayoutManager()
    : playout_delay_(MilliSeconds(300)),  // 默认300ms播放延迟
      current_playback_frame_(0),
      total_frames_(0),
      completed_frames_(0),
      skipped_frames_(0),
      on_time_frames_(0),
      skip_frame_pending_(false),
      skip_target_frame_id_(UINT32_MAX) {
    NS_LOG_INFO("FramePlayoutManager created with default playout delay: " 
                << playout_delay_.GetMilliSeconds() << "ms");
}

FramePlayoutManager::~FramePlayoutManager() {
    PrintStatistics();
}

void FramePlayoutManager::SetPlayoutDelay(Time delay) {
    playout_delay_ = delay;
    NS_LOG_INFO("FramePlayoutManager: Playout delay set to " 
                << delay.GetMilliSeconds() << "ms");
    std::cout << "[FramePlayoutManager] Playout delay set to " 
              << delay.GetMilliSeconds() << "ms" << std::endl;
}

void FramePlayoutManager::SetSkipFrameCallback(SkipFrameCallback cb) {
    skip_frame_callback_ = cb;
    NS_LOG_INFO("FramePlayoutManager: Skip frame callback registered");
}

FrameStatistics& FramePlayoutManager::GetOrCreateFrame(uint32_t frame_id) {
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) {
        FrameStatistics stats;
        stats.frame_id = frame_id;
        frames_[frame_id] = stats;
        total_frames_++;
        return frames_[frame_id];
    }
    return it->second;
}

void FramePlayoutManager::RecordKeyFrame(uint32_t frame_id) {
    // 保持关键帧ID有序
    if (keyframe_ids_.empty() || keyframe_ids_.back() < frame_id) {
        keyframe_ids_.push_back(frame_id);
    } else {
        // 插入到正确位置
        auto pos = std::lower_bound(keyframe_ids_.begin(), keyframe_ids_.end(), frame_id);
        if (pos == keyframe_ids_.end() || *pos != frame_id) {
            keyframe_ids_.insert(pos, frame_id);
        }
    }
    
    // 只保留最近的100个关键帧记录
    while (keyframe_ids_.size() > 100) {
        keyframe_ids_.pop_front();
    }
}

void FramePlayoutManager::OnPacketReceived(const FramePacketInfo& info, uint32_t packet_size) {
    Time now = Simulator::Now();
    uint32_t frame_id = info.frame_id;
    
    // 检查是否正在跳帧，如果是则忽略跳过范围内的帧
    if (skip_frame_pending_ && frame_id < skip_target_frame_id_) {
        NS_LOG_DEBUG("FramePlayoutManager: Ignoring packet for skipped frame " << frame_id);
        return;
    }
    
    // 获取或创建帧统计
    FrameStatistics& frame = GetOrCreateFrame(frame_id);
    
    // 更新帧统计
    frame.frame_size += packet_size;
    frame.packets_received++;
    frame.rtp_timestamp = info.rtp_timestamp;
    
    // 第一个包到达时
    if (info.is_first_packet || frame.packets_received == 1) {
        frame.send_time = MilliSeconds(info.send_time_ms);
        frame.first_packet_recv_time = now;
        
        // 设置播放截止时间
        frame.playout_deadline = frame.send_time + playout_delay_;
        
        // 调度截止时间检查
        Time time_until_deadline = frame.playout_deadline - now;
        if (time_until_deadline > Time(0)) {
            ScheduleDeadlineCheck(frame_id, frame.playout_deadline);
        }
        
        NS_LOG_DEBUG("FramePlayoutManager: Frame " << frame_id 
                     << " first packet, send_time=" << frame.send_time.GetMilliSeconds()
                     << "ms, deadline=" << frame.playout_deadline.GetMilliSeconds() << "ms");
    }
    
    // 记录关键帧
    if (info.is_keyframe) {
        frame.is_keyframe = true;
        RecordKeyFrame(frame_id);
        NS_LOG_INFO("FramePlayoutManager: Keyframe detected - frame_id=" << frame_id);
    }
    
    // 最后一个包到达时（marker bit）
    if (info.is_last_packet) {
        frame.marker_received = true;
        frame.receive_complete_time = now;
        MarkFrameComplete(frame_id);
    }
}

void FramePlayoutManager::MarkFrameComplete(uint32_t frame_id) {
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return;
    
    FrameStatistics& frame = it->second;
    if (frame.is_complete) return;  // 已经标记过
    
    frame.is_complete = true;
    completed_frames_++;
    
    Time now = Simulator::Now();
    
    // 检查是否按时完成
    if (now <= frame.playout_deadline && !frame.skipped) {
        frame.played_on_time = true;
        on_time_frames_++;
        NS_LOG_INFO("FramePlayoutManager: Frame " << frame_id 
                    << " completed on time at " << now.GetMilliSeconds() << "ms"
                    << " (deadline: " << frame.playout_deadline.GetMilliSeconds() << "ms)");
    } else {
        frame.played_on_time = false;
        NS_LOG_INFO("FramePlayoutManager: Frame " << frame_id 
                    << " completed LATE at " << now.GetMilliSeconds() << "ms"
                    << " (deadline: " << frame.playout_deadline.GetMilliSeconds() << "ms)");
    }
    
    // 清除跳帧状态如果已到达目标帧
    if (skip_frame_pending_ && frame_id >= skip_target_frame_id_ && frame.is_keyframe) {
        skip_frame_pending_ = false;
        NS_LOG_INFO("FramePlayoutManager: Skip frame completed, arrived at keyframe " << frame_id);
    }
}

void FramePlayoutManager::ScheduleDeadlineCheck(uint32_t frame_id, Time deadline) {
    Time now = Simulator::Now();
    Time delay = deadline - now;
    
    if (delay > Time(0)) {
        Simulator::Schedule(delay, &FramePlayoutManager::CheckFrameDeadline, this, frame_id);
    }
}

void FramePlayoutManager::CheckFrameDeadline(uint32_t frame_id) {
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return;
    
    FrameStatistics& frame = it->second;
    
    // 如果帧已完成或已跳过，不需要处理
    if (frame.is_complete || frame.skipped) {
        return;
    }
    
    Time now = Simulator::Now();
    
    // 帧超时，需要跳帧
    if (now > frame.playout_deadline) {
        frame.played_on_time = false;
        frame.skipped = true;
        skipped_frames_++;
        
        NS_LOG_WARN("FramePlayoutManager: Frame " << frame_id 
                    << " TIMEOUT at " << now.GetMilliSeconds() << "ms"
                    << " (deadline: " << frame.playout_deadline.GetMilliSeconds() << "ms)"
                    << " - packets received: " << frame.packets_received);
        
        std::cout << "[FramePlayoutManager] Frame " << frame_id << " TIMEOUT! "
                  << "Received " << frame.packets_received << " packets, "
                  << "deadline was " << frame.playout_deadline.GetMilliSeconds() << "ms"
                  << std::endl;
        
        // 找到下一个关键帧
        uint32_t next_keyframe = FindNextKeyFrame(frame_id);
        
        if (next_keyframe != UINT32_MAX) {
            // 标记中间的所有帧为跳过
            for (uint32_t fid = frame_id; fid < next_keyframe; fid++) {
                auto& f = GetOrCreateFrame(fid);
                if (!f.is_complete && !f.skipped) {
                    f.skipped = true;
                    f.played_on_time = false;
                    skipped_frames_++;
                }
            }
            
            skip_frame_pending_ = true;
            skip_target_frame_id_ = next_keyframe;
            
            std::cout << "[FramePlayoutManager] Skipping to keyframe " << next_keyframe 
                      << std::endl;
            
            // 通知发送端跳帧
            if (skip_frame_callback_) {
                skip_frame_callback_(next_keyframe);
            }
        } else {
            NS_LOG_WARN("FramePlayoutManager: No keyframe found after frame " << frame_id);
        }
    }
}

uint32_t FramePlayoutManager::FindNextKeyFrame(uint32_t from_frame_id) {
    // 在关键帧列表中查找下一个大于from_frame_id的关键帧
    auto it = std::upper_bound(keyframe_ids_.begin(), keyframe_ids_.end(), from_frame_id);
    
    if (it != keyframe_ids_.end()) {
        return *it;
    }
    
    // 如果没找到，在帧映射中搜索
    for (auto& pair : frames_) {
        if (pair.first > from_frame_id && pair.second.is_keyframe) {
            return pair.first;
        }
    }
    
    return UINT32_MAX;
}

void FramePlayoutManager::ExportFrameTrace(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("FramePlayoutManager: Cannot open file for writing: " << filename);
        std::cerr << "[FramePlayoutManager] Cannot open file: " << filename << std::endl;
        return;
    }
    
    // CSV头
    file << "frame_id,frame_size_bytes,send_time_ms,first_recv_time_ms,"
         << "receive_complete_time_ms,playout_deadline_ms,rtp_timestamp,"
         << "is_keyframe,is_complete,played_on_time,skipped,packets_received"
         << std::endl;
    
    // 按frame_id排序输出
    for (const auto& pair : frames_) {
        const FrameStatistics& f = pair.second;
        file << f.frame_id << ","
             << f.frame_size << ","
             << f.send_time.GetMilliSeconds() << ","
             << f.first_packet_recv_time.GetMilliSeconds() << ","
             << f.receive_complete_time.GetMilliSeconds() << ","
             << f.playout_deadline.GetMilliSeconds() << ","
             << f.rtp_timestamp << ","
             << (f.is_keyframe ? 1 : 0) << ","
             << (f.is_complete ? 1 : 0) << ","
             << (f.played_on_time ? 1 : 0) << ","
             << (f.skipped ? 1 : 0) << ","
             << f.packets_received
             << std::endl;
    }
    
    file.close();
    
    NS_LOG_INFO("FramePlayoutManager: Frame trace exported to " << filename);
    std::cout << "[FramePlayoutManager] Frame trace exported to " << filename << std::endl;
}

void FramePlayoutManager::PrintStatistics() {
    std::cout << "\n========== Frame Playout Statistics ==========" << std::endl;
    std::cout << "Total frames:     " << total_frames_ << std::endl;
    std::cout << "Completed frames: " << completed_frames_ << std::endl;
    std::cout << "Skipped frames:   " << skipped_frames_ << std::endl;
    std::cout << "On-time frames:   " << on_time_frames_ << std::endl;
    
    if (total_frames_ > 0) {
        double completion_rate = 100.0 * completed_frames_ / total_frames_;
        double skip_rate = 100.0 * skipped_frames_ / total_frames_;
        double on_time_rate = completed_frames_ > 0 ? 
                              100.0 * on_time_frames_ / completed_frames_ : 0;
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Completion rate:  " << completion_rate << "%" << std::endl;
        std::cout << "Skip rate:        " << skip_rate << "%" << std::endl;
        std::cout << "On-time rate:     " << on_time_rate << "%" << std::endl;
    }
    
    std::cout << "Playout delay:    " << playout_delay_.GetMilliSeconds() << "ms" << std::endl;
    std::cout << "Keyframes recorded: " << keyframe_ids_.size() << std::endl;
    std::cout << "==============================================" << std::endl;
}

void FramePlayoutManager::Reset() {
    frames_.clear();
    keyframe_ids_.clear();
    current_playback_frame_ = 0;
    total_frames_ = 0;
    completed_frames_ = 0;
    skipped_frames_ = 0;
    on_time_frames_ = 0;
    skip_frame_pending_ = false;
    skip_target_frame_id_ = UINT32_MAX;
    
    NS_LOG_INFO("FramePlayoutManager: State reset");
}

} // namespace ns3

