//    以码率调节发送速率

#include "webrtc-sender.h"
#include "ns3/log.h"
#include "ns3/socket.h"
#include "ns3/inet-socket-address.h"
#include "ns3/udp-socket-factory.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/sent_packet.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "webrtc-tag.h"
#include <random>
#include <typeinfo>
#include <cxxabi.h>
#include <dlfcn.h>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WebrtcSender");

// static const definition
const double WebrtcSender::BANDWIDTH_TOLERANCE = 0.05;

namespace {
    const uint32_t kIpv4HeaderSize = 20;
    const int32_t kTraceInterval = 25;
    constexpr char kDummyTransportName[] = "dummy";
}

// ==================== 构造函数 ====================
WebrtcSender::WebrtcSender(WebrtcSessionManager *manager){
    std::cout << "[DEBUG] WebrtcSender constructor called" << std::endl;

    m_manager = manager;
    m_clock = manager->time_controller_->GetClock();
    m_manager->RegisterSenderTransport(this, false);
    m_client = m_manager->sender_client_;
    m_call = m_client->GetCall();
    m_initial_time = Simulator::Now().GetMilliSeconds();

    m_current_estimated_bandwidth = 0;
    m_current_scaled_bandwidth = 0;
    m_socket = nullptr;
    m_bindPort = 0;
    m_peerPort = 0;
    m_context = 0;
    m_lastTraceTime = 0;
    m_packetOverhead = 0;
    m_running = false;
    m_seq = 0;
    m_bandwidth_scale_factor = 1.0; // 默认1.0

    m_last_applied_scaled_bw = 0;
    m_pending_scaled_bw = 0;
    m_has_pending_bw = false;
    
    // OSCC动态调速用：保存基准带宽（避免累积效应）
    m_base_gcc_bandwidth = 0;
    m_last_applied_mu = 1.0;
    m_zeroCount = 0;

    std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
}

WebrtcSender::~WebrtcSender(){}

// ==================== 带宽缩放接口 ====================
void WebrtcSender::SetBandwidthScaleFactor(double factor) {
    if (factor > 0 && factor <= 1.3) {
        m_bandwidth_scale_factor = factor;
        NS_LOG_INFO("WebrtcSender: Initial bandwidth scale factor set to: " << factor);
    } else {
        NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor << ", using default 1.0");
        m_bandwidth_scale_factor = 1;
    }
}

double WebrtcSender::GetBandwidthScaleFactor() const {
    return m_bandwidth_scale_factor;
}

double WebrtcSender::GetCurrentBandwidthScaleFactor() const {
    return m_bandwidth_scale_factor;
}

void WebrtcSender::SetBandwidthScaleFactorDirect(double factor) {
    if (factor > 0 && factor <= 1.3) {
        double old_mu = m_bandwidth_scale_factor;
        m_bandwidth_scale_factor = factor;
        std::cout << "=== Direct Bandwidth Scaling ===\n"
                  << "Time: " << Simulator::Now().GetSeconds() << "s\n"
                  << "μ changed: " << old_mu << " -> " << factor << "\n"
                  << "Bandwidth: " << (factor * 100) << "% of original\n"
                  << "=================================" << std::endl;
        NS_LOG_INFO("WebrtcSender: Bandwidth scale factor set directly to: " << factor);
        // Apply to controller after short delay
        Simulator::Schedule(Time(MilliSeconds(100)), &WebrtcSender::ApplyPendingBitrate, this);
    } else {
        NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor);
    }
}

// OSCC集成：运行时动态更新μ值并立即应用到GCC
void WebrtcSender::UpdateMuDynamic(double new_mu) {
    // 验证μ值范围（OSCC规定的范围是[0.5, 1.5]）
    if (new_mu >= 0.5 && new_mu <= 1.5) {
        double old_mu = m_bandwidth_scale_factor;
        
        // 只有当μ值发生变化时才更新
        if (std::abs(new_mu - old_mu) > 0.001) {
            m_bandwidth_scale_factor = new_mu;
            
            NS_LOG_INFO("WebrtcSender: OSCC dynamic mu update: " << old_mu << " -> " << new_mu);
            std::cout << "[OSCC-Sender] Dynamic mu update at " << Simulator::Now().GetSeconds() 
                      << "s: " << old_mu << " -> " << new_mu << std::endl;
            
            // ========== 关键修复：直接调用 ApplyBandwidthScalingToController ==========
            // 而不是调用 ApplyPendingBitrate（需要设置 m_has_pending_bw 等参数）
            // ApplyBandwidthScalingToController 会读取最新的 m_bandwidth_scale_factor 并应用
            if (m_context != 0) {
                Simulator::ScheduleWithContext(m_context, Time(MilliSeconds(1)), 
                    &WebrtcSender::ApplyBandwidthScalingToController, this);
            } else if (GetNode()) {
                Simulator::ScheduleWithContext(GetNode()->GetId(), Time(MilliSeconds(1)), 
                    &WebrtcSender::ApplyBandwidthScalingToController, this);
            } else {
                Simulator::Schedule(Time(MilliSeconds(1)), 
                    &WebrtcSender::ApplyBandwidthScalingToController, this);
            }
            
            std::cout << "[OSCC-Sender] Scheduled ApplyBandwidthScalingToController with new mu=" 
                      << new_mu << std::endl;
        }
    } else {
        NS_LOG_WARN("WebrtcSender: OSCC mu value out of range [0.5, 1.5]: " << new_mu);
    }
}

