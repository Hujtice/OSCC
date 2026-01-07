#pragma once
#include <stdint.h>
#include "ns3/tag.h"

namespace ns3{

// 帧包信息结构（用于传递给 FramePlayoutManager）
struct FramePacketInfo {
    uint64_t seq;              // 包序列号
    uint64_t send_time_ms;     // 发送时间(ms)
    uint32_t frame_id;         // 帧ID
    uint32_t rtp_timestamp;    // RTP时间戳
    uint32_t packet_size;      // 包大小
    uint8_t is_keyframe;       // 是否关键帧
    uint8_t is_first_packet;   // 是否帧的第一个包
    uint8_t is_last_packet;    // 是否帧的最后一个包
    
    FramePacketInfo()
        : seq(0), send_time_ms(0), frame_id(0), rtp_timestamp(0),
          packet_size(0), is_keyframe(0), is_first_packet(0), is_last_packet(0) {}
};

class WebrtcTag : public Tag {
public:
    WebrtcTag()
        : seq_(0), time_(0), frame_id_(0), rtp_timestamp_(0),
          is_keyframe_(0), is_first_packet_(0), is_last_packet_(0) {}
    
    WebrtcTag(uint64_t seq, uint64_t time)
        : seq_(seq), time_(time), frame_id_(0), rtp_timestamp_(0),
          is_keyframe_(0), is_first_packet_(0), is_last_packet_(0) {}
    
    // 扩展构造函数：包含帧信息
    WebrtcTag(uint64_t seq, uint64_t time, uint32_t frame_id, uint32_t rtp_timestamp,
              uint8_t is_keyframe, uint8_t is_first_packet, uint8_t is_last_packet)
        : seq_(seq), time_(time), frame_id_(frame_id), rtp_timestamp_(rtp_timestamp),
          is_keyframe_(is_keyframe), is_first_packet_(is_first_packet), 
          is_last_packet_(is_last_packet) {}
    
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const override;
    virtual uint32_t GetSerializedSize(void) const override;
    virtual void Serialize(TagBuffer i) const override;
    virtual void Deserialize(TagBuffer i) override;
    virtual void Print(std::ostream &os) const override;
    
    // 原有接口
    uint64_t GetSequence() const { return seq_; }
    void Sequence(uint64_t seq) { seq_ = seq; }
    uint64_t GetTime() const { return time_; }
    void Time(uint64_t now) { time_ = now; }
    
    // 新增帧信息接口
    uint32_t GetFrameId() const { return frame_id_; }
    void SetFrameId(uint32_t frame_id) { frame_id_ = frame_id; }
    
    uint32_t GetRtpTimestamp() const { return rtp_timestamp_; }
    void SetRtpTimestamp(uint32_t ts) { rtp_timestamp_ = ts; }
    
    uint8_t IsKeyframe() const { return is_keyframe_; }
    void SetKeyframe(uint8_t is_key) { is_keyframe_ = is_key; }
    
    uint8_t IsFirstPacket() const { return is_first_packet_; }
    void SetFirstPacket(uint8_t is_first) { is_first_packet_ = is_first; }
    
    uint8_t IsLastPacket() const { return is_last_packet_; }
    void SetLastPacket(uint8_t is_last) { is_last_packet_ = is_last; }
    
    // 获取完整的帧包信息
    FramePacketInfo GetFramePacketInfo(uint32_t packet_size = 0) const {
        FramePacketInfo info;
        info.seq = seq_;
        info.send_time_ms = time_;
        info.frame_id = frame_id_;
        info.rtp_timestamp = rtp_timestamp_;
        info.packet_size = packet_size;
        info.is_keyframe = is_keyframe_;
        info.is_first_packet = is_first_packet_;
        info.is_last_packet = is_last_packet_;
        return info;
    }
    
    // 兼容旧代码的接口
    uint32_t GetFrameDeadline() const { return 0; } // 废弃，保持兼容
    
private:
    uint64_t seq_;             // 包序列号
    uint64_t time_;            // 发送时间(ms)
    uint32_t frame_id_;        // 帧ID（基于RTP timestamp计算）
    uint32_t rtp_timestamp_;   // RTP时间戳
    uint8_t is_keyframe_;      // 是否关键帧 (1=关键帧, 0=非关键帧)
    uint8_t is_first_packet_;  // 是否帧的第一个包
    uint8_t is_last_packet_;   // 是否帧的最后一个包（marker bit）
};

} // namespace ns3
