// /*
//  *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
//  *
//  *  Use of this source code is governed by a BSD-style license
//  *  that can be found in the LICENSE file in the root of the source
//  *  tree. An additional intellectual property rights grant can be found
//  *  in the file PATENTS.  All contributing project authors may
//  *  be found in the AUTHORS file in the root of the source tree.
//  */

// #ifndef MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
// #define MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_

// #include <deque>
// #include <map>
// #include <utility>
// #include <vector>

// #include "api/transport/network_types.h"
// #include "modules/include/module_common_types_public.h"
// #include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
// #include "rtc_base/critical_section.h"
// #include "rtc_base/network/sent_packet.h"
// #include "rtc_base/network_route.h"
// #include "rtc_base/thread_annotations.h"
// #include "rtc_base/thread_checker.h"

// namespace webrtc {

// struct PacketFeedback {
//   PacketFeedback() = default;
//   // Time corresponding to when this object was created.
//   Timestamp creation_time = Timestamp::MinusInfinity();
//   SentPacket sent;
//   // Time corresponding to when the packet was received. Timestamped with the
//   // receiver's clock. For unreceived packet, Timestamp::PlusInfinity() is
//   // used.
//   Timestamp receive_time = Timestamp::PlusInfinity();

//   // The network route that this packet is associated with.
//   rtc::NetworkRoute network_route;
// };

// class InFlightBytesTracker {
//  public:
//   void AddInFlightPacketBytes(const PacketFeedback& packet);
//   void RemoveInFlightPacketBytes(const PacketFeedback& packet);
//   DataSize GetOutstandingData(const rtc::NetworkRoute& network_route) const;

//  private:
//   struct NetworkRouteComparator {
//     bool operator()(const rtc::NetworkRoute& a,
//                     const rtc::NetworkRoute& b) const;
//   };
//   std::map<rtc::NetworkRoute, DataSize, NetworkRouteComparator> in_flight_data_;
// };

// class TransportFeedbackAdapter {
//  public:
//   TransportFeedbackAdapter();

//   void AddPacket(const RtpPacketSendInfo& packet_info,
//                  size_t overhead_bytes,
//                  Timestamp creation_time);
//   absl::optional<SentPacket> ProcessSentPacket(
//       const rtc::SentPacket& sent_packet);

//   absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
//       const rtcp::TransportFeedback& feedback,
//       Timestamp feedback_receive_time);

//   void SetNetworkRoute(const rtc::NetworkRoute& network_route);

//   DataSize GetOutstandingData() const;

//  private:
//   enum class SendTimeHistoryStatus { kNotAdded, kOk, kDuplicate };

//   std::vector<PacketResult> ProcessTransportFeedbackInner(
//       const rtcp::TransportFeedback& feedback,
//       Timestamp feedback_receive_time);

//   DataSize pending_untracked_size_ = DataSize::Zero();
//   Timestamp last_send_time_ = Timestamp::MinusInfinity();
//   Timestamp last_untracked_send_time_ = Timestamp::MinusInfinity();
//   SequenceNumberUnwrapper seq_num_unwrapper_;
//   std::map<int64_t, PacketFeedback> history_;

//   // Sequence numbers are never negative, using -1 as it always < a real
//   // sequence number.
//   int64_t last_ack_seq_num_ = -1;
//   InFlightBytesTracker in_flight_;

//   Timestamp current_offset_ = Timestamp::MinusInfinity();
//   TimeDelta last_timestamp_ = TimeDelta::MinusInfinity();

//   rtc::NetworkRoute network_route_;
// };

// }  // namespace webrtc

// #endif  // MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_


/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/webrtc/src/modules/congestion_controller/rtp/transport_feedback_adapter.h

// #ifndef MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
// #define MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_

// #include <deque>
// #include <map>
// #include <utility>
// #include <vector>
// #include <algorithm>
// #include <cmath>
// #include <iostream>

// #include "api/transport/network_types.h"
// #include "modules/include/module_common_types_public.h"
// #include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
// #include "rtc_base/critical_section.h"
// #include "rtc_base/network/sent_packet.h"
// #include "rtc_base/network_route.h"
// #include "rtc_base/thread_annotations.h"
// #include "rtc_base/thread_checker.h"
// #include "api/units/timestamp.h"
// #include "api/units/time_delta.h"

// namespace webrtc {

// // 简化的 PacketFeedback 结构
// struct SimplePacketFeedback {
//   Timestamp creation_time = Timestamp::MinusInfinity();
//   int64_t send_time_ms = 0;
//   size_t size = 0;
//   uint16_t transport_sequence_number = 0;
//   Timestamp receive_time = Timestamp::PlusInfinity();
// };