uint32_t WebrtcSender::GetScaledBandwidth(uint32_t original_bw) {
    double current_mu = GetCurrentBandwidthScaleFactor();
    uint32_t scaled_bw = static_cast<uint32_t>(original_bw * current_mu);

    // simple transient zero protection: if GCC value is zero transiently keep last non-zero for a few rounds
    if (original_bw == 0) {
        m_zeroCount++;
        if (m_zeroCount < 5 && m_current_estimated_bandwidth > 0) {
            // use last estimate
            scaled_bw = static_cast<uint32_t>(m_current_estimated_bandwidth * current_mu);
        }
    } else {
        m_zeroCount = 0;
    }

    if (original_bw != m_current_estimated_bandwidth || scaled_bw != m_current_scaled_bandwidth) {
        NS_LOG_INFO("WebrtcSender: Bandwidth scaling - Original: " << original_bw
                     << " bps * μ=" << current_mu
                     << " = Scaled: " << scaled_bw << " bps");
        m_current_estimated_bandwidth = original_bw;
        m_current_scaled_bandwidth = scaled_bw;
    }
    return scaled_bw;
}

void WebrtcSender::SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
    m_traceScaledBw = cb;
    NS_LOG_INFO("WebrtcSender: Scaled bandwidth trace callback set");
}

void WebrtcSender::SetBwTraceFuc(TraceBandwidth cb) {
    m_traceBw = cb;
    NS_LOG_INFO("WebrtcSender: Bandwidth trace callback set");
}

// ==================== 绑定和配置 ====================
void WebrtcSender::Bind(uint16_t port) {
    NS_LOG_INFO("WebrtcSender: Binding to port " << port);
    m_bindPort = port;
    if (!m_socket) {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        auto local = InetSocketAddress{Ipv4Address::GetAny(), port};
        NS_ASSERT(m_socket->Bind(local) == 0);
        m_socket->SetRecvCallback(MakeCallback(&WebrtcSender::RecvPacket, this));
        NS_LOG_INFO("WebrtcSender: Successfully bound to port " << port);
    }
}

void WebrtcSender::ConfigurePeer(Ipv4Address addr, uint16_t port) {
    m_peerIp = addr;
    m_peerPort = port;
    NS_LOG_INFO("WebrtcSender: Configured peer - IP: " << addr << ", Port: " << port);
}

InetSocketAddress WebrtcSender::GetLocalAddress() {
    if (m_socket) {
        Address addr;
        m_socket->GetSockName(addr);
        return InetSocketAddress::ConvertFrom(addr);
    }
    return InetSocketAddress(Ipv4Address::GetAny(), 0);
}

// ==================== 帧信息解析辅助方法 ====================

// 根据RTP时间戳获取或创建帧ID
uint32_t WebrtcSender::GetOrCreateFrameId(uint32_t rtp_timestamp) {
    auto it = m_rtpTimestampToFrameId.find(rtp_timestamp);
    if (it != m_rtpTimestampToFrameId.end()) {
        return it->second;
    }
    // 新的RTP时间戳，分配新的帧ID
    uint32_t frame_id = m_nextFrameId++;
    m_rtpTimestampToFrameId[rtp_timestamp] = frame_id;
    
    // 清理旧的映射（保留最近100个帧的映射）
    if (m_rtpTimestampToFrameId.size() > 100) {
        auto oldest = m_rtpTimestampToFrameId.begin();
        m_rtpTimestampToFrameId.erase(oldest);
    }
    
    return frame_id;
}

