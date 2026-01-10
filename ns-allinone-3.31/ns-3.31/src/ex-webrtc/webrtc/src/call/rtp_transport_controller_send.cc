

// #include "call/rtp_transport_controller_send.h"
// #include <iostream>

// namespace webrtc {

// RtpTransportControllerSend::RtpTransportControllerSend(
//     Clock* clock,
//     RtcEventLog* event_log,
//     NetworkStatePredictorFactoryInterface* predictor_factory,
//     NetworkControllerFactoryInterface* controller_factory,
//     const BitrateConstraints& bitrate_config,
//     std::unique_ptr<ProcessThread> process_thread,
//     TaskQueueFactory* task_queue_factory,
//     const WebRtcKeyValueConfig* trials)
//     : clock_(clock),
//       event_log_(event_log),
//       bitrate_configurator_(bitrate_config),  // 关键：使用参数构造
//       process_thread_(std::move(process_thread)),
//       task_queue_(task_queue_factory->CreateTaskQueue(
//           "RtpTransportControllerSend",
//           TaskQueueFactory::Priority::NORMAL)),
//       // 初始化我们新增的带宽缩放成员
//       bandwidth_scale_factor_(1.0),
//       bandwidth_scaling_enabled_(false) {
//   std::cout << "RtpTransportControllerSend initialized" << std::endl;
// }

// // 只实现绝对必要的方法
// void RtpTransportControllerSend::SetBandwidthScaleFactor(double scale_factor) {
//   bandwidth_scale_factor_ = scale_factor;
// }

// double RtpTransportControllerSend::GetBandwidthScaleFactor() const {
//   return bandwidth_scale_factor_;
// }

// void RtpTransportControllerSend::EnableBandwidthScaling(bool enable) {
//   bandwidth_scaling_enabled_ = enable;
// }

// // 其他方法保持空实现
// void RtpTransportControllerSend::UpdateStreamsConfig() {}
// void RtpTransportControllerSend::MaybeCreateControllers() {}
// void RtpTransportControllerSend::UpdateControlState() {}

// RtpVideoSenderInterface* RtpTransportControllerSend::CreateRtpVideoSender(
//     std::map<uint32_t, RtpState> suspended_ssrcs,
//     const std::map<uint32_t, RtpPayloadState>& states,
//     const RtpConfig& rtp_config,
//     int rtcp_report_interval_ms,
//     Transport* send_transport,
//     const RtpSenderObservers& observers,
//     RtcEventLog* event_log,
//     std::unique_ptr<FecController> fec_controller,
//     const RtpSenderFrameEncryptionConfig& frame_encryption_config,
//     rtc::scoped_refptr<FrameTransformerInterface> frame_transformer) {
//   // 简化实现
//   return nullptr;
// }
// void RtpTransportControllerSend::DestroyRtpVideoSender(
//     RtpVideoSenderInterface* rtp_video_sender) {
//   // 简化实现
// }

// rtc::TaskQueue* RtpTransportControllerSend::GetWorkerQueue() { return &task_queue_; }
// PacketRouter* RtpTransportControllerSend::packet_router() { return &packet_router_; }

// // 其他方法...
// }  // namespace webrtc



// OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/webrtc/src/call/rtp_transport_controller_send.cc

// OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/webrtc/src/call/rtp_transport_controller_send.cc

#include "call/rtp_transport_controller_send.h"
#include <iostream>
#include <algorithm>

