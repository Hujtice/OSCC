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
      next_playout_frame_id_(0),
      last_played_frame_id_(UINT32_MAX),
      total_frames_(0),
      completed_frames_(0),
      on_time_frames_(0),
      skip_frame_enabled_(false),
      skip_frame_pending_(false),
      skip_target_frame_id_(UINT32_MAX),
      recovery_frame_timeout_(MilliSeconds(500)),  // 默认500ms恢复帧超时
      locked_recovery_frame_(UINT32_MAX) {
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

void FramePlayoutManager::SetSkipFrameEnabled(bool enabled) {
    skip_frame_enabled_ = enabled;
    NS_LOG_INFO("FramePlayoutManager: Skip frame logic " << (enabled ? "ENABLED" : "DISABLED"));
    std::cout << "[FramePlayoutManager] Skip frame logic " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
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

    // 如果启用了跳帧逻辑，检查是否需要丢弃包
    if (skip_frame_enabled_ && skip_frame_pending_) {
        // 严格模式：丢弃所有包，直到遇到目标帧（或更新的）且必须是关键帧
        if (frame_id < skip_target_frame_id_) {
            // 丢弃旧帧
            return;
        }

        // 对于 frame_id >= target，我们必须确保它是关键帧才能恢复
        if (info.is_first_packet) {
            if (!info.is_keyframe) {
                // 不是关键帧，丢弃
                std::cout << "[FramePlayoutManager] Dropping intermediate Frame " << frame_id
                          << " (waiting for KeyFrame >= " << skip_target_frame_id_ << ")" << std::endl;
                return;
            }

            // 是关键帧！检查是否有未完成的锁定恢复帧
            if (locked_recovery_frame_ != UINT32_MAX && locked_recovery_frame_ != frame_id) {
                // 有一个不同的恢复帧被锁定，检查它是否完成
                auto prev_it = frames_.find(locked_recovery_frame_);
                if (prev_it != frames_.end() && !prev_it->second.is_complete) {
                    // 之前的恢复帧未完成，标记为跳过
                    std::cout << "[FramePlayoutManager] Previous recovery frame " << locked_recovery_frame_
                              << " incomplete (packets=" << prev_it->second.packets_received
                              << "), marking as skipped due to new KeyFrame " << frame_id << std::endl;
                    prev_it->second.skipped = true;
                }
                // 取消之前的超时
                CancelRecoveryTimeout();
            }

            // 锁定新的恢复帧
            std::cout << "[FramePlayoutManager] KeyFrame " << frame_id
                      << " first packet received, locking as recovery frame" << std::endl;
            skip_target_frame_id_ = frame_id;

            // 启动恢复帧超时计时器
            locked_recovery_frame_ = frame_id;
            ScheduleRecoveryTimeout(frame_id);
        } else {
            // 非首包处理
            auto it = frames_.find(frame_id);
            if (it != frames_.end()) {
                // 帧已存在，检查是否是关键帧
                if (!it->second.is_keyframe) {
                    return;
                }
            } else if (frame_id == locked_recovery_frame_) {
                // 帧不存在，但这是锁定的恢复帧的后续包
                // 可能是第一个包丢失了，但后续包到达了
                // 创建帧并继续接收（等待重传的第一个包来标记为关键帧）
                NS_LOG_DEBUG("FramePlayoutManager: Receiving non-first packet for locked recovery frame "
                             << frame_id << " (first packet may be lost)");
            } else {
                // 帧不存在且不是锁定的恢复帧，丢弃
                return;
            }
        }
    }

    // 获取或创建帧统计
    FrameStatistics& frame = GetOrCreateFrame(frame_id);

    // 如果帧已经完成（收到了marker bit），忽略后续包（可能是重传）
    if (frame.is_complete) {
        NS_LOG_DEBUG("FramePlayoutManager: Frame " << frame_id
                     << " already complete, ignoring late packet seq=" << info.seq);
        return;
    }

    // 去重检查：如果这个seq已经收到过，跳过
    if (frame.received_seqs.find(info.seq) != frame.received_seqs.end()) {
        NS_LOG_DEBUG("FramePlayoutManager: Duplicate packet seq=" << info.seq
                     << " for frame " << frame_id << ", ignoring");
        return;
    }
    // 记录已接收的seq
    frame.received_seqs.insert(info.seq);

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

        // 如果baseline已建立且帧还没有deadline，立即计算并调度检查
        // 这确保即使帧永远不完成，也会在deadline时触发跳帧
        if (baseline_established_ && frame.playout_deadline == Seconds(0)) {
            double offset = (double)frame_id / fps_;
            frame.playout_deadline = first_frame_playout_time_ + Seconds(offset);

            NS_LOG_DEBUG("FramePlayoutManager: Frame " << frame_id
                         << " deadline set on first packet: "
                         << frame.playout_deadline.GetMilliSeconds() << "ms");

            // 调度deadline检查（针对可能永远不完成的帧）
            if (skip_frame_enabled_ && !frame.is_complete) {
                ScheduleDeadlineCheck(frame_id, frame.playout_deadline);
            }
        }
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

        NS_LOG_INFO("FramePlayoutManager: Frame 0 baseline established at " << now.GetMilliSeconds() << "ms");

        // 更新所有其他帧的截止时间
        for (auto& pair : frames_) {
            if (pair.first != 0) {
                double offset = (double)pair.first / fps_;
                pair.second.playout_deadline = first_frame_playout_time_ + Seconds(offset);

                // 如果开启了跳帧逻辑，调度检查
                if (skip_frame_enabled_ && !pair.second.is_complete) {
                    ScheduleDeadlineCheck(pair.first, pair.second.playout_deadline);
                }
            }
        }

        // 尝试播放
        TryPlayNextFrame();
    } else if (skip_frame_enabled_ && skip_frame_pending_ && frame_id >= skip_target_frame_id_) {
        // 跳帧恢复：关键帧到达
        if (frame.is_keyframe) {
            // 取消恢复帧超时计时器
            CancelRecoveryTimeout();
            locked_recovery_frame_ = UINT32_MAX;

            // 重置时间线
            Time playout_time = frame.receive_complete_time;
            first_frame_playout_time_ = playout_time - Seconds((double)frame_id / fps_);
            baseline_established_ = true;

            skip_frame_pending_ = false;
            // 更新下一个播放帧ID为恢复帧
            next_playout_frame_id_ = frame_id;

            NS_LOG_INFO("FramePlayoutManager: Timeline RESET at frame " << frame_id << " (KeyFrame recovered)");
            std::cout << "[FramePlayoutManager] Timeline RESET at frame " << frame_id
                      << " (KeyFrame recovered). next_playout_frame_id=" << next_playout_frame_id_ << std::endl;

            // 更新当前帧deadline
            frame.playout_deadline = playout_time;

            // 更新后续所有帧的截止时间
            for (auto& pair : frames_) {
                if (pair.first > frame_id) {
                    double offset = (double)pair.first / fps_;
                    pair.second.playout_deadline = first_frame_playout_time_ + Seconds(offset);

                    if (skip_frame_enabled_ && !pair.second.is_complete) {
                        ScheduleDeadlineCheck(pair.first, pair.second.playout_deadline);
                    }
                }
            }

            // 尝试播放
            TryPlayNextFrame();
        }
    } else if (baseline_established_) {
        // 正常帧完成
        double offset = (double)frame_id / fps_;
        frame.playout_deadline = first_frame_playout_time_ + Seconds(offset);

        // 尝试按序播放
        TryPlayNextFrame();
    } else {
        // Frame 0还没到，暂时无法确定截止时间
        frame.playout_deadline = Seconds(0);
    }

    // 触发帧完成回调
    if (frame_complete_callback_) {
        frame_complete_callback_(frame);
    }
}