// 检测VP8关键帧
// VP8 RTP Payload Descriptor格式:
// +-+-+-+-+-+-+-+-+
// |X|R|N|S|R| PID | (REQUIRED)
// +-+-+-+-+-+-+-+-+
// X: Extension bit
// R: Reserved
// N: Non-reference frame
// S: Start of VP8 partition
// PID: Partition index
//
// VP8 Payload Header (关键帧第一字节的bit 0为0):
// +-+-+-+-+-+-+-+-+
// |Size0|H| VER |P|
// +-+-+-+-+-+-+-+-+
// P=0 表示关键帧，P=1 表示inter frame
bool WebrtcSender::IsVP8KeyFrame(const uint8_t* payload, size_t payload_length) {
    if (payload_length < 1) return false;
    
    // VP8 payload descriptor 第一字节
    uint8_t desc = payload[0];
    bool has_extension = (desc & 0x80) != 0;  // X bit
    bool is_start = (desc & 0x10) != 0;       // S bit (start of partition)
    
    // 只有在分区开始时才能判断关键帧
    if (!is_start) {
        return false;  // 不是分区开始，无法确定
    }
    
    int header_size = 1;
    
    if (has_extension && payload_length > 1) {
        uint8_t ext = payload[1];
        header_size++;
        if ((ext & 0x80) != 0) {  // I bit (picture ID present)
            header_size++;
            if (payload_length > header_size && (payload[2] & 0x80) != 0) {
                // 16-bit picture ID
                header_size++;
            }
        }
        if ((ext & 0x40) != 0) {  // L bit (TL0PICIDX present)
            header_size++;
        }
        if ((ext & 0x20) != 0 || (ext & 0x10) != 0) {  // T or K bit
            header_size++;
        }
    }
    
    if (payload_length <= (size_t)header_size) {
        return false;
    }
    
    // VP8 uncompressed data chunk - 检查 P bit (bit 0)
    // P=0 表示关键帧
    uint8_t vp8_header = payload[header_size];
    return (vp8_header & 0x01) == 0;
}

// 解析RTP包获取帧信息
RtpFrameInfo WebrtcSender::ParseRtpPacketInfo(const uint8_t* packet, size_t length) {
    RtpFrameInfo info;
    
    if (length < 12) {
        // RTP头至少12字节
        NS_LOG_WARN("WebrtcSender: RTP packet too short: " << length);
        return info;
    }
    
    // RTP Header格式:
    // 0                   1                   2                   3
    // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |V=2|P|X|  CC   |M|     PT      |       sequence number         |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                           timestamp                           |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |           synchronization source (SSRC) identifier            |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    
    uint8_t first_byte = packet[0];
    uint8_t second_byte = packet[1];
    
    // 检查RTP版本 (应该是2)
    uint8_t version = (first_byte >> 6) & 0x03;
    if (version != 2) {
        NS_LOG_WARN("WebrtcSender: Invalid RTP version: " << (int)version);
        return info;
    }
    
    bool has_padding = (first_byte & 0x20) != 0;
    bool has_extension = (first_byte & 0x10) != 0;
    uint8_t csrc_count = first_byte & 0x0F;
    
    // Marker bit (第二字节的最高位) - 表示帧的最后一个包
    bool marker = (second_byte & 0x80) != 0;
    // payload_type 保留用于将来扩展
    (void)(second_byte & 0x7F);  // suppress unused variable warning
    
    // RTP timestamp (字节4-7, big-endian)
    uint32_t rtp_timestamp = (packet[4] << 24) | (packet[5] << 16) | 
                             (packet[6] << 8) | packet[7];
    
    // 计算payload偏移
    size_t header_size = 12 + csrc_count * 4;
    if (has_extension && length > header_size + 4) {
        // Extension header: 2 bytes profile + 2 bytes length (in 32-bit words)
        uint16_t ext_length = (packet[header_size + 2] << 8) | packet[header_size + 3];
        header_size += 4 + ext_length * 4;
    }
    
    if (header_size >= length) {
        NS_LOG_WARN("WebrtcSender: RTP header exceeds packet length");
        return info;
    }
    
    // 获取payload
    const uint8_t* payload = packet + header_size;
    size_t payload_length = length - header_size;
    if (has_padding && payload_length > 0) {
        uint8_t padding_size = packet[length - 1];
        if (padding_size < payload_length) {
            payload_length -= padding_size;
        }
    }
    
    // 填充帧信息
    info.rtp_timestamp = rtp_timestamp;
    info.frame_id = GetOrCreateFrameId(rtp_timestamp);
    info.is_last_packet = marker ? 1 : 0;
    
    // 检测是否是帧的第一个包（RTP timestamp变化时）
    if (rtp_timestamp != m_lastRtpTimestamp) {
        info.is_first_packet = 1;
        m_lastRtpTimestamp = rtp_timestamp;
    } else {
        info.is_first_packet = 0;
    }
    
    // VP8 关键帧检测 (payload_type 通常是 96-127 范围内的动态类型)
    // WebRTC 默认使用 VP8，payload_type 通常是 96 或其他配置值
    if (payload_length > 0 && info.is_first_packet) {
        info.is_keyframe = IsVP8KeyFrame(payload, payload_length) ? 1 : 0;
    } else {
        // 非第一个包继承之前的关键帧状态（通过frame_id查找）
        // 简化处理：只在第一个包确定关键帧状态
        info.is_keyframe = 0;
    }
    
    NS_LOG_INFO("WebrtcSender: Parsed RTP - frame_id=" << info.frame_id 
                 << ", rtp_ts=" << info.rtp_timestamp
                 << ", keyframe=" << (int)info.is_keyframe
                 << ", first=" << (int)info.is_first_packet
                 << ", last=" << (int)info.is_last_packet);
    
    return info;
}