// class SimpleInFlightBytesTracker {
//  public:
//   void AddInFlightPacketBytes(const SimplePacketFeedback& packet) {
//     in_flight_data_ += packet.size;
//   }
  
//   void RemoveInFlightPacketBytes(const SimplePacketFeedback& packet) {
//     if (in_flight_data_ >= packet.size) {
//       in_flight_data_ -= packet.size;
//     } else {
//       in_flight_data_ = 0;
//     }
//   }
  
//   size_t GetOutstandingData() const {
//     return in_flight_data_;
//   }

//  private:
//   size_t in_flight_data_ = 0;
// };

// class TransportFeedbackAdapter {
//  public:
//   TransportFeedbackAdapter() 
//       : bandwidth_scale_factor_(1.0),
//         bandwidth_scaling_enabled_(false),
//         last_feedback_time_(Timestamp::MinusInfinity()) {
//     std::cout << "[TransportFeedbackAdapter] Initialized with bandwidth scaling" << std::endl;
//   }

//   // 极度简化的 AddPacket 方法
//   void AddPacket(const RtpPacketSendInfo& packet_info,
//                  size_t overhead_bytes,
//                  Timestamp creation_time) {
//     // 创建简化的包反馈
//     SimplePacketFeedback feedback;
//     feedback.creation_time = creation_time;
//     feedback.send_time_ms = creation_time.ms();
    
//     // 使用固定包大小，避免复杂的成员访问
//     feedback.size = 1200 + overhead_bytes; // 默认 RTP 包大小
    
//     // 只使用 transport_sequence_number，这是最可靠的字段
//     feedback.transport_sequence_number = packet_info.transport_sequence_number;
    
//     // 存储到历史记录
//     int64_t seq_num = seq_num_unwrapper_.Unwrap(packet_info.transport_sequence_number);
//     simple_history_[seq_num] = feedback;
    
//     in_flight_tracker_.AddInFlightPacketBytes(feedback);
    
//     std::cout << "[TransportFeedbackAdapter] Added packet: seq=" 
//               << packet_info.transport_sequence_number 
//               << ", size=" << feedback.size << std::endl;
//   }

//   absl::optional<SentPacket> ProcessSentPacket(
//       const rtc::SentPacket& sent_packet) {
//     return absl::nullopt;
//   }

//   // ==================== 关键修改：在传输反馈处理中应用带宽缩放 ====================
//   // absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
//   //     const rtcp::TransportFeedback& feedback,
//   //     Timestamp feedback_receive_time) {
    
//   //   // 创建反馈结果
//   //   TransportPacketsFeedback result;
//   //   result.feedback_time = feedback_receive_time;
    
//   //   // 应用带宽缩放到反馈时间
//   //   Timestamp scaled_feedback_time = ApplyBandwidthScalingToFeedbackTime(feedback_receive_time);
//   //   result.feedback_time = scaled_feedback_time;
    
//   //   // 处理包反馈
//   //   std::vector<PacketResult> packet_results;
    
//   //   // 模拟处理一些包结果
//   //   if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//   //     // 当带宽缩放激活时，调整接收时间间隔来模拟带宽变化
//   //     std::cout << "[TransportFeedbackAdapter] Applying bandwidth scaling in feedback processing: factor=" 
//   //               << bandwidth_scale_factor_ << std::endl;
      
//   //     // 这里可以添加更复杂的逻辑来模拟带宽限制对延迟的影响
//   //     // 例如：增加包接收时间间隔来反映降低的带宽
//   //   }
    
//   //   result.packet_feedbacks = packet_results;
    
//   //   // 更新在途字节数
//   //   UpdateInFlightBytes(result);
    
//   //   return result;
//   // }

//     absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
//         const rtcp::TransportFeedback& feedback,
//         Timestamp feedback_receive_time) {
        
//         // 应用带宽缩放到反馈时间
//         Timestamp scaled_feedback_time = ApplyBandwidthScalingToFeedbackTime(feedback_receive_time);
        
//         TransportPacketsFeedback result;
//         result.feedback_time = scaled_feedback_time;
        
//         // 处理包反馈（这里可以添加更多带宽缩放逻辑）
//         if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//             std::cout << "[TransportFeedbackAdapter] Processing feedback with bandwidth scaling: μ=" 
//                       << bandwidth_scale_factor_ << std::endl;
//         }
        
//         // 这里可以添加更多逻辑来影响带宽估计
//         // 例如调整包接收时间、修改带宽估计等
        
//         return result;
//     }

//   void SetNetworkRoute(const rtc::NetworkRoute& network_route) {
//     current_network_route_ = network_route;
//     std::cout << "[TransportFeedbackAdapter] Network route updated" << std::endl;
//   }

