// #include <iostream>
// #include "ns3/webrtc-tag.h"
// #include "ns3/log.h"
// #include "ns3/core-module.h"
// namespace ns3{
// size_t varint_length(uint64_t number){
//     int64_t next=number;
//     size_t key=0;
//     if(next){
//         do{
//             next=next/128;
//             key++;
//         }while(next>0);
//     }
//     return key;
// }
// TypeId WebrtcTag::GetTypeId (void){
//    static TypeId tid = TypeId ("ns3::WebrtcTag")
//      .SetParent<Tag> ()
//      .AddConstructor<WebrtcTag> ()
//      .AddAttribute ("Time",
//                     "time stamp",
//                     EmptyAttributeValue (),
//                     MakeUintegerAccessor (&WebrtcTag::GetTime),
//                     MakeUintegerChecker<uint64_t> ())
//      .AddAttribute ("Number",
//                     "sequence number",
//                     EmptyAttributeValue (),
//                     MakeUintegerAccessor (&WebrtcTag::GetSequence),
//                     MakeUintegerChecker<uint64_t> ())                    
//    ;
//    return tid;    
// }
// TypeId WebrtcTag::GetInstanceTypeId (void) const{
//     return GetTypeId ();
// } 
// uint32_t WebrtcTag::GetSerializedSize (void) const {
//     return varint_length(seq_)+varint_length(time_);
// }
// void WebrtcTag::Serialize (TagBuffer i) const{
//     VarintEncode(i,seq_);
//     VarintEncode(i,time_);
// }
// void WebrtcTag::Deserialize (TagBuffer i){
//     VarientDecode(i,&seq_);
//     VarientDecode(i,&time_);
// }
// void WebrtcTag::VarintEncode(TagBuffer &i,uint64_t value) const{
//     char first=0;
//     uint64_t next=value;
//     if(next){
//         do{
//             uint8_t byte=0;
//             first=next%128;
//             next=next/128;
//             byte=first;
//             if(next>0){
//                 byte|=128;
//             }
//             i.WriteU8(byte);
//         }while(next>0);
//     }    
// }
// void WebrtcTag::VarientDecode(TagBuffer &i,uint64_t *value){
//     uint64_t remain=0;
//     uint64_t remain_multi=1;
//     uint8_t byte=0;
//     do{
//         byte=i.ReadU8();
//         remain+=(byte&127)*remain_multi;
//         remain_multi*=128;
//     }while(byte&128);
//     *value=remain;
// }
// }

// #include <iostream>
// #include "ns3/webrtc-tag.h"
// #include "ns3/log.h"
// #include "ns3/core-module.h"

// namespace ns3{

// NS_LOG_COMPONENT_DEFINE("WebrtcTag");

// TypeId WebrtcTag::GetTypeId (void){
//    static TypeId tid = TypeId ("ns3::WebrtcTag")
//      .SetParent<Tag> ()
//      .AddConstructor<WebrtcTag> ()
//      .AddAttribute ("Time",
//                     "time stamp",
//                     EmptyAttributeValue (),
//                     MakeUintegerAccessor (&WebrtcTag::GetTime),
//                     MakeUintegerChecker<uint64_t> ())
//      .AddAttribute ("Number",
//                     "sequence number",
//                     EmptyAttributeValue (),
//                     MakeUintegerAccessor (&WebrtcTag::GetSequence),
//                     MakeUintegerChecker<uint64_t> ())                    
//    ;
//    return tid;    
// }

// TypeId WebrtcTag::GetInstanceTypeId (void) const{
//     return GetTypeId ();
// } 

// uint32_t WebrtcTag::GetSerializedSize (void) const {
//     // 固定大小：2个 uint64_t = 16字节
//     return sizeof(uint64_t) * 2;
// }

// void WebrtcTag::Serialize (TagBuffer i) const{
//     // 使用固定长度写入
//     i.WriteU64(seq_);
//     i.WriteU64(time_);
    
//     NS_LOG_DEBUG("WebrtcTag Serialize - seq: " << seq_ << ", time: " << time_);
// }

// void WebrtcTag::Deserialize (TagBuffer i){
//     // 使用固定长度读取
//     seq_ = i.ReadU64();
//     time_ = i.ReadU64();
    
//     NS_LOG_DEBUG("WebrtcTag Deserialize - seq: " << seq_ << ", time: " << time_);
// }

// void WebrtcTag::Print (std::ostream &os) const {
//     os << "WebrtcTag [seq=" << seq_ << ", time=" << time_ << "]";
// }

// // 删除 VarintEncode 和 VarientDecode 方法
// // 或者保留但不要使用

// } // namespace ns3


#include <iostream>
#include "ns3/webrtc-tag.h"
#include "ns3/log.h"
#include "ns3/core-module.h"

namespace ns3{

NS_LOG_COMPONENT_DEFINE("WebrtcTag");

TypeId WebrtcTag::GetTypeId (void){
   static TypeId tid = TypeId ("ns3::WebrtcTag")
     .SetParent<Tag> ()
     .AddConstructor<WebrtcTag> ()
     .AddAttribute ("Time",
                    "time stamp",
                    EmptyAttributeValue (),
                    MakeUintegerAccessor (&WebrtcTag::GetTime),
                    MakeUintegerChecker<uint64_t> ())
     .AddAttribute ("Number",
                    "sequence number",
                    EmptyAttributeValue (),
                    MakeUintegerAccessor (&WebrtcTag::GetSequence),
                    MakeUintegerChecker<uint64_t> ())    
     .AddAttribute ("FrameDeadline",
                    "frame deadline",
                    EmptyAttributeValue (),
                    MakeUintegerAccessor (&WebrtcTag::GetFrameDeadline),
                    MakeUintegerChecker<uint64_t> ())
   ;
   return tid;    
}

TypeId WebrtcTag::GetInstanceTypeId (void) const{
    return GetTypeId ();
} 

uint32_t WebrtcTag::GetSerializedSize (void) const {
    return sizeof(uint64_t) * 2;  // 16字节
}

uint32_t WebrtcTag::GetFrameDeadline(void) const{

void WebrtcTag::Serialize (TagBuffer i) const{
    i.WriteU64(seq_);
    i.WriteU64(time_);
}

void WebrtcTag::Deserialize (TagBuffer i){
    seq_ = i.ReadU64();
    time_ = i.ReadU64();
}

void WebrtcTag::Print (std::ostream &os) const {
    os << "WebrtcTag [seq=" << seq_ << ", time=" << time_ << "]";
}

} // namespace ns3