// ==================== RTP/RTCP 发送 ====================
bool WebrtcSender::SendRtcp(const uint8_t* packet, size_t length) {
    if (length == 0) {
        NS_LOG_INFO("0 RTCP packet");
        return true;
    }
    NS_ASSERT(length < 1500 && length > 0);
    int64_t send_time_ms = m_clock->TimeInMilliseconds();
    rtc::SentPacket sent_packet;
    sent_packet.packet_id = -1;
    sent_packet.send_time_ms = send_time_ms;
    sent_packet.info.packet_size_bytes = length;
    sent_packet.info.packet_type = rtc::PacketType::kData;
    m_call->OnSentPacket(sent_packet);

    {
        rtc::CopyOnWriteBuffer buffer(packet, length);
        LockScope ls(&m_rtcpLock);
        m_rtcpQ.push_back(buffer);
    }

    if (m_running) {
        Simulator::ScheduleWithContext(m_context, Time(0),
                                      MakeEvent(&WebrtcSender::DeliveryPacket, this));
    }
    return true;
}

bool WebrtcSender::SendRtp(const uint8_t* packet,
               size_t length,
               const webrtc::PacketOptions& options){
    if(length==0){
      NS_LOG_INFO("0 packet");
      return true;
    }
    NS_ASSERT(length<1500&&length>0);

    int64_t send_time_ms = m_clock->TimeInMilliseconds();
    rtc::SentPacket sent_packet;
    sent_packet.packet_id = options.packet_id;
    sent_packet.info.included_in_feedback = options.included_in_feedback;
    sent_packet.info.included_in_allocation = options.included_in_allocation;
    sent_packet.send_time_ms = send_time_ms;
    sent_packet.info.packet_size_bytes = length;
    sent_packet.info.packet_type = rtc::PacketType::kData;
    m_call->OnSentPacket(sent_packet);

    // 解析RTP包获取帧信息
    RtpFrameInfo frame_info = ParseRtpPacketInfo(packet, length);

    {
        rtc::CopyOnWriteBuffer buffer(packet,length);
        LockScope ls(&m_rtpLock);
        m_rtpQ.push_back(buffer);
        m_rtpFrameInfoQ.push_back(frame_info);  // 同步队列帧信息
        // NS_LOG_INFO("WebrtcSender: m_rtpQ: " << m_rtpQ.size() << " packets, frame_id=" << frame_info.frame_id);
    }

    // 带宽追踪
    bool output=false;
    uint32_t now=Simulator::Now().GetMilliSeconds();
    if(m_lastTraceTime==0) m_lastTraceTime=now, output=true;
    if(now>=m_lastTraceTime+kTraceInterval) m_lastTraceTime=now, output=true;

    if(output && !m_traceBw.IsNull()) {
        uint32_t original_bw = m_call->last_bandwidth_bps();
        uint32_t scaled_bw = GetScaledBandwidth(original_bw);
        NS_LOG_INFO("WebrtcSender: GCC Bandwidth - Original: " << original_bw
                   << " bps, Scaled target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor << ")");
        if(original_bw != 0){
            std::cout << "[GCC-Bandwidth-TRANSPORT-FEEDBACK] Time: " << now << "ms, "
                      << "Original: " << original_bw << " bps, "
                      << "Target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor
                      << ")" << std::endl;
        }
        m_traceBw(now, original_bw);
        if(!m_traceScaledBw.IsNull())
            m_traceScaledBw(now, original_bw, scaled_bw, m_bandwidth_scale_factor);
    }

    if(m_running)
        Simulator::ScheduleWithContext(m_context, Time (0),MakeEvent(&WebrtcSender::DeliveryPacket, this));
    return true;
}

// ==================== Start / Stop Application ====================
void WebrtcSender::StartApplication(){
    std::cout << "[DEBUG] StartApplication called" << std::endl;
    m_running=true;
    m_manager->CreateStreamPair();
    m_manager->Start();
    NS_LOG_INFO("WebrtcSender: Starting application with BitrateConstraints bandwidth scaling (方案A)");
    std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
    
    // Ensure context
    if (GetNode()) {
        m_context = static_cast<uint32_t>(GetNode()->GetId());
    }

    // 延迟应用带宽缩放，等待GCC有初始带宽估计
    Simulator::Schedule(Time(MilliSeconds(200)), &WebrtcSender::ApplyBandwidthScalingToController, this);
}