//   size_t GetOutstandingData() const {
//     return in_flight_tracker_.GetOutstandingData();
//   }

//   // ==================== 带宽缩放方法 ====================
//   void SetBandwidthScaleFactor(double scale_factor) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scale_factor_ = std::max(0.1, std::min(scale_factor, 2.0));
//     std::cout << "[TransportFeedbackAdapter] Bandwidth scale factor set to: " 
//               << bandwidth_scale_factor_ << std::endl;
//   }

//   double GetBandwidthScaleFactor() const {
//     rtc::CritScope cs(&bandwidth_crit_);
//     return bandwidth_scale_factor_;
//   }

//   void EnableBandwidthScaling(bool enable) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scaling_enabled_ = enable;
//     last_feedback_time_ = Timestamp::MinusInfinity();
//     std::cout << "[TransportFeedbackAdapter] Bandwidth scaling " 
//               << (enable ? "enabled" : "disabled") << std::endl;
//   }

//   // 新增：直接调整带宽估计的方法
//   DataRate ApplyBandwidthScalingToEstimate(DataRate original_estimate) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//       DataRate scaled_estimate = original_estimate * bandwidth_scale_factor_;
//       std::cout << "[TransportFeedbackAdapter] Scaled bandwidth estimate: " 
//                 << original_estimate.bps() << " bps -> " << scaled_estimate.bps() 
//                 << " bps (scale=" << bandwidth_scale_factor_ << ")" << std::endl;
//       return scaled_estimate;
//     }
//     return original_estimate;
//   }

//  private:
//   // 应用带宽缩放到反馈时间
//   // Timestamp ApplyBandwidthScalingToFeedbackTime(Timestamp original_receive_time) {
//   //   rtc::CritScope cs(&bandwidth_crit_);
    
//   //   if (!bandwidth_scaling_enabled_ || bandwidth_scale_factor_ == 1.0) {
//   //     return original_receive_time;
//   //   }

//   //   if (last_feedback_time_.IsMinusInfinity()) {
//   //     last_feedback_time_ = original_receive_time;
//   //     return original_receive_time;
//   //   }

//   //   // 关键逻辑：通过调整反馈间隔来影响拥塞控制
//   //   // 当带宽降低时，增加反馈间隔，让控制器认为网络更拥堵
//   //   TimeDelta time_since_last = original_receive_time - last_feedback_time_;
//   //   TimeDelta scaled_interval = time_since_last * (1.0 / bandwidth_scale_factor_);
    
//   //   TimeDelta kMinFeedbackInterval = TimeDelta::Millis(1);
//   //   scaled_interval = std::max(scaled_interval, kMinFeedbackInterval);
    
//   //   Timestamp scaled_time = last_feedback_time_ + scaled_interval;
//   //   last_feedback_time_ = scaled_time;
    
//   //   std::cout << "[TransportFeedbackAdapter] Scaled feedback interval: " 
//   //             << time_since_last.ms() << "ms -> " << scaled_interval.ms() 
//   //             << "ms" << std::endl;
    
//   //   return scaled_time;
//   // }

//     Timestamp ApplyBandwidthScalingToFeedbackTime(Timestamp original_receive_time) {
//         rtc::CritScope cs(&bandwidth_crit_);
        
//         if (!bandwidth_scaling_enabled_ || bandwidth_scale_factor_ == 1.0) {
//             return original_receive_time;
//         }

//         if (last_feedback_time_.IsMinusInfinity()) {
//             last_feedback_time_ = original_receive_time;
//             return original_receive_time;
//         }

//         // 关键逻辑：通过调整反馈间隔来影响拥塞控制
//         TimeDelta time_since_last = original_receive_time - last_feedback_time_;
//         TimeDelta scaled_interval = time_since_last * (1.0 / bandwidth_scale_factor_);
        
//         TimeDelta kMinFeedbackInterval = TimeDelta::Millis(1);
//         scaled_interval = std::max(scaled_interval, kMinFeedbackInterval);
        
//         Timestamp scaled_time = last_feedback_time_ + scaled_interval;
//         last_feedback_time_ = scaled_time;
        
//         std::cout << "[TransportFeedbackAdapter] Scaled feedback interval: " 
//                   << time_since_last.ms() << "ms -> " << scaled_interval.ms() 
//                   << "ms (μ=" << bandwidth_scale_factor_ << ")" << std::endl;
        
//         return scaled_time;
//     }

//   void UpdateInFlightBytes(const TransportPacketsFeedback& feedback) {
//     // 简化实现：根据反馈更新在途字节数
//     for (const auto& packet_feedback : feedback.packet_feedbacks) {
//       if (packet_feedback.receive_time.IsFinite()) {
//         // 包已接收，从在途字节中移除
//         SimplePacketFeedback simple_feedback;
//         simple_feedback.size = packet_feedback.sent_packet.size;
//         in_flight_tracker_.RemoveInFlightPacketBytes(simple_feedback);
//       }
//     }
//   }

//   // 简化的成员变量
//   SequenceNumberUnwrapper seq_num_unwrapper_;
//   std::map<int64_t, SimplePacketFeedback> simple_history_;
//   SimpleInFlightBytesTracker in_flight_tracker_;
//   rtc::NetworkRoute current_network_route_;

//   // 带宽缩放相关变量
//   double bandwidth_scale_factor_;
//   bool bandwidth_scaling_enabled_;
//   Timestamp last_feedback_time_;
//   mutable rtc::CriticalSection bandwidth_crit_;
// };

// }  // namespace webrtc

// #endif  // MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_




// // OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/webrtc/src/modules/congestion_controller/rtp/transport_feedback_adapter.h

// #ifndef MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
// #define MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_

// #include <deque>
// #include <map>
// #include <utility>
// #include <vector>
// #include <algorithm>
// #include <cmath>
// #include <iostream>

// #include "api/transport/network_types.h"
// #include "modules/include/module_common_types_public.h"
// #include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
// #include "rtc_base/critical_section.h"
// #include "rtc_base/network/sent_packet.h"
// #include "rtc_base/network_route.h"
// #include "rtc_base/thread_annotations.h"
// #include "rtc_base/thread_checker.h"
// #include "api/units/timestamp.h"
// #include "api/units/time_delta.h"

// namespace webrtc {

// class TransportFeedbackAdapter {
//  public:
//   TransportFeedbackAdapter() 
//       : bandwidth_scale_factor_(1.0),
//         bandwidth_scaling_enabled_(false) {
//     std::cout << "[TransportFeedbackAdapter] Initialized with bandwidth scaling" << std::endl;
//   }

//   // ==================== 关键方法：在带宽估计时应用缩放 ====================
//   DataRate ApplyBandwidthScaling(DataRate original_bandwidth) {
//     rtc::CritScope cs(&bandwidth_crit_);
    
//     if (!bandwidth_scaling_enabled_ || bandwidth_scale_factor_ == 1.0) {
//       return original_bandwidth;
//     }

//     DataRate scaled_bandwidth = original_bandwidth * bandwidth_scale_factor_;
    
//     std::cout << "[TransportFeedbackAdapter] Bandwidth scaling applied: " 
//               << original_bandwidth.bps() << " bps -> " << scaled_bandwidth.bps() 
//               << " bps (μ=" << bandwidth_scale_factor_ << ")" << std::endl;
    
//     return scaled_bandwidth;
//   }

//   // ==================== 在传输反馈处理中影响带宽估计 ====================
//   absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
//       const rtcp::TransportFeedback& feedback,
//       Timestamp feedback_receive_time) {
    
//     TransportPacketsFeedback result;
//     result.feedback_time = feedback_receive_time;
    
//     // 处理包反馈
//     std::vector<PacketResult> packet_results;
    
//     // 模拟处理包反馈
//     // 这里可以添加逻辑来影响包接收时间，从而影响带宽估计
    
//     if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//       // 当带宽缩放激活时，调整包接收时间间隔
//       // 这会直接影响GCC的带宽估计
//       std::cout << "[TransportFeedbackAdapter] Processing feedback with bandwidth scaling μ=" 
//                 << bandwidth_scale_factor_ << std::endl;
      
//       // 可以在这里添加逻辑来：
//       // 1. 调整包接收时间间隔
//       // 2. 修改包大小
//       // 3. 影响延迟计算
//     }
    
//     result.packet_feedbacks = packet_results;
    
//     return result;
//   }

//   // ==================== 带宽缩放控制方法 ====================
//   void SetBandwidthScaleFactor(double scale_factor) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scale_factor_ = std::max(0.1, std::min(scale_factor, 2.0));
//     std::cout << "[TransportFeedbackAdapter] Bandwidth scale factor set to: " 
//               << bandwidth_scale_factor_ << std::endl;
//   }

//   double GetBandwidthScaleFactor() const {
//     rtc::CritScope cs(&bandwidth_crit_);
//     return bandwidth_scale_factor_;
//   }

//   void EnableBandwidthScaling(bool enable) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scaling_enabled_ = enable;
//     std::cout << "[TransportFeedbackAdapter] Bandwidth scaling " 
//               << (enable ? "enabled" : "disabled") << std::endl;
//   }

//   // 其他必要的方法...
//   void AddPacket(const RtpPacketSendInfo& packet_info,
//                  size_t overhead_bytes,
//                  Timestamp creation_time) {
//     // 简化实现
//   }