namespace webrtc {

RtpTransportControllerSend::RtpTransportControllerSend(
    Clock* clock,
    RtcEventLog* event_log,
    NetworkStatePredictorFactoryInterface* predictor_factory,
    NetworkControllerFactoryInterface* controller_factory,
    const BitrateConstraints& bitrate_config,
    std::unique_ptr<ProcessThread> process_thread,
    TaskQueueFactory* task_queue_factory,
    const WebRtcKeyValueConfig* trials)
    : clock_(clock),
      event_log_(event_log),
      packet_router_(clock),
      bitrate_configurator_(bitrate_config),
      process_thread_(std::move(process_thread)),
      use_task_queue_pacer_(true),
      observer_(nullptr),
      controller_factory_override_(controller_factory),
      process_interval_(TimeDelta::Millis(25)),
      transport_overhead_bytes_per_packet_(0),
      network_available_(true),
      task_queue_(task_queue_factory->CreateTaskQueue(
          "RtpTransportControllerSend",
          TaskQueueFactory::Priority::NORMAL)),
      // 初始化我们新增的带宽缩放成员
      bandwidth_scale_factor_(1.0),
      bandwidth_scaling_enabled_(false),
      last_update_time_(Timestamp::MinusInfinity()) {
  
  std::cout << "[RtpTransportControllerSend] Initialized with bandwidth scaling support" << std::endl;
  
  // 简化初始化过程
  task_queue_.PostTask([this]() {
    std::cout << "[RtpTransportControllerSend] Task queue initialized" << std::endl;
  });
}

RtpTransportControllerSend::~RtpTransportControllerSend() {
  std::cout << "[RtpTransportControllerSend] Destroyed" << std::endl;
}

// ==================== 带宽缩放方法实现 ====================
void RtpTransportControllerSend::SetBandwidthScaleFactor(double scale_factor) {
  std::cout << "[RtpTransportControllerSend] SetBandwidthScaleFactor called: " << scale_factor << std::endl;
  
  double clamped_factor = std::max(0.1, std::min(scale_factor, 2.0));
  
  // 安全地设置值
  bandwidth_scale_factor_ = clamped_factor;
  
  std::cout << "[RtpTransportControllerSend] Bandwidth scale factor set to: " 
            << bandwidth_scale_factor_ << std::endl;
  
  // 确保传输反馈适配器也使用相同的缩放系数
  if (bandwidth_scaling_enabled_) {
    transport_feedback_adapter_.SetBandwidthScaleFactor(clamped_factor);
    std::cout << "[RtpTransportControllerSend] Also set scale factor in transport_feedback_adapter" << std::endl;
  }
}

double RtpTransportControllerSend::GetBandwidthScaleFactor() const {
  return bandwidth_scale_factor_;
}

void RtpTransportControllerSend::EnableBandwidthScaling(bool enable) {
  std::cout << "[RtpTransportControllerSend] EnableBandwidthScaling called: " << enable << std::endl;
  
  bandwidth_scaling_enabled_ = enable;
  transport_feedback_adapter_.EnableBandwidthScaling(enable);
  
  std::cout << "[RtpTransportControllerSend] Bandwidth scaling " 
            << (enable ? "enabled" : "disabled") << std::endl;
            
  if (enable) {
    std::cout << "[RtpTransportControllerSend] Current scale factor: " << bandwidth_scale_factor_ << std::endl;
  }
}

// ==================== 简化其他必要方法 ====================
RtpVideoSenderInterface* RtpTransportControllerSend::CreateRtpVideoSender(
    std::map<uint32_t, RtpState> suspended_ssrcs,
    const std::map<uint32_t, RtpPayloadState>& states,
    const RtpConfig& rtp_config,
    int rtcp_report_interval_ms,
    Transport* send_transport,
    const RtpSenderObservers& observers,
    RtcEventLog* event_log,
    std::unique_ptr<FecController> fec_controller,
    const RtpSenderFrameEncryptionConfig& frame_encryption_config,
    rtc::scoped_refptr<FrameTransformerInterface> frame_transformer) {
  std::cout << "[RtpTransportControllerSend] CreateRtpVideoSender called" << std::endl;
  return nullptr;
}

void RtpTransportControllerSend::DestroyRtpVideoSender(
    RtpVideoSenderInterface* rtp_video_sender) {
  std::cout << "[RtpTransportControllerSend] DestroyRtpVideoSender called" << std::endl;
}

rtc::TaskQueue* RtpTransportControllerSend::GetWorkerQueue() { 
  return &task_queue_; 
}

PacketRouter* RtpTransportControllerSend::packet_router() { 
  return &packet_router_; 
}

NetworkStateEstimateObserver* RtpTransportControllerSend::network_state_estimate_observer() { 
  return this; 
}

TransportFeedbackObserver* RtpTransportControllerSend::transport_feedback_observer() { 
  return this; 
}

RtpPacketSender* RtpTransportControllerSend::packet_sender() { 
  if (task_queue_pacer_) {
    return task_queue_pacer_.get();
  }
  return process_thread_pacer_.get();
}

void RtpTransportControllerSend::SetAllocatedSendBitrateLimits(BitrateAllocationLimits limits) {
  // 简化实现
}

void RtpTransportControllerSend::SetPacingFactor(float pacing_factor) {
  // 简化实现
}

void RtpTransportControllerSend::SetQueueTimeLimit(int limit_ms) {
  // 简化实现
}

StreamFeedbackProvider* RtpTransportControllerSend::GetStreamFeedbackProvider() {
  return nullptr;
}

void RtpTransportControllerSend::RegisterTargetTransferRateObserver(
    TargetTransferRateObserver* observer) {
  observer_ = observer;
}

void RtpTransportControllerSend::OnNetworkRouteChanged(const std::string& transport_name,
                           const rtc::NetworkRoute& network_route) {
  std::cout << "[RtpTransportControllerSend] OnNetworkRouteChanged" << std::endl;
  transport_feedback_adapter_.SetNetworkRoute(network_route);
}

void RtpTransportControllerSend::OnNetworkAvailability(bool network_available) {
  network_available_ = network_available;
}

RtcpBandwidthObserver* RtpTransportControllerSend::GetBandwidthObserver() {
  return this;
}

int64_t RtpTransportControllerSend::GetPacerQueuingDelayMs() const {
  return 0;
}

absl::optional<Timestamp> RtpTransportControllerSend::GetFirstPacketTime() const {
  return absl::nullopt;
}

void RtpTransportControllerSend::EnablePeriodicAlrProbing(bool enable) {
  // 简化实现
}

void RtpTransportControllerSend::OnSentPacket(const rtc::SentPacket& sent_packet) {
  // 简化实现
}

void RtpTransportControllerSend::OnReceivedPacket(const ReceivedPacket& packet_msg) {
  // 简化实现
}

void RtpTransportControllerSend::SetSdpBitrateParameters(const BitrateConstraints& constraints) {
  // 简化实现
}

void RtpTransportControllerSend::SetClientBitratePreferences(const BitrateSettings& preferences) {
  // 简化实现
}

void RtpTransportControllerSend::OnTransportOverheadChanged(
    size_t transport_overhead_bytes_per_packet) {
  transport_overhead_bytes_per_packet_ = transport_overhead_bytes_per_packet;
}

void RtpTransportControllerSend::AccountForAudioPacketsInPacedSender(bool account_for_audio) {
  // 简化实现
}

void RtpTransportControllerSend::IncludeOverheadInPacedSender() {
  // 简化实现
}

// Implements RtcpBandwidthObserver interface
void RtpTransportControllerSend::OnReceivedEstimatedBitrate(uint32_t bitrate) {
  // 简化实现
}

void RtpTransportControllerSend::OnReceivedRtcpReceiverReport(const ReportBlockList& report_blocks,
                                    int64_t rtt,
                                    int64_t now_ms) {
  // 简化实现
}

// Implements TransportFeedbackObserver interface
void RtpTransportControllerSend::OnAddPacket(const RtpPacketSendInfo& packet_info) {
  transport_feedback_adapter_.AddPacket(packet_info, 0, clock_->CurrentTime());
}

void RtpTransportControllerSend::OnTransportFeedback(const rtcp::TransportFeedback& feedback) {
  auto processed_feedback = transport_feedback_adapter_.ProcessTransportFeedback(feedback, clock_->CurrentTime());
  
  if (processed_feedback) {
    std::cout << "[RtpTransportControllerSend] Processed transport feedback with bandwidth scaling: " 
              << bandwidth_scale_factor_ << std::endl;
  }
}

// Implements NetworkStateEstimateObserver interface
void RtpTransportControllerSend::OnRemoteNetworkEstimate(NetworkStateEstimate estimate) {
  // 简化实现
}

// ==================== 私有方法实现 ====================
void RtpTransportControllerSend::MaybeCreateControllers() {
  // 简化实现
}

void RtpTransportControllerSend::UpdateInitialConstraints(TargetRateConstraints new_contraints) {
  // 简化实现
}

void RtpTransportControllerSend::StartProcessPeriodicTasks() {
  // 简化实现
}

void RtpTransportControllerSend::UpdateControllerWithTimeInterval() {
  // 简化实现
}

absl::optional<BitrateConstraints> RtpTransportControllerSend::ApplyOrLiftRelayCap(bool is_relayed) {
  return absl::nullopt;
}

bool RtpTransportControllerSend::IsRelevantRouteChange(const rtc::NetworkRoute& old_route,
                             const rtc::NetworkRoute& new_route) const {
  return true;
}

void RtpTransportControllerSend::UpdateBitrateConstraints(const BitrateConstraints& updated) {
  // 简化实现
}

void RtpTransportControllerSend::UpdateStreamsConfig() {
  // 简化实现
}

void RtpTransportControllerSend::OnReceivedRtcpReceiverReportBlocks(const ReportBlockList& report_blocks,
                                          int64_t now_ms) {
  // 简化实现
}

void RtpTransportControllerSend::PostUpdates(NetworkControlUpdate update) {
  // 简化实现
}

void RtpTransportControllerSend::UpdateControlState() {
  // 简化实现
}

RtpPacketPacer* RtpTransportControllerSend::pacer() {
  if (task_queue_pacer_) {
    return task_queue_pacer_.get();
  }
  return process_thread_pacer_.get();
}

const RtpPacketPacer* RtpTransportControllerSend::pacer() const {
  if (task_queue_pacer_) {
    return task_queue_pacer_.get();
  }
  return process_thread_pacer_.get();
}

}  // namespace webrtc