void WebrtcSender::StopApplication(){
    std::cout << "[DEBUG] StopApplication called" << std::endl;
    m_running=false;
    if (m_manager) m_manager->Stop();
    if (m_socket) { m_socket->Close(); m_socket = nullptr; }
    {
        LockScope ls(&m_rtpLock); m_rtpQ.clear();
    }
    {
        LockScope ls(&m_rtcpLock); m_rtcpQ.clear();
    }
    NS_LOG_INFO("WebrtcSender: Stopping application, final bandwidth scale factor μ=" << m_bandwidth_scale_factor);
    if (m_bandwidth_scale_factor != 1.0) {
        std::cout << "WebrtcSender: Bandwidth scaling completed with μ=" << m_bandwidth_scale_factor << std::endl;
    }
    std::cout << "WebrtcSender: Application stopped" << std::endl;
}

//==================== Apply Bandwidth Scaling via BitrateConstraints (方案A) =====================
void WebrtcSender::ApplyBandwidthScalingToController() {
    std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
    if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
    
    auto* transport_controller = m_call->GetTransportControllerSend();
    if(!transport_controller){ 
        std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; 
        return; 
    }

    double current_mu = GetCurrentBandwidthScaleFactor();
    uint32_t gcc_bandwidth = m_call->last_bandwidth_bps();
    
    // 如果GCC还没有估计出带宽，使用默认值
    if (gcc_bandwidth == 0) {
        gcc_bandwidth = 2500000; // 默认2.5 Mbps
        std::cout << "[WARNING] GCC bandwidth is 0, using default 2.5 Mbps" << std::endl;
    }
    
    // ========== OSCC修复：避免累积效应 ==========
    // 问题：如果直接用 last_bandwidth_bps() * current_mu，会导致累积：
    //   第一次：3M * 1.05 = 3.15M → 设置为 max_bitrate
    //   第二次：3.15M * 1.1 = 3.465M（错误！应该是 3M * 1.1 = 3.3M）
    // 解决：保存"基准带宽"，每次都用 基准带宽 * current_mu
    
    // 如果还没有基准带宽，或者这是首次调速（μ=1），保存当前值为基准
    if (m_base_gcc_bandwidth == 0 || (current_mu == 1.0 && m_last_applied_mu == 1.0)) {
        m_base_gcc_bandwidth = gcc_bandwidth;
        std::cout << "[OSCC-FIX] Base GCC bandwidth set to: " << (m_base_gcc_bandwidth / 1000000.0) << " Mbps" << std::endl;
    } else if (m_last_applied_mu != 1.0 && current_mu != m_last_applied_mu) {
        // 如果上次应用的μ不是1，需要还原：base = last_bandwidth / last_mu
        // 但这可能会有误差累积，更好的方法是检测到GCC带宽大幅下降时重置基准
        uint32_t estimated_base = static_cast<uint32_t>(gcc_bandwidth / m_last_applied_mu);
        
        // 只有当GCC带宽明显下降时（可能是网络变化），才更新基准
        if (gcc_bandwidth < m_base_gcc_bandwidth * 0.5) {
            // GCC带宽大幅下降，可能是网络拥塞，重置基准
            m_base_gcc_bandwidth = estimated_base;
            std::cout << "[OSCC-FIX] Base bandwidth reset due to congestion: " << (m_base_gcc_bandwidth / 1000000.0) << " Mbps" << std::endl;
        }
    }
    
    // 使用基准带宽计算缩放后的带宽
    uint32_t scaled_bandwidth = static_cast<uint32_t>(m_base_gcc_bandwidth * current_mu);
    
    // 保存本次应用的μ值
    m_last_applied_mu = current_mu;
    
    // ========== 方案A：通过BitrateConstraints限制WebRTC内部的发送带宽 ==========
    webrtc::BitrateConstraints constraints;
    constraints.min_bitrate_bps = 50000;  // 50 kbps
    constraints.start_bitrate_bps = scaled_bandwidth;
    constraints.max_bitrate_bps = scaled_bandwidth;
    
    try {
        transport_controller->SetSdpBitrateParameters(constraints);
        
        std::cout << "=== WebrtcSender: BitrateConstraints Applied (方案A) ===" << std::endl;
        std::cout << "Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
        std::cout << "Base GCC Bandwidth: " << (m_base_gcc_bandwidth / 1000000.0) << " Mbps" << std::endl;
        std::cout << "Current GCC reported: " << (gcc_bandwidth / 1000000.0) << " Mbps" << std::endl;
        std::cout << "Bandwidth scale factor μ: " << current_mu << std::endl;
        std::cout << "Scaled Bandwidth (max_bitrate): " << (scaled_bandwidth / 1000000.0) << " Mbps" << std::endl;
        std::cout << "=====================================================" << std::endl;
        
        NS_LOG_INFO("WebrtcSender: BitrateConstraints applied - μ=" << current_mu 
                   << ", Base_BW=" << m_base_gcc_bandwidth << " bps, Scaled_BW=" << scaled_bandwidth << " bps");
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in ApplyBandwidthScalingToController: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[ERROR] Unknown exception in ApplyBandwidthScalingToController" << std::endl;
    }
}