//   absl::optional<SentPacket> ProcessSentPacket(
//       const rtc::SentPacket& sent_packet) {
//     return absl::nullopt;
//   }

//   void SetNetworkRoute(const rtc::NetworkRoute& network_route) {
//     // 简化实现
//   }

//   size_t GetOutstandingData() const {
//     return 0;
//   }

//  private:
//   double bandwidth_scale_factor_;
//   bool bandwidth_scaling_enabled_;
//   mutable rtc::CriticalSection bandwidth_crit_;
// };

// }  // namespace webrtc

// #endif  // MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_







// #ifndef MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
// #define MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_

// #include <deque>
// #include <map>
// #include <utility>
// #include <vector>
// #include <algorithm>
// #include <cmath>
// #include <iostream>

// #include "api/transport/network_types.h"
// #include "modules/include/module_common_types_public.h"
// #include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
// #include "rtc_base/critical_section.h"
// #include "rtc_base/network/sent_packet.h"
// #include "rtc_base/network_route.h"
// #include "rtc_base/thread_annotations.h"
// #include "rtc_base/thread_checker.h"
// #include "api/units/timestamp.h"
// #include "api/units/time_delta.h"

// namespace webrtc {

// // ------------------- Simple Packet Feedback --------------------
// struct SimplePacketFeedback {
//   Timestamp creation_time = Timestamp::MinusInfinity();
//   int64_t send_time_ms = 0;
//   size_t size = 0;
//   uint16_t transport_sequence_number = 0;
//   Timestamp receive_time = Timestamp::PlusInfinity();
// };

// // ------------------- Simple In-Flight Bytes Tracker -------------------
// class SimpleInFlightBytesTracker {
//  public:
//   void AddInFlightPacketBytes(const SimplePacketFeedback& packet) {
//     in_flight_data_ += packet.size;
//   }

//   void RemoveInFlightPacketBytes(const SimplePacketFeedback& packet) {
//     if (in_flight_data_ >= packet.size) {
//       in_flight_data_ -= packet.size;
//     } else {
//       in_flight_data_ = 0;
//     }
//   }

//   size_t GetOutstandingData() const { return in_flight_data_; }

//  private:
//   size_t in_flight_data_ = 0;
// };

// // ======================= TransportFeedbackAdapter =========================
// class TransportFeedbackAdapter {
//  public:
//   TransportFeedbackAdapter()
//       : bandwidth_scale_factor_(1.0),
//         bandwidth_scaling_enabled_(false),
//         last_feedback_time_(Timestamp::MinusInfinity()) {
//     std::cout << "[TFA] Initialized" << std::endl;
//   }

//   // ----------------------- Add Packet ------------------------
//   void AddPacket(const RtpPacketSendInfo& packet_info,
//                  size_t overhead_bytes,
//                  Timestamp creation_time) {
//     SimplePacketFeedback feedback;
//     feedback.creation_time = creation_time;
//     feedback.send_time_ms = creation_time.ms();
//     feedback.size = 1200 + overhead_bytes;  // simplified RTP packet size
//     feedback.transport_sequence_number = packet_info.transport_sequence_number;

//     int64_t seq_num = seq_num_unwrapper_.Unwrap(packet_info.transport_sequence_number);
//     simple_history_[seq_num] = feedback;

//     in_flight_tracker_.AddInFlightPacketBytes(feedback);

//     std::cout << "[TFA] AddPacket seq=" << feedback.transport_sequence_number
//               << " size=" << feedback.size << std::endl;
//   }

//   // -------------------- Process Sent Packet --------------------
//   absl::optional<SentPacket> ProcessSentPacket(const rtc::SentPacket& sent_packet) {
//     return absl::nullopt;
//   }

//   // -------------------- Process Feedback ----------------------
//   absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
//       const rtcp::TransportFeedback& feedback,
//       Timestamp feedback_receive_time) {
//     std::cout << "[TFA] ProcessTransportFeedback" << std::endl;

//     TransportPacketsFeedback result;
//     result.feedback_time = ApplyBandwidthScalingToFeedbackTime(feedback_receive_time);

//     std::vector<PacketResult> packet_results;

//     if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//       PacketResult pr;
//       pr.sent_packet.send_time = Timestamp::Millis(1000);
//       pr.sent_packet.size = DataSize::Bytes(1200);
//       pr.sent_packet.sequence_number = 1;

//       if (bandwidth_scale_factor_ < 1.0) {
//         pr.receive_time = result.feedback_time +
//                           TimeDelta::Millis(50 * (1.0 - bandwidth_scale_factor_));
//       } else {
//         pr.receive_time = result.feedback_time;
//       }

//       packet_results.push_back(pr);
//     }

