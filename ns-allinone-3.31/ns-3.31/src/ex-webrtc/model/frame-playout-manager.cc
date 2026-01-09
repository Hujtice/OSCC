#include "frame-playout-manager.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("FramePlayoutManager");

FramePlayoutManager::FramePlayoutManager()
    : playout_delay_(MilliSeconds(300)),  // 默认300ms播放延迟
      fps_(30),
      first_frame_playout_time_(Seconds(0)),
      baseline_established_(false),
      current_playback_frame_(0),
      total_frames_(0),
      completed_frames_(0),
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

void FramePlayoutManager::SetFPS(uint32_t fps) {
    if (fps > 0) {
        fps_ = fps;
        NS_LOG_INFO("FramePlayoutManager: FPS set to " << fps);
        std::cout << "[FramePlayoutManager] FPS set to " << fps << std::endl;
    }
}

void FramePlayoutManager::SetSkipFrameCallback(SkipFrameCallback cb) {
    skip_frame_callback_ = cb;
    NS_LOG_INFO("FramePlayoutManager: Skip frame callback registered");
}

void FramePlayoutManager::SetPacketReceivedCallback(PacketReceivedCallback cb) {
    packet_received_callback_ = cb;
    NS_LOG_INFO("FramePlayoutManager: Packet received callback registered");
}

void FramePlayoutManager::SetFrameCompleteCallback(FrameCompleteCallback cb) {
    frame_complete_callback_ = cb;
    NS_LOG_INFO("FramePlayoutManager: Frame complete callback registered");
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
    
    // 获取或创建帧统计
    FrameStatistics& frame = GetOrCreateFrame(frame_id);
    
    // 更新帧统计
    frame.frame_size += packet_size;
    frame.packets_received++;
    
    // 第一个包到达时
    if (info.is_first_packet || frame.packets_received == 1) {
        frame.send_time = MilliSeconds(info.send_time_ms);
        frame.first_packet_recv_time = now;
        
        NS_LOG_DEBUG("FramePlayoutManager: Frame " << frame_id 
                     << " first packet, send_time=" << frame.send_time.GetMilliSeconds()
                     << "ms");
    }
    
    // 记录关键帧
    if (info.is_keyframe) {
        frame.is_keyframe = true;
        RecordKeyFrame(frame_id);
        NS_LOG_INFO("FramePlayoutManager: Keyframe detected - frame_id=" << frame_id);
    }
    
    // 触发包接收回调
    if (packet_received_callback_) {
        packet_received_callback_(info, frame);
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
    
    if (frame_id == 0) {
        // 第一帧到达，建立基准
        first_frame_playout_time_ = now;
        baseline_established_ = true;
        frame.playout_deadline = now;
        frame.played_on_time = true;
        on_time_frames_++; // Frame 0 always on time by definition
        
        NS_LOG_INFO("FramePlayoutManager: Frame 0 baseline established at " << now.GetMilliSeconds() << "ms");
        
        // 更新所有其他帧的截止时间
        for (auto& pair : frames_) {
            if (pair.first != 0) {
                 double offset = (double)pair.first / fps_;
                 pair.second.playout_deadline = first_frame_playout_time_ + Seconds(offset);
                 
                 // 如果该帧之前已经完成，重新评估其是否按时
                 if (pair.second.is_complete) {
                     bool was_on_time = pair.second.played_on_time;
                     // 按时播放判定：完成时间 <= 截止时间
                     pair.second.played_on_time = (pair.second.receive_complete_time <= pair.second.playout_deadline);
                     
                     if (!was_on_time && pair.second.played_on_time) on_time_frames_++;
                     else if (was_on_time && !pair.second.played_on_time) on_time_frames_--;
                 }
            }
        }
    } else {
        if (baseline_established_) {
             double offset = (double)frame_id / fps_;
             frame.playout_deadline = first_frame_playout_time_ + Seconds(offset);
             frame.played_on_time = (now <= frame.playout_deadline);
             if (frame.played_on_time) on_time_frames_++;
        } else {
            // Frame 0还没到，暂时无法确定截止时间
            frame.playout_deadline = Seconds(0);
            frame.played_on_time = false; 
        }
    }
    
    // 触发帧完成回调
    if (frame_complete_callback_) {
        frame_complete_callback_(frame);
    }
}

void FramePlayoutManager::ScheduleDeadlineCheck(uint32_t frame_id, Time deadline) {
    // 逻辑移除：不再调度超时检查，等待所有帧自然完成（或不完成）
}

void FramePlayoutManager::CheckFrameDeadline(uint32_t frame_id) {
    // 逻辑移除
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
    
    // CSV头 - 移除了rtp_timestamp和skipped
    file << "frame_id,frame_size_bytes,send_time_ms,first_recv_time_ms,"
         << "receive_complete_time_ms,playout_deadline_ms,"
         << "is_keyframe,is_complete,played_on_time,packets_received"
         << std::endl;
    
    // 按frame_id排序输出
    for (const auto& pair : frames_) {
        const FrameStatistics& f = pair.second;
        
        // 处理 receive_complete_time_ms 输出逻辑
        double recv_complete_ms = f.receive_complete_time.GetMilliSeconds();
        
        if (f.frame_id != 0) {
            // 除第一帧外，如果没有在deadline之前收完，显示-1
            // 只要没完成，或者完成时间超过deadline，都显示-1
            // 注意：is_complete为1表示收完了所有包，无论时间。
            // 但显示时间时，题目要求：如果超过deadline，标记为-1。
            if (!f.is_complete) {
                recv_complete_ms = -1;
            } else if (baseline_established_ && f.receive_complete_time > f.playout_deadline) {
                recv_complete_ms = -1;
            }
        } else {
            // 第一帧
            if (!f.is_complete) recv_complete_ms = -1;
        }

        file << f.frame_id << ","
             << f.frame_size << ","
             << f.send_time.GetMilliSeconds() << ","
             << f.first_packet_recv_time.GetMilliSeconds() << ","
             << recv_complete_ms << ","
             << f.playout_deadline.GetMilliSeconds() << ","
             << (f.is_keyframe ? 1 : 0) << ","
             << (f.is_complete ? 1 : 0) << ","
             << (f.played_on_time ? 1 : 0) << ","
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
    std::cout << "On-time frames:   " << on_time_frames_ << std::endl;
    
    if (total_frames_ > 0) {
        double completion_rate = 100.0 * completed_frames_ / total_frames_;
        double on_time_rate = completed_frames_ > 0 ? 
                              100.0 * on_time_frames_ / completed_frames_ : 0;
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Completion rate:  " << completion_rate << "%" << std::endl;
        std::cout << "On-time rate:     " << on_time_rate << "%" << std::endl;
    }
    
    std::cout << "Playout delay:    " << playout_delay_.GetMilliSeconds() << "ms (IGNORED)" << std::endl;
    std::cout << "Keyframes recorded: " << keyframe_ids_.size() << std::endl;
    std::cout << "==============================================" << std::endl;
}

void FramePlayoutManager::Reset() {
    frames_.clear();
    keyframe_ids_.clear();
    current_playback_frame_ = 0;
    total_frames_ = 0;
    completed_frames_ = 0;
    on_time_frames_ = 0;
    baseline_established_ = false;
    first_frame_playout_time_ = Seconds(0);
    skip_frame_pending_ = false;
    skip_target_frame_id_ = UINT32_MAX;
    
    NS_LOG_INFO("FramePlayoutManager: State reset");
}

} // namespace ns3