// ==================== 网络和包处理 ====================
void WebrtcSender::NotifyRouteChange(){
    rtc::NetworkRoute route;
    route.connected = true;
    route.local = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(1234));
    route.remote = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(4321));
    m_packetOverhead=webrtc::test::PacketOverhead::kDefault + kIpv4HeaderSize+cricket::kUdpHeaderSize;
    route.packet_overhead = m_packetOverhead;                        
    m_call->GetTransportControllerSend()->OnNetworkRouteChanged(kDummyTransportName, route);                         
}

// ==================== DeliveryPacket（方案A：恢复原始逻辑，通过BitrateConstraints缩放） ====================
void WebrtcSender::DeliveryPacket() {
    // 使用pair存储包和对应的帧信息
    std::deque<std::pair<Ptr<Packet>, RtpFrameInfo>> sendQ;
    
    {
        LockScope ls(&m_rtpLock);
        while(!m_rtpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            
            // 获取对应的帧信息
            RtpFrameInfo frame_info;
            if (!m_rtpFrameInfoQ.empty()) {
                frame_info = m_rtpFrameInfoQ.front();
                m_rtpFrameInfoQ.pop_front();
            }
            
            sendQ.push_back(std::make_pair(packet, frame_info));
            m_rtpQ.pop_front();
        }
    }
    {
        LockScope ls(&m_rtcpLock);
        while(!m_rtcpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            // RTCP包使用空的帧信息
            RtpFrameInfo empty_info;
            sendQ.push_back(std::make_pair(packet, empty_info));
            m_rtcpQ.pop_front();
        }        
    }

    // 立即发送，由WebRTC内部Pacer控制节奏
    while(!sendQ.empty()){
        auto& pkt_pair = sendQ.front();
        SendToNetworkWithFrameInfo(pkt_pair.first, pkt_pair.second);
        sendQ.pop_front();
    }
}

// 带帧信息的发送方法（新）
void WebrtcSender::SendToNetworkWithFrameInfo(Ptr<Packet> p, const RtpFrameInfo& frame_info){
    NS_ASSERT(p->GetSize()>0);
    uint64_t send_time = Simulator::Now().GetMilliSeconds();
    
    // 创建扩展的WebrtcTag，包含帧信息
    WebrtcTag tag(m_seq, send_time, frame_info.frame_id, frame_info.rtp_timestamp,
                  frame_info.is_keyframe, frame_info.is_first_packet, frame_info.is_last_packet);
    m_seq++;
    p->AddPacketTag(tag);
    
    if (m_socket) {
        m_socket->SendTo(p, 0, InetSocketAddress{m_peerIp, m_peerPort});
        
        // 调试日志（仅在帧第一个或最后一个包时输出）
        if (frame_info.is_first_packet || frame_info.is_last_packet) {
            NS_LOG_INFO("WebrtcSender: Sent packet seq=" << (m_seq-1) 
                        << ", frame_id=" << frame_info.frame_id
                        << ", keyframe=" << (int)frame_info.is_keyframe
                        << ", first=" << (int)frame_info.is_first_packet
                        << ", last=" << (int)frame_info.is_last_packet);
        }
    } else {
        NS_LOG_WARN("WebrtcSender: socket is null in SendToNetworkWithFrameInfo");
    }
}

// 保留原有接口以兼容性（不带帧信息）
void WebrtcSender::SendToNetwork(Ptr<Packet> p){
    RtpFrameInfo empty_info;
    SendToNetworkWithFrameInfo(p, empty_info);
}

void WebrtcSender::RecvPacket(Ptr<Socket> socket){
    if(!m_running) return;
    Address remoteAddr;
    auto packet = socket->RecvFrom(remoteAddr);
    if (!packet) return;
    uint32_t recv = packet->GetSize();
    NS_ASSERT(recv <= 1500);
    uint8_t buf[1500] = {'\0'};
    packet->CopyData(buf, recv);
    rtc::CopyOnWriteBuffer packet_data(buf, recv);
    if(!webrtc::RtpHeaderParser::IsRtcp(buf, recv)){
        auto ssrc = webrtc::RtpHeaderParser::GetSsrc(buf, recv);
        if(!ssrc.has_value()){ NS_LOG_INFO("sender no ssrc"); return; }
    }
    webrtc::EmulatedIpPacket emu_packet(rtc::SocketAddress(), rtc::SocketAddress(), std::move(packet_data),
                                        m_clock->CurrentTime(), m_packetOverhead);
    m_client->OnPacketReceived(std::move(emu_packet));                      
} 