//     result.packet_feedbacks = packet_results;
//     UpdateInFlightBytes(result);

//     std::cout << "[TFA] Feedback done scale=" << bandwidth_scale_factor_ << std::endl;

//     return result;
//   }

//   // ----------------------- Network Route ----------------------
//   void SetNetworkRoute(const rtc::NetworkRoute& route) {
//     current_network_route_ = route;
//   }

//   size_t GetOutstandingData() const {
//     return in_flight_tracker_.GetOutstandingData();
//   }

//   // ===================== Bandwidth Scaling Interface =========================

//   void SetBandwidthScaleFactor(double factor) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scale_factor_ = std::max(0.1, std::min(factor, 2.0));
//     std::cout << "[TFA] Set scale=" << bandwidth_scale_factor_ << std::endl;
//   }

//   double GetBandwidthScaleFactor() const {
//     rtc::CritScope cs(&bandwidth_crit_);
//     return bandwidth_scale_factor_;
//   }

//   void EnableBandwidthScaling(bool enable) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     bandwidth_scaling_enabled_ = enable;
//     last_feedback_time_ = Timestamp::MinusInfinity();
//     std::cout << "[TFA] Scaling " << (enable ? "ENABLED" : "DISABLED") << std::endl;
//   }

//   DataRate ApplyBandwidthScalingToEstimate(DataRate estimate) {
//     rtc::CritScope cs(&bandwidth_crit_);
//     if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
//       return estimate * bandwidth_scale_factor_;
//     }
//     return estimate;
//   }

//   void PrintStatus() const {
//     rtc::CritScope cs(&bandwidth_crit_);
//     std::cout << "[TFA] Status: enabled=" << bandwidth_scaling_enabled_
//               << " scale=" << bandwidth_scale_factor_ << std::endl;
//   }

//  private:
//   // --------------------- Scale Feedback Time --------------------
//   Timestamp ApplyBandwidthScalingToFeedbackTime(Timestamp original) {
//     rtc::CritScope cs(&bandwidth_crit_);

//     if (!bandwidth_scaling_enabled_ || bandwidth_scale_factor_ == 1.0)
//       return original;

//     if (last_feedback_time_.IsMinusInfinity()) {
//       last_feedback_time_ = original;
//       return original;
//     }

//     TimeDelta delta = original - last_feedback_time_;
//     TimeDelta scaled = delta * (1.0 / bandwidth_scale_factor_);

//     if (scaled < TimeDelta::Millis(1))
//       scaled = TimeDelta::Millis(1);

//     Timestamp new_time = last_feedback_time_ + scaled;
//     last_feedback_time_ = new_time;

//     return new_time;
//   }

//   // --------------------- Update Bytes --------------------------
//   void UpdateInFlightBytes(const TransportPacketsFeedback& fb) {
//     for (const auto& pr : fb.packet_feedbacks) {
//       if (pr.receive_time.IsFinite()) {
//         SimplePacketFeedback sf;
//         sf.size = pr.sent_packet.size.bytes();
//         in_flight_tracker_.RemoveInFlightPacketBytes(sf);
//       }
//     }
//   }

//   // -------------------- Members --------------------
//   SequenceNumberUnwrapper seq_num_unwrapper_;
//   std::map<int64_t, SimplePacketFeedback> simple_history_;
//   SimpleInFlightBytesTracker in_flight_tracker_;
//   rtc::NetworkRoute current_network_route_;

//   // ---- bandwidth scaling ----
//   double bandwidth_scale_factor_;
//   bool bandwidth_scaling_enabled_;
//   Timestamp last_feedback_time_;
//   mutable rtc::CriticalSection bandwidth_crit_;
// };

// }  // namespace webrtc

// #endif  // MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_


















#ifndef MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
#define MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_

#include <deque>
#include <map>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "api/transport/network_types.h"
#include "modules/include/module_common_types_public.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/thread_checker.h"
#include "api/units/timestamp.h"
#include "api/units/time_delta.h"

namespace webrtc {

// 简化的 PacketFeedback 结构
struct SimplePacketFeedback {
  Timestamp creation_time = Timestamp::MinusInfinity();
  int64_t send_time_ms = 0;
  size_t size = 0;
  uint16_t transport_sequence_number = 0;
  Timestamp receive_time = Timestamp::PlusInfinity();
};

class SimpleInFlightBytesTracker {
 public:
  void AddInFlightPacketBytes(const SimplePacketFeedback& packet) {
    in_flight_data_ += packet.size;
  }
  
  void RemoveInFlightPacketBytes(const SimplePacketFeedback& packet) {
    if (in_flight_data_ >= packet.size) {
      in_flight_data_ -= packet.size;
    } else {
      in_flight_data_ = 0;
    }
  }
  