void FramePlayoutManager::TryPlayNextFrame() {
    if (!baseline_established_) return;

    Time now = Simulator::Now();

    // 循环尝试播放连续的帧
    while (true) {
        auto it = frames_.find(next_playout_frame_id_);

        if (it == frames_.end()) {
            // 下一帧还没到，等待
            break;
        }

        FrameStatistics& frame = it->second;

        if (!frame.is_complete) {
            // 帧还没完成，检查是否超时
            if (skip_frame_enabled_ && now >= frame.playout_deadline) {
                // 超时了，触发跳帧
                std::cout << "[FramePlayoutManager] Frame " << next_playout_frame_id_
                          << " incomplete at deadline! Triggering SKIP." << std::endl;

                frame.skipped = true;
                skip_target_frame_id_ = next_playout_frame_id_ + 1;
                skip_frame_pending_ = true;

                // 通知发送端请求关键帧
                if (skip_frame_callback_) {
                    skip_frame_callback_(skip_target_frame_id_);
                }

                // 清除所有 >= skip_target 的非关键帧
                auto erase_it = frames_.upper_bound(next_playout_frame_id_);
                while (erase_it != frames_.end()) {
                    if (erase_it->second.is_keyframe) {
                        ++erase_it;
                        continue;
                    }
                    total_frames_--;
                    if (erase_it->second.is_complete) {
                        completed_frames_--;
                        if (erase_it->second.played_on_time) {
                            on_time_frames_--;
                        }
                    }
                    erase_it = frames_.erase(erase_it);
                }
                break;
            }
            // 还没超时，等待
            break;
        }

        if (frame.is_played) {
            // 已经播放过了，跳到下一帧
            next_playout_frame_id_++;
            continue;
        }

        // 播放这一帧
        PlayFrame(next_playout_frame_id_);
        next_playout_frame_id_++;
    }
}