// ==================== trace -> sender: SetTargetBitrate & apply pending implementation ====================
void WebrtcSender::SetTargetBitrate(uint32_t target_bps) {
    NS_LOG_INFO("WebrtcSender::SetTargetBitrate called with target_bps=" << target_bps);
    std::cout << "[TRACE->SENDER] SetTargetBitrate called: " << target_bps << " bps" << std::endl;

    if (target_bps == 0) {
        NS_LOG_WARN("WebrtcSender::SetTargetBitrate - target_bps == 0, ignoring");
        return;
    }

    // 去抖：如果变化太小则忽略
    if (m_last_applied_scaled_bw > 0) {
        uint32_t old_bw = m_last_applied_scaled_bw;
        uint32_t diff = (old_bw > target_bps) ? (old_bw - target_bps) : (target_bps - old_bw);
        if (diff < m_apply_threshold_abs && diff < static_cast<uint32_t>(old_bw * m_apply_threshold_ratio)) {
            NS_LOG_DEBUG("WebrtcSender::SetTargetBitrate - change too small, skip applying. diff=" << diff);
            return;
        }
    }

    // 保存 pending 值
    m_pending_scaled_bw = target_bps;
    m_has_pending_bw = true;

    // Schedule ApplyPendingBitrate in the node context if available, otherwise schedule normally.
    uint32_t ctx = m_context;
    if (ctx == 0 && GetNode()) {
        ctx = static_cast<uint32_t>(GetNode()->GetId());
    }

    if (ctx != 0) {
        Simulator::ScheduleWithContext(ctx, Time(0), MakeEvent(&WebrtcSender::ApplyPendingBitrate, this));
    } else {
        // Fallback if no node context yet
        Simulator::Schedule(Time(0), MakeEvent(&WebrtcSender::ApplyPendingBitrate, this));
    }

    std::cout << "[TRACE->SENDER] Pending scaled bw set to " << (m_pending_scaled_bw / 1000000.0)
              << " Mbps; scheduled ApplyPendingBitrate" << std::endl;
}

void WebrtcSender::ApplyPendingBitrate() {
    if (!m_has_pending_bw) {
        return;
    }
    uint32_t to_apply = m_pending_scaled_bw;
    m_has_pending_bw = false;

    // Do actual apply in node context (we are already in context due to ScheduleWithContext)
    std::cout << "[APPLY] Applying scaled bitrate to transport controller: " << to_apply << " bps" << std::endl;
    ApplyRealBandwidthScaling(to_apply);

    // update last applied for debouncing
    m_last_applied_scaled_bw = to_apply;

    // Notify trace callbacks if present (time now ms)
    uint32_t now_ms = Simulator::Now().GetMilliSeconds();
    if (!m_traceScaledBw.IsNull()) {
        // send original and scaled as same here; original unknown in this callback path, pass scaled for both original/target
        m_traceScaledBw(now_ms, to_apply, to_apply, m_bandwidth_scale_factor);
    }
}

void WebrtcSender::ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps) {
    if (!m_call) {
        NS_LOG_WARN("ApplyRealBandwidthScaling: m_call is null");
        return;
    }
    auto* transport_controller = m_call->GetTransportControllerSend();
    if (!transport_controller) {
        NS_LOG_WARN("ApplyRealBandwidthScaling: transport controller is null");
        return;
    }

    try {
        // Build BitrateConstraints with target as min/start/max to strictly bound pacer
        webrtc::BitrateConstraints constraints;
        constraints.min_bitrate_bps = std::max<uint32_t>(10000, target_bandwidth_bps / 10); // small floor
        constraints.start_bitrate_bps = target_bandwidth_bps;
        constraints.max_bitrate_bps = target_bandwidth_bps;

        transport_controller->SetSdpBitrateParameters(constraints);

        NS_LOG_INFO("Applied real bandwidth scaling to transport controller: " << target_bandwidth_bps);
        std::cout << "[APPLY] Transport controller SetSdpBitrateParameters applied: "
                  << (target_bandwidth_bps/1000000.0) << " Mbps" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in ApplyRealBandwidthScaling: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[ERROR] Unknown exception in ApplyRealBandwidthScaling" << std::endl;
    }
}

// ==================== Simple stubs for adaptive µ (extend as needed) ====================
double WebrtcSender::ComputeMuBasedOnNetwork() {
    // Placeholder: compute μ based on observed network state (loss/delay) via m_call or external RL manager
    // For now, return current value
    return m_bandwidth_scale_factor;
}