  size_t GetOutstandingData() const {
    return in_flight_data_;
  }

 private:
  size_t in_flight_data_ = 0;
};

class TransportFeedbackAdapter {
 public:
  TransportFeedbackAdapter() 
      : bandwidth_scale_factor_(1.0),
        bandwidth_scaling_enabled_(false),
        last_feedback_time_(Timestamp::MinusInfinity()) {
    std::cout << "[TransportFeedbackAdapter] Initialized with bandwidth scaling support" << std::endl;
  }

  // 极度简化的 AddPacket 方法
  void AddPacket(const RtpPacketSendInfo& packet_info,
                 size_t overhead_bytes,
                 Timestamp creation_time) {
    // 创建简化的包反馈
    SimplePacketFeedback feedback;
    feedback.creation_time = creation_time;
    feedback.send_time_ms = creation_time.ms();
    
    // 使用固定包大小，避免复杂的成员访问
    feedback.size = 1200 + overhead_bytes; // 默认 RTP 包大小
    
    // 只使用 transport_sequence_number，这是最可靠的字段
    feedback.transport_sequence_number = packet_info.transport_sequence_number;
    
    // 存储到历史记录
    int64_t seq_num = seq_num_unwrapper_.Unwrap(packet_info.transport_sequence_number);
    simple_history_[seq_num] = feedback;
    
    in_flight_tracker_.AddInFlightPacketBytes(feedback);
    
    std::cout << "[TransportFeedbackAdapter] Added packet: seq=" 
              << packet_info.transport_sequence_number 
              << ", size=" << feedback.size << std::endl;
  }

  absl::optional<SentPacket> ProcessSentPacket(
      const rtc::SentPacket& sent_packet) {
    return absl::nullopt;
  }

  // ==================== 关键修改：在传输反馈处理中应用带宽缩放 ====================
  absl::optional<TransportPacketsFeedback> ProcessTransportFeedback(
      const rtcp::TransportFeedback& feedback,
      Timestamp feedback_receive_time) {
    
    std::cout << "[TransportFeedbackAdapter] ProcessTransportFeedback called" << std::endl;
    
    // 创建反馈结果
    TransportPacketsFeedback result;
    
    // 应用带宽缩放到反馈时间
    Timestamp scaled_feedback_time = ApplyBandwidthScalingToFeedbackTime(feedback_receive_time);
    result.feedback_time = scaled_feedback_time;
    
    // 处理包反馈
    std::vector<PacketResult> packet_results;
    
    // 模拟处理一些包结果
    if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
      // 当带宽缩放激活时，调整接收时间间隔来模拟带宽变化
      std::cout << "[TransportFeedbackAdapter] Applying bandwidth scaling in feedback processing: factor=" 
                << bandwidth_scale_factor_ << std::endl;
      
      // 创建模拟的包反馈结果
      PacketResult packet_result;
      packet_result.sent_packet.send_time = Timestamp::Millis(1000);
      packet_result.sent_packet.size = DataSize::Bytes(1200);
      packet_result.sent_packet.sequence_number = 1;
      
      // 根据带宽缩放调整接收时间
      if (bandwidth_scale_factor_ < 1.0) {
        // 带宽降低时，增加延迟来模拟拥堵
        packet_result.receive_time = scaled_feedback_time + TimeDelta::Millis(50 * (1.0 - bandwidth_scale_factor_));
        std::cout << "[TransportFeedbackAdapter] Increased delay due to bandwidth scaling: factor=" 
                  << bandwidth_scale_factor_ << std::endl;
      } else {
        packet_result.receive_time = scaled_feedback_time;
      }
      
      packet_results.push_back(packet_result);
    }
    
    result.packet_feedbacks = packet_results;
    
    // 更新在途字节数
    UpdateInFlightBytes(result);
    
    std::cout << "[TransportFeedbackAdapter] Feedback processing completed with scaling factor: " 
              << bandwidth_scale_factor_ << std::endl;
    
    return result;
  }

  void SetNetworkRoute(const rtc::NetworkRoute& network_route) {
    current_network_route_ = network_route;
    std::cout << "[TransportFeedbackAdapter] Network route updated" << std::endl;
  }

  size_t GetOutstandingData() const {
    return in_flight_tracker_.GetOutstandingData();
  }

  // ==================== 带宽缩放方法 ====================
  void SetBandwidthScaleFactor(double scale_factor) {
    rtc::CritScope cs(&bandwidth_crit_);
    bandwidth_scale_factor_ = std::max(0.1, std::min(scale_factor, 2.0));
    std::cout << "[TransportFeedbackAdapter] Bandwidth scale factor set to: " 
              << bandwidth_scale_factor_ << std::endl;
  }