void FramePlayoutManager::PlayFrame(uint32_t frame_id) {
    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return;

    FrameStatistics& frame = it->second;
    if (frame.is_played) return;

    Time now = Simulator::Now();
    frame.is_played = true;
    frame.actual_playout_time = now;

    // 判断是否按时播放
    frame.played_on_time = (frame.receive_complete_time <= frame.playout_deadline);
    if (frame.played_on_time) {
        on_time_frames_++;
    } else if (skip_frame_enabled_ && !skip_frame_pending_) {
        // 帧完成但超时了，需要触发跳帧
        // 根据逻辑：申请第i+1帧作为关键帧重发，清除缓存中i+1及之后的所有帧
        std::cout << "[FramePlayoutManager] Frame " << frame_id
                  << " completed but missed deadline! recv=" << frame.receive_complete_time.GetMilliSeconds()
                  << "ms, deadline=" << frame.playout_deadline.GetMilliSeconds()
                  << "ms. Triggering SKIP." << std::endl;

        frame.skipped = true;
        skip_target_frame_id_ = frame_id + 1;
        skip_frame_pending_ = true;

        // 通知发送端请求关键帧
        if (skip_frame_callback_) {
            skip_frame_callback_(skip_target_frame_id_);
        }

        // 清除所有 > frame_id 的帧（包括关键帧，因为需要重新接收）
        auto erase_it = frames_.upper_bound(frame_id);
        while (erase_it != frames_.end()) {
            std::cout << "[FramePlayoutManager] Clearing cached frame " << erase_it->first
                      << " (keyframe=" << erase_it->second.is_keyframe << ")" << std::endl;
            total_frames_--;
            if (erase_it->second.is_complete) {
                completed_frames_--;
                if (erase_it->second.played_on_time) {
                    on_time_frames_--;
                }
            }
            erase_it = frames_.erase(erase_it);
        }
    }

    last_played_frame_id_ = frame_id;

    NS_LOG_INFO("FramePlayoutManager: Played frame " << frame_id
                << " at " << now.GetMilliSeconds() << "ms"
                << ", deadline=" << frame.playout_deadline.GetMilliSeconds() << "ms"
                << ", on_time=" << frame.played_on_time);
}

void FramePlayoutManager::ScheduleDeadlineCheck(uint32_t frame_id, Time deadline) {
    if (!skip_frame_enabled_) return;

    Time now = Simulator::Now();
    Time delay = deadline - now;
    if (delay.IsNegative()) delay = Seconds(0);

    // 只对未完成的帧调度
    auto it = frames_.find(frame_id);
    if (it != frames_.end() && it->second.is_complete) return;

    Simulator::Schedule(delay, &FramePlayoutManager::CheckFrameDeadline, this, frame_id);
}

