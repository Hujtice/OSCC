#include <iostream>
#include "ns3/webrtc-tag.h"
#include "ns3/log.h"
#include "ns3/core-module.h"

namespace ns3{

NS_LOG_COMPONENT_DEFINE("WebrtcTag");

TypeId WebrtcTag::GetTypeId(void){
   static TypeId tid = TypeId("ns3::WebrtcTag")
     .SetParent<Tag>()
     .AddConstructor<WebrtcTag>()
     .AddAttribute("Time",
                   "time stamp",
                   EmptyAttributeValue(),
                   MakeUintegerAccessor(&WebrtcTag::GetTime),
                   MakeUintegerChecker<uint64_t>())
     .AddAttribute("Number",
                   "sequence number",
                   EmptyAttributeValue(),
                   MakeUintegerAccessor(&WebrtcTag::GetSequence),
                   MakeUintegerChecker<uint64_t>())
     .AddAttribute("FrameId",
                   "frame id",
                   EmptyAttributeValue(),
                   MakeUintegerAccessor(&WebrtcTag::GetFrameId),
                   MakeUintegerChecker<uint32_t>())
     .AddAttribute("RtpTimestamp",
                   "RTP timestamp",
                   EmptyAttributeValue(),
                   MakeUintegerAccessor(&WebrtcTag::GetRtpTimestamp),
                   MakeUintegerChecker<uint32_t>())
   ;
   return tid;    
}

TypeId WebrtcTag::GetInstanceTypeId(void) const{
    return GetTypeId();
} 

uint32_t WebrtcTag::GetSerializedSize(void) const {
    // 2 * uint64_t (seq_, time_) = 16 bytes
    // 2 * uint32_t (frame_id_, rtp_timestamp_) = 8 bytes
    // 3 * uint8_t (is_keyframe_, is_first_packet_, is_last_packet_) = 3 bytes
    // Total: 27 bytes
    return sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2 + sizeof(uint8_t) * 3;
}

void WebrtcTag::Serialize(TagBuffer i) const{
    i.WriteU64(seq_);
    i.WriteU64(time_);
    i.WriteU32(frame_id_);
    i.WriteU32(rtp_timestamp_);
    i.WriteU8(is_keyframe_);
    i.WriteU8(is_first_packet_);
    i.WriteU8(is_last_packet_);
    
    NS_LOG_DEBUG("WebrtcTag Serialize - seq: " << seq_ << ", time: " << time_ 
                 << ", frame_id: " << frame_id_ << ", rtp_ts: " << rtp_timestamp_
                 << ", keyframe: " << (int)is_keyframe_ 
                 << ", first: " << (int)is_first_packet_ 
                 << ", last: " << (int)is_last_packet_);
}

void WebrtcTag::Deserialize(TagBuffer i){
    seq_ = i.ReadU64();
    time_ = i.ReadU64();
    frame_id_ = i.ReadU32();
    rtp_timestamp_ = i.ReadU32();
    is_keyframe_ = i.ReadU8();
    is_first_packet_ = i.ReadU8();
    is_last_packet_ = i.ReadU8();
    
    NS_LOG_DEBUG("WebrtcTag Deserialize - seq: " << seq_ << ", time: " << time_
                 << ", frame_id: " << frame_id_ << ", rtp_ts: " << rtp_timestamp_
                 << ", keyframe: " << (int)is_keyframe_
                 << ", first: " << (int)is_first_packet_
                 << ", last: " << (int)is_last_packet_);
}

void WebrtcTag::Print(std::ostream &os) const {
    os << "WebrtcTag [seq=" << seq_ << ", time=" << time_ 
       << ", frame_id=" << frame_id_ << ", rtp_ts=" << rtp_timestamp_
       << ", keyframe=" << (int)is_keyframe_ 
       << ", first=" << (int)is_first_packet_ 
       << ", last=" << (int)is_last_packet_ << "]";
}

} // namespace ns3