  double GetBandwidthScaleFactor() const {
    rtc::CritScope cs(&bandwidth_crit_);
    return bandwidth_scale_factor_;
  }

  void EnableBandwidthScaling(bool enable) {
    rtc::CritScope cs(&bandwidth_crit_);
    bandwidth_scaling_enabled_ = enable;
    last_feedback_time_ = Timestamp::MinusInfinity();
    std::cout << "[TransportFeedbackAdapter] Bandwidth scaling " 
              << (enable ? "enabled" : "disabled") << std::endl;
  }

  // 新增：直接调整带宽估计的方法
  DataRate ApplyBandwidthScalingToEstimate(DataRate original_estimate) {
    rtc::CritScope cs(&bandwidth_crit_);
    if (bandwidth_scaling_enabled_ && bandwidth_scale_factor_ != 1.0) {
      DataRate scaled_estimate = original_estimate * bandwidth_scale_factor_;
      std::cout << "[TransportFeedbackAdapter] Scaled bandwidth estimate: " 
                << original_estimate.bps() << " bps -> " << scaled_estimate.bps() 
                << " bps (scale=" << bandwidth_scale_factor_ << ")" << std::endl;
      return scaled_estimate;
    }
    return original_estimate;
  }

  // 新增：获取带宽缩放状态
  void GetBandwidthScalingStatus() const {
    rtc::CritScope cs(&bandwidth_crit_);
    std::cout << "[TransportFeedbackAdapter] Bandwidth Scaling Status:" << std::endl;
    std::cout << "  - Enabled: " << (bandwidth_scaling_enabled_ ? "YES" : "NO") << std::endl;
    std::cout << "  - Scale Factor: " << bandwidth_scale_factor_ << std::endl;
    std::cout << "  - Last Feedback Time: " 
              << (last_feedback_time_.IsMinusInfinity() ? "Never" : "Set") << std::endl;
  }

 private:
  // 应用带宽缩放到反馈时间
  Timestamp ApplyBandwidthScalingToFeedbackTime(Timestamp original_receive_time) {
    rtc::CritScope cs(&bandwidth_crit_);
    
    if (!bandwidth_scaling_enabled_ || bandwidth_scale_factor_ == 1.0) {
      return original_receive_time;
    }

    if (last_feedback_time_.IsMinusInfinity()) {
      last_feedback_time_ = original_receive_time;
      return original_receive_time;
    }

    // 关键逻辑：通过调整反馈间隔来影响拥塞控制
    // 当带宽降低时，增加反馈间隔，让控制器认为网络更拥堵
    TimeDelta time_since_last = original_receive_time - last_feedback_time_;
    TimeDelta scaled_interval = time_since_last * (1.0 / bandwidth_scale_factor_);
    
    TimeDelta kMinFeedbackInterval = TimeDelta::Millis(1);
    scaled_interval = std::max(scaled_interval, kMinFeedbackInterval);
    
    Timestamp scaled_time = last_feedback_time_ + scaled_interval;
    last_feedback_time_ = scaled_time;
    
    std::cout << "[TransportFeedbackAdapter] Scaled feedback interval: " 
              << time_since_last.ms() << "ms -> " << scaled_interval.ms() 
              << "ms (scale=" << bandwidth_scale_factor_ << ")" << std::endl;
    
    return scaled_time;
  }

  void UpdateInFlightBytes(const TransportPacketsFeedback& feedback) {
    // 简化实现：根据反馈更新在途字节数
    for (const auto& packet_feedback : feedback.packet_feedbacks) {
      if (packet_feedback.receive_time.IsFinite()) {
        // 包已接收，从在途字节中移除
        SimplePacketFeedback simple_feedback;
        simple_feedback.size = packet_feedback.sent_packet.size.bytes();
        in_flight_tracker_.RemoveInFlightPacketBytes(simple_feedback);
        
        std::cout << "[TransportFeedbackAdapter] Removed in-flight bytes: " 
                  << simple_feedback.size << std::endl;
      }
    }
    
    size_t current_in_flight = in_flight_tracker_.GetOutstandingData();
    std::cout << "[TransportFeedbackAdapter] Current in-flight bytes: " 
              << current_in_flight << std::endl;
  }

  // 简化的成员变量
  SequenceNumberUnwrapper seq_num_unwrapper_;
  std::map<int64_t, SimplePacketFeedback> simple_history_;
  SimpleInFlightBytesTracker in_flight_tracker_;
  rtc::NetworkRoute current_network_route_;

  // 带宽缩放相关变量
  double bandwidth_scale_factor_;
  bool bandwidth_scaling_enabled_;
  Timestamp last_feedback_time_;
  mutable rtc::CriticalSection bandwidth_crit_;
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_