void FramePlayoutManager::CheckFrameDeadline(uint32_t frame_id) {
    if (!skip_frame_enabled_) return;

    // 如果已经在跳帧过程中，不处理
    if (skip_frame_pending_) return;

    auto it = frames_.find(frame_id);
    if (it == frames_.end()) return;

    FrameStatistics& frame = it->second;

    // 如果帧已完成或已播放，不处理
    if (frame.is_complete || frame.is_played) return;

    Time now = Simulator::Now();

    // 检查是否超时（确保deadline有效，即非零）
    if (frame.playout_deadline > Seconds(0) && now >= frame.playout_deadline) {
        // 超时，触发跳帧
        NS_LOG_INFO("FramePlayoutManager: Frame " << frame_id << " missed deadline!");
        std::cout << "[FramePlayoutManager] Frame " << frame_id
                  << " missed deadline at " << now.GetMilliSeconds()
                  << "ms (deadline=" << frame.playout_deadline.GetMilliSeconds() << "ms)"
                  << ", next_playout_frame_id=" << next_playout_frame_id_ << std::endl;

        frame.skipped = true;

        // 跳帧目标：从当前帧的下一帧开始
        skip_target_frame_id_ = frame_id + 1;
        skip_frame_pending_ = true;

        // 更新 next_playout_frame_id_ 以跳过当前不完整的帧
        if (frame_id >= next_playout_frame_id_) {
            next_playout_frame_id_ = frame_id + 1;
        }

        // 通知发送端请求关键帧
        if (skip_frame_callback_) {
            skip_frame_callback_(skip_target_frame_id_);
        }

        // 清除所有 > frame_id 的非关键帧
        auto erase_it = frames_.upper_bound(frame_id);
        while (erase_it != frames_.end()) {
            if (erase_it->second.is_keyframe) {
                ++erase_it;
                continue;
            }
            std::cout << "[FramePlayoutManager] Clearing cached frame " << erase_it->first
                      << " due to skip from frame " << frame_id << std::endl;
            total_frames_--;
            if (erase_it->second.is_complete) {
                completed_frames_--;
                if (erase_it->second.played_on_time) {
                    on_time_frames_--;
                }
            }
            erase_it = frames_.erase(erase_it);
        }
    }
}

uint32_t FramePlayoutManager::FindNextKeyFrame(uint32_t from_frame_id) {
    auto it = std::upper_bound(keyframe_ids_.begin(), keyframe_ids_.end(), from_frame_id);

    if (it != keyframe_ids_.end()) {
        return *it;
    }

    // 在帧映射中搜索
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
         << "receive_complete_time_ms,playout_deadline_ms,"
         << "is_keyframe,is_complete,played_on_time,packets_received,skipped"
         << std::endl;

    // 按frame_id排序输出
    for (const auto& pair : frames_) {
        const FrameStatistics& f = pair.second;

        // 跳过未完成的恢复帧（这些帧不应该出现在trace中）
        // 只有被跳过且未完成的关键帧才会被过滤
        // 正常跳过的帧（超时但完成的）仍然记录
        if (f.skipped && !f.is_complete && f.is_keyframe) {
            std::cout << "[FramePlayoutManager] Excluding incomplete recovery keyframe "
                      << f.frame_id << " from trace" << std::endl;
            continue;
        }

        // 处理 receive_complete_time_ms 输出逻辑
        double recv_complete_ms = f.receive_complete_time.GetMilliSeconds();

        // 只有在以下情况才标记为-1：
        // 1. 帧未完成接收 (!f.is_complete)
        // 2. 帧被显式跳过 (f.skipped)
        if (!f.is_complete || f.skipped) {
            recv_complete_ms = -1;
        }

        // 计算有效的 played_on_time 值：
        // 只有帧完成且未被跳过且按时完成，才能算作按时播放
        // 未完成的帧或被跳过的帧，played_on_time 必须为 false
        bool effective_on_time = f.is_complete && !f.skipped && f.played_on_time;

        file << f.frame_id << ","
             << f.frame_size << ","
             << f.send_time.GetMilliSeconds() << ","
             << f.first_packet_recv_time.GetMilliSeconds() << ","
             << recv_complete_ms << ","
             << f.playout_deadline.GetMilliSeconds() << ","
             << (f.is_keyframe ? 1 : 0) << ","
             << (f.is_complete ? 1 : 0) << ","
             << (effective_on_time ? 1 : 0) << ","
             << f.packets_received << ","
             << (f.skipped ? 1 : 0)
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
    std::cout << "Last played frame: " << last_played_frame_id_ << std::endl;
    std::cout << "Next playout frame: " << next_playout_frame_id_ << std::endl;

    if (total_frames_ > 0) {
        double completion_rate = 100.0 * completed_frames_ / total_frames_;
        double on_time_rate = completed_frames_ > 0 ?
                              100.0 * on_time_frames_ / completed_frames_ : 0;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Completion rate:  " << completion_rate << "%" << std::endl;
        std::cout << "On-time rate:     " << on_time_rate << "%" << std::endl;
    }

    std::cout << "Keyframes recorded: " << keyframe_ids_.size() << std::endl;
    std::cout << "==============================================" << std::endl;
}

void FramePlayoutManager::Reset() {
    // 取消恢复帧超时
    CancelRecoveryTimeout();

    frames_.clear();
    keyframe_ids_.clear();
    next_playout_frame_id_ = 0;
    last_played_frame_id_ = UINT32_MAX;
    total_frames_ = 0;
    completed_frames_ = 0;
    on_time_frames_ = 0;
    baseline_established_ = false;
    first_frame_playout_time_ = Seconds(0);
    skip_frame_pending_ = false;
    skip_target_frame_id_ = UINT32_MAX;
    locked_recovery_frame_ = UINT32_MAX;

    NS_LOG_INFO("FramePlayoutManager: State reset");
}

void FramePlayoutManager::ScheduleRecoveryTimeout(uint32_t frame_id) {
    // 取消之前的超时事件
    CancelRecoveryTimeout();

    Time now = Simulator::Now();
    Time timeout_time = now + recovery_frame_timeout_;

    NS_LOG_INFO("FramePlayoutManager: Scheduling recovery timeout for frame " << frame_id
                << " at " << timeout_time.GetMilliSeconds() << "ms");

    recovery_timeout_event_ = Simulator::Schedule(
        recovery_frame_timeout_,
        &FramePlayoutManager::CheckRecoveryFrameTimeout,
        this,
        frame_id);
}

void FramePlayoutManager::CancelRecoveryTimeout() {
    if (recovery_timeout_event_.IsRunning()) {
        Simulator::Cancel(recovery_timeout_event_);
        NS_LOG_DEBUG("FramePlayoutManager: Recovery timeout cancelled");
    }
}

void FramePlayoutManager::CheckRecoveryFrameTimeout(uint32_t frame_id) {
    // 检查是否仍在等待这个恢复帧
    if (!skip_frame_pending_ || locked_recovery_frame_ != frame_id) {
        return;
    }

    auto it = frames_.find(frame_id);
    if (it == frames_.end()) {
        // 帧不存在，放弃
        AbandonRecoveryFrame(frame_id);
        return;
    }

    FrameStatistics& frame = it->second;

    // 如果帧已完成，不需要处理
    if (frame.is_complete) {
        return;
    }

    // 帧未完成，超时了
    Time now = Simulator::Now();
    NS_LOG_INFO("FramePlayoutManager: Recovery frame " << frame_id
                << " TIMEOUT at " << now.GetMilliSeconds() << "ms"
                << ", packets_received=" << frame.packets_received);

    std::cout << "[FramePlayoutManager] Recovery frame " << frame_id
              << " TIMEOUT! packets_received=" << frame.packets_received
              << ", abandoning and requesting next keyframe" << std::endl;

    AbandonRecoveryFrame(frame_id);
}

void FramePlayoutManager::AbandonRecoveryFrame(uint32_t frame_id) {
    // 标记当前恢复帧为跳过
    auto it = frames_.find(frame_id);
    if (it != frames_.end()) {
        it->second.skipped = true;
    }

    // 请求下一个关键帧
    uint32_t next_target = frame_id + 1;

    NS_LOG_INFO("FramePlayoutManager: Abandoning recovery frame " << frame_id
                << ", requesting keyframe >= " << next_target);

    std::cout << "[FramePlayoutManager] Abandoning incomplete recovery frame " << frame_id
              << ", requesting new keyframe >= " << next_target << std::endl;

    // 更新跳帧目标
    skip_target_frame_id_ = next_target;
    locked_recovery_frame_ = UINT32_MAX;

    // 通知发送端请求新的关键帧
    if (skip_frame_callback_) {
        skip_frame_callback_(next_target);
    }

    // 清除当前不完整的恢复帧和后续非关键帧
    auto erase_it = frames_.find(frame_id);
    while (erase_it != frames_.end()) {
        // 保留已完成的关键帧（可能是更新的恢复帧）
        if (erase_it->second.is_keyframe && erase_it->second.is_complete && erase_it->first > frame_id) {
            ++erase_it;
            continue;
        }

        std::cout << "[FramePlayoutManager] Clearing frame " << erase_it->first
                  << " (keyframe=" << erase_it->second.is_keyframe
                  << ", complete=" << erase_it->second.is_complete << ")" << std::endl;

        total_frames_--;
        if (erase_it->second.is_complete) {
            completed_frames_--;
            if (erase_it->second.played_on_time) {
                on_time_frames_--;
            }
        }
        erase_it = frames_.erase(erase_it);
    }
}

} // namespace ns3