void WebrtcSender::UpdateBandwidthScaleFactor(double new_factor) {
    if (!(new_factor > 0.0)) {
        NS_LOG_WARN("WebrtcSender::UpdateBandwidthScaleFactor called with invalid factor: " << new_factor);
        return;
    }
    double old_mu = m_bandwidth_scale_factor;
    m_bandwidth_scale_factor = new_factor;
    std::cout << "[DEBUG] WebrtcSender::UpdateBandwidthScaleFactor: μ " << old_mu << " -> " << new_factor
              << " at simtime=" << Simulator::Now().GetSeconds() << "s" << std::endl;
    NS_LOG_INFO("WebrtcSender: Bandwidth scale factor updated to: " << new_factor);
    // Apply to controller shortly
    Simulator::ScheduleWithContext(m_context ? m_context : (GetNode()?static_cast<uint32_t>(GetNode()->GetId()):0),
                                   Time(MilliSeconds(50)), MakeEvent(&WebrtcSender::ApplyPendingBitrate, this));
}

void WebrtcSender::ApplyPeriodicBandwidthScaling() {
    // Optional periodic enforcement: read call->last_bandwidth_bps and apply GetScaledBandwidth
    if (!m_call) return;
    uint32_t gcc_bw = m_call->last_bandwidth_bps();
    if (gcc_bw == 0) return;
    uint32_t scaled = GetScaledBandwidth(gcc_bw);
    // debounced apply
    if (m_last_applied_scaled_bw == 0 ||
        std::abs(static_cast<int64_t>(static_cast<int64_t>(m_last_applied_scaled_bw) - static_cast<int64_t>(scaled))) >
            static_cast<int64_t>(std::max<uint32_t>(m_apply_threshold_abs, static_cast<uint32_t>(m_last_applied_scaled_bw * m_apply_threshold_ratio)))) {
        m_pending_scaled_bw = scaled;
        m_has_pending_bw = true;
        Simulator::ScheduleWithContext(m_context ? m_context : (GetNode()?static_cast<uint32_t>(GetNode()->GetId()):0),
                                       Time(MilliSeconds(0)), MakeEvent(&WebrtcSender::ApplyPendingBitrate, this));
    }
}

// ==================== 跳帧相关方法实现 ====================
void WebrtcSender::SkipToFrame(uint32_t target_frame_id) {
    std::cout << "[WebrtcSender] SkipToFrame called, target_frame_id=" << target_frame_id << std::endl;
    
    // 1. 清空当前RTP/RTCP发送队列（丢弃中间帧的数据）
    ClearPendingPackets();
    
    // 2. 设置跳帧目标
    m_skip_target_frame_id = target_frame_id;
    m_skip_frame_active = true;
    
    // 3. 请求编码器生成新的关键帧
    RequestKeyFrame();
    
    // 4. 输出日志
    std::cout << "[WebrtcSender] SkipToFrame: target=" << target_frame_id 
              << ", skip_active=" << m_skip_frame_active << std::endl;
    
    NS_LOG_INFO("WebrtcSender: SkipToFrame to " << target_frame_id);
}

void WebrtcSender::ClearPendingPackets() {
    size_t rtp_cleared = 0;
    size_t rtcp_cleared = 0;
    
    {
        LockScope ls(&m_rtpLock);
        rtp_cleared = m_rtpQ.size();
        m_rtpQ.clear();
    }
    {
        LockScope ls(&m_rtcpLock);
        rtcp_cleared = m_rtcpQ.size();
        m_rtcpQ.clear();
    }
    
    std::cout << "[WebrtcSender] ClearPendingPackets: cleared " << rtp_cleared 
              << " RTP packets, " << rtcp_cleared << " RTCP packets" << std::endl;
    
    NS_LOG_INFO("WebrtcSender: Cleared " << rtp_cleared << " RTP packets, " 
                << rtcp_cleared << " RTCP packets");
}

void WebrtcSender::RequestKeyFrame() {
    // 在 ns-3 WebRTC 仿真中，通过 Call 接口请求关键帧
    // WebRTC 的 Call 接口可能支持 RequestKeyFrame，但在仿真环境中
    // 视频编码器是模拟的，所以这里主要是记录日志
    
    if (m_call) {
        // 尝试通过 transport controller 或其他接口请求关键帧
        // 注：真实 WebRTC 中可以通过以下方式请求：
        // - call->SignalChannelNetworkState() 触发网络状态变化
        // - 或通过 video stream 的 RequestKeyFrame() 方法
        
        std::cout << "[WebrtcSender] RequestKeyFrame: requesting key frame from encoder" << std::endl;
        NS_LOG_INFO("WebrtcSender: Requesting key frame from encoder");
        
        // 在仿真环境中，关键帧请求会自动由视频 trace 处理
        // 因为我们是基于预录制的视频 trace 进行仿真
        // 跳帧后，FrameManager 会自动从目标关键帧开始处理
    } else {
        std::cout << "[WebrtcSender] RequestKeyFrame: WARNING - m_call is null!" << std::endl;
        NS_LOG_WARN("WebrtcSender: Cannot request key frame - m_call is null");
    }
}

// ==================== End file ====================
} // namespace ns3








