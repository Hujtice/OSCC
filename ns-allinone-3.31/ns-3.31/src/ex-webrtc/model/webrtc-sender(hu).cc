// #include "webrtc-sender.h"
// #include "ns3/log.h"
// #include "ns3/socket.h"
// #include "ns3/inet-socket-address.h"
// #include "ns3/udp-socket-factory.h"
// #include "rtc_base/net_helper.h"
// #include "rtc_base/network/sent_packet.h"
// #include "api/test/network_emulation/network_emulation_interfaces.h"
// #include "webrtc-tag.h"
// #include <random>
// #include <typeinfo>
// #include <cxxabi.h>
// #include <dlfcn.h>

// namespace ns3 {

// NS_LOG_COMPONENT_DEFINE("WebrtcSender");

// namespace {
//     const uint32_t kIpv4HeaderSize = 20;
//     const int32_t kTraceInterval = 25;
//     constexpr char kDummyTransportName[] = "dummy";
// }

// // ==================== 构造函数 ====================
// WebrtcSender::WebrtcSender(WebrtcSessionManager *manager){
//     std::cout << "[DEBUG] WebrtcSender constructor called" << std::endl;

//     m_manager = manager;
//     m_clock = manager->time_controller_->GetClock();
//     m_manager->RegisterSenderTransport(this, false);
//     m_client = m_manager->sender_client_;
//     m_call = m_client->GetCall();
//     m_initial_time = Simulator::Now().GetMilliSeconds();

//     m_current_estimated_bandwidth = 0;
//     m_current_scaled_bandwidth = 0;
//     m_socket = nullptr;
//     m_bindPort = 0;
//     m_peerPort = 0;
//     m_context = 0;
//     m_lastTraceTime = 0;
//     m_packetOverhead = 0;
//     m_running = false;
//     m_seq = 0;
//     m_bandwidth_scale_factor = 1.0; // 默认1.0

//     std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
// }

// WebrtcSender::~WebrtcSender(){}

// // ==================== 带宽缩放接口 ====================
// void WebrtcSender::SetBandwidthScaleFactor(double factor) {
//     if (factor > 0 && factor <= 1.3) {
//         m_bandwidth_scale_factor = factor;
//         NS_LOG_INFO("WebrtcSender: Initial bandwidth scale factor set to: " << factor);
//     } else {
//         NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor << ", using default 1.0");
//         m_bandwidth_scale_factor = 1;
//     }
// }

// double WebrtcSender::GetBandwidthScaleFactor() const {
//     return m_bandwidth_scale_factor;
// }

// double WebrtcSender::GetCurrentBandwidthScaleFactor() const {
//     return m_bandwidth_scale_factor;
// }

// void WebrtcSender::SetBandwidthScaleFactorDirect(double factor) {
//     if (factor > 0 && factor <= 1.3) {
//         double old_mu = m_bandwidth_scale_factor;
//         m_bandwidth_scale_factor = factor;
//         std::cout << "=== Direct Bandwidth Scaling ===\n"
//                   << "Time: " << Simulator::Now().GetSeconds() << "s\n"
//                   << "μ changed: " << old_mu << " -> " << factor << "\n"
//                   << "Bandwidth: " << (factor * 100) << "% of original\n"
//                   << "=================================" << std::endl;
//         NS_LOG_INFO("WebrtcSender: Bandwidth scale factor set directly to: " << factor);
//         // 安排延迟以让 transport controller 接口稳定
//         Simulator::Schedule(Time(MilliSeconds(100)), &WebrtcSender::ApplyBandwidthScalingToController, this);
//     } else {
//         NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor);
//     }
// }

// uint32_t WebrtcSender::GetScaledBandwidth(uint32_t original_bw) {
//     double current_mu = GetCurrentBandwidthScaleFactor();
//     uint32_t scaled_bw = static_cast<uint32_t>(original_bw * current_mu);
//     if (original_bw != m_current_estimated_bandwidth || scaled_bw != m_current_scaled_bandwidth) {
//         NS_LOG_INFO("WebrtcSender: Bandwidth scaling - Original: " << original_bw
//                      << " bps * μ=" << current_mu
//                      << " = Scaled: " << scaled_bw << " bps");
//         m_current_estimated_bandwidth = original_bw;
//         m_current_scaled_bandwidth = scaled_bw;
//     }
//     return scaled_bw;
// }

// void WebrtcSender::SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
//     m_traceScaledBw = cb;
//     NS_LOG_INFO("WebrtcSender: Scaled bandwidth trace callback set");
// }

// void WebrtcSender::SetBwTraceFuc(TraceBandwidth cb) {
//     m_traceBw = cb;
//     NS_LOG_INFO("WebrtcSender: Bandwidth trace callback set");
// }

// // ==================== 绑定和配置 ====================
// void WebrtcSender::Bind(uint16_t port) {
//     NS_LOG_INFO("WebrtcSender: Binding to port " << port);
//     m_bindPort = port;
//     if (!m_socket) {
//         m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
//         auto local = InetSocketAddress{Ipv4Address::GetAny(), port};
//         NS_ASSERT(m_socket->Bind(local) == 0);
//         m_socket->SetRecvCallback(MakeCallback(&WebrtcSender::RecvPacket, this));
//         NS_LOG_INFO("WebrtcSender: Successfully bound to port " << port);
//     }
// }

// void WebrtcSender::ConfigurePeer(Ipv4Address addr, uint16_t port) {
//     m_peerIp = addr;
//     m_peerPort = port;
//     NS_LOG_INFO("WebrtcSender: Configured peer - IP: " << addr << ", Port: " << port);
// }

// InetSocketAddress WebrtcSender::GetLocalAddress() {
//     if (m_socket) {
//         Address addr;
//         m_socket->GetSockName(addr);
//         return InetSocketAddress::ConvertFrom(addr);
//     }
//     return InetSocketAddress(Ipv4Address::GetAny(), 0);
// }

// // ==================== RTP/RTCP 发送 ====================
// bool WebrtcSender::SendRtcp(const uint8_t* packet, size_t length) {
//     if (length == 0) {
//         NS_LOG_INFO("0 RTCP packet");
//         return true;
//     }
//     NS_ASSERT(length < 1500 && length > 0);
//     int64_t send_time_ms = m_clock->TimeInMilliseconds();
//     rtc::SentPacket sent_packet;
//     sent_packet.packet_id = -1;
//     sent_packet.send_time_ms = send_time_ms;
//     sent_packet.info.packet_size_bytes = length;
//     sent_packet.info.packet_type = rtc::PacketType::kData;
//     m_call->OnSentPacket(sent_packet);

//     {
//         rtc::CopyOnWriteBuffer buffer(packet, length);
//         LockScope ls(&m_rtcpLock);
//         m_rtcpQ.push_back(buffer);
//     }

//     if (m_running) {
//         Simulator::ScheduleWithContext(m_context, Time(0),
//                                       MakeEvent(&WebrtcSender::DeliveryPacket, this));
//     }
//     return true;
// }

// bool WebrtcSender::SendRtp(const uint8_t* packet,
//                size_t length,
//                const webrtc::PacketOptions& options){
//     if(length==0){
//       NS_LOG_INFO("0 packet");
//       return true;
//     }
//     NS_ASSERT(length<1500&&length>0);

//     int64_t send_time_ms = m_clock->TimeInMilliseconds();
//     rtc::SentPacket sent_packet;
//     sent_packet.packet_id = options.packet_id;
//     sent_packet.info.included_in_feedback = options.included_in_feedback;
//     sent_packet.info.included_in_allocation = options.included_in_allocation;
//     sent_packet.send_time_ms = send_time_ms;
//     sent_packet.info.packet_size_bytes = length;
//     sent_packet.info.packet_type = rtc::PacketType::kData;
//     m_call->OnSentPacket(sent_packet);

//     {
//         rtc::CopyOnWriteBuffer buffer(packet,length);
//         LockScope ls(&m_rtpLock);
//         m_rtpQ.push_back(buffer);
//     }

//     // 带宽追踪
//     bool output=false;
//     uint32_t now=Simulator::Now().GetMilliSeconds();
//     if(m_lastTraceTime==0) m_lastTraceTime=now, output=true;
//     if(now>=m_lastTraceTime+kTraceInterval) m_lastTraceTime=now, output=true;

//     if(output && !m_traceBw.IsNull()) {
//         uint32_t original_bw = m_call->last_bandwidth_bps();
//         uint32_t scaled_bw = GetScaledBandwidth(original_bw);
//         NS_LOG_INFO("WebrtcSender: GCC Bandwidth - Original: " << original_bw
//                    << " bps, Scaled target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor << ")");
//         if(original_bw != 0){
//             std::cout << "[GCC-Bandwidth-TRANSPORT-FEEDBACK] Time: " << now << "ms, "
//                       << "Original: " << original_bw << " bps, "
//                       << "Target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor
//                       << ")" << std::endl;
//         }
//         m_traceBw(now, original_bw);
//         if(!m_traceScaledBw.IsNull())
//             m_traceScaledBw(now, original_bw, scaled_bw, m_bandwidth_scale_factor);
//     }

//     if(m_running)
//         Simulator::ScheduleWithContext(m_context, Time (0),MakeEvent(&WebrtcSender::DeliveryPacket, this));
//     return true;
// }

// // ==================== Start / Stop Application ====================
// void WebrtcSender::StartApplication(){
//     std::cout << "[DEBUG] StartApplication called" << std::endl;
//     m_running=true;
//     m_manager->CreateStreamPair();
//     m_manager->Start();
//     NS_LOG_INFO("WebrtcSender: Starting application with BitrateConstraints bandwidth scaling (方案A)");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
    
//     // 延迟应用带宽缩放，等待GCC有初始带宽估计
//     // 此后周期性调用以应对GCC带宽变化
//     Simulator::Schedule(Time(MilliSeconds(2000)), &WebrtcSender::ApplyBandwidthScalingToController, this);
// }

// void WebrtcSender::StopApplication(){
//     std::cout << "[DEBUG] StopApplication called" << std::endl;
//     m_running=false;
//     if (m_manager) m_manager->Stop();
//     if (m_socket) { m_socket->Close(); m_socket = nullptr; }
//     {
//         LockScope ls(&m_rtpLock); m_rtpQ.clear();
//     }
//     {
//         LockScope ls(&m_rtcpLock); m_rtcpQ.clear();
//     }
//     NS_LOG_INFO("WebrtcSender: Stopping application, final bandwidth scale factor μ=" << m_bandwidth_scale_factor);
//     if (m_bandwidth_scale_factor != 1.0) {
//         std::cout << "WebrtcSender: Bandwidth scaling completed with μ=" << m_bandwidth_scale_factor << std::endl;
//     }
//     std::cout << "WebrtcSender: Application stopped" << std::endl;
// }

// //==================== Apply Bandwidth Scaling via BitrateConstraints (方案A) ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
    
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ 
//         std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; 
//         return; 
//     }

//     double current_mu = GetCurrentBandwidthScaleFactor();
//     uint32_t gcc_bandwidth = m_call->last_bandwidth_bps();
    
//     // 如果GCC还没有估计出带宽，使用默认值
//     if (gcc_bandwidth == 0) {
//         gcc_bandwidth = 2500000; // 默认2.5 Mbps
//         std::cout << "[WARNING] GCC bandwidth is 0, using default 2.5 Mbps" << std::endl;
//     }
    
//     // 计算缩放后的目标带宽
//     uint32_t scaled_bandwidth = static_cast<uint32_t>(gcc_bandwidth * current_mu);
    
//     // ========== 方案A：通过BitrateConstraints限制WebRTC内部的发送带宽 ==========
//     webrtc::BitrateConstraints constraints;
    
//     // 最小带宽：保持较低以允许自适应
//     constraints.min_bitrate_bps = 50000;  // 50 kbps
    
//     // 开始带宽：设置为缩放后的带宽
//     constraints.start_bitrate_bps = scaled_bandwidth;
    
//     // 最大带宽：关键！限制上限为缩放后的带宽，让Pacer无法超过此限制
//     constraints.max_bitrate_bps = scaled_bandwidth;
    
//     try {
//         transport_controller->SetSdpBitrateParameters(constraints);
        
//         std::cout << "=== WebrtcSender: BitrateConstraints Applied (方案A) ===" << std::endl;
//         std::cout << "Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
//         std::cout << "GCC Bandwidth: " << (gcc_bandwidth / 1000000.0) << " Mbps" << std::endl;
//         std::cout << "Bandwidth scale factor μ: " << current_mu << std::endl;
//         std::cout << "Scaled Bandwidth (max_bitrate): " << (scaled_bandwidth / 1000000.0) << " Mbps" << std::endl;
//         std::cout << "Min Bitrate: " << (constraints.min_bitrate_bps / 1000.0) << " kbps" << std::endl;
//         std::cout << "=====================================================" << std::endl;
        
//         NS_LOG_INFO("WebrtcSender: BitrateConstraints applied - μ=" << current_mu 
//                    << ", GCC_BW=" << gcc_bandwidth << " bps, Scaled_BW=" << scaled_bandwidth << " bps");
        
//     } catch (const std::exception& e) {
//         std::cerr << "[ERROR] Exception in ApplyBandwidthScalingToController: " << e.what() << std::endl;
//     } catch (...) {
//         std::cerr << "[ERROR] Unknown exception in ApplyBandwidthScalingToController" << std::endl;
//     }
// }

// // void WebrtcSender::ApplyBandwidthScalingToController() {
// //     if (!m_call) return;

// //     uint32_t estimated_bw = m_call->last_bandwidth_bps();
// //     if (estimated_bw == 0) return;

// //     uint32_t scaled_bw =
// //         static_cast<uint32_t>(estimated_bw * m_bandwidth_scale_factor);

// //     webrtc::BitrateConstraints constraints;
// //     constraints.min_bitrate_bps   = 10000;                 // 10 kbps safety floor
// //     constraints.start_bitrate_bps = std::min(estimated_bw, scaled_bw);
// //     constraints.max_bitrate_bps   = scaled_bw;

// //     m_call->SetBitrateConstraints(constraints);

// //     NS_LOG_INFO("[BandwidthScaling-Call] "
// //         << "estimated=" << estimated_bw
// //         << " scaled=" << scaled_bw
// //         << " mu=" << m_bandwidth_scale_factor);
// // }



// // ==================== 网络和包处理 ====================
// void WebrtcSender::NotifyRouteChange(){
//     rtc::NetworkRoute route;
//     route.connected = true;
//     route.local = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(1234));
//     route.remote = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(4321));
//     m_packetOverhead=webrtc::test::PacketOverhead::kDefault + kIpv4HeaderSize+cricket::kUdpHeaderSize;
//     route.packet_overhead = m_packetOverhead;                        
//     m_call->GetTransportControllerSend()->OnNetworkRouteChanged(kDummyTransportName, route);                         
// }

// // ==================== DeliveryPacket（方案A：恢复原始逻辑，通过BitrateConstraints缩放） ====================
// void WebrtcSender::DeliveryPacket() {
//     std::deque<Ptr<Packet>> sendQ;
//     {
//         LockScope ls(&m_rtpLock);
//         while(!m_rtpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             sendQ.push_back(packet);
//             m_rtpQ.pop_front();
//         }
//     }
//     {
//         LockScope ls(&m_rtcpLock);
//         while(!m_rtcpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             sendQ.push_back(packet);
//             m_rtcpQ.pop_front();
//         }        
//     }

//     // --------- 恢复原始逻辑：立即发送，由WebRTC内部Pacer控制节奏 ---------
//     // 带宽缩放现在通过BitrateConstraints实现，不在这里添加人为间隔
//     while(!sendQ.empty()){
//         Ptr<Packet> packet = sendQ.front();
//         sendQ.pop_front();
//         SendToNetwork(packet);
//     }
// }

// void WebrtcSender::SendToNetwork(Ptr<Packet> p){
//     NS_ASSERT(p->GetSize()>0);
//     uint64_t send_time=Simulator::Now().GetMilliSeconds();
//     WebrtcTag tag(m_seq,send_time);
//     m_seq++;
//     p->AddPacketTag(tag); 
//     m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
// }

// void WebrtcSender::RecvPacket(Ptr<Socket> socket){
//     if(!m_running) return;
//     Address remoteAddr;
//     auto packet = socket->RecvFrom(remoteAddr);
//     uint32_t recv = packet->GetSize();
//     NS_ASSERT(recv <= 1500);
//     uint8_t buf[1500] = {'\0'};
//     packet->CopyData(buf, recv);
//     rtc::CopyOnWriteBuffer packet_data(buf, recv);
//     if(!webrtc::RtpHeaderParser::IsRtcp(buf, recv)){
//         auto ssrc = webrtc::RtpHeaderParser::GetSsrc(buf, recv);
//         if(!ssrc.has_value()){ NS_LOG_INFO("sender no ssrc"); return; }
//     }
//     webrtc::EmulatedIpPacket emu_packet(rtc::SocketAddress(), rtc::SocketAddress(), std::move(packet_data),
//                                         m_clock->CurrentTime(), m_packetOverhead);
//     m_client->OnPacketReceived(std::move(emu_packet));                      
// } 

// } // namespace ns3







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

    {
        rtc::CopyOnWriteBuffer buffer(packet,length);
        LockScope ls(&m_rtpLock);
        m_rtpQ.push_back(buffer);
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
    std::deque<Ptr<Packet>> sendQ;
    {
        LockScope ls(&m_rtpLock);
        while(!m_rtpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            sendQ.push_back(packet);
            m_rtpQ.pop_front();
        }
    }
    {
        LockScope ls(&m_rtcpLock);
        while(!m_rtcpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            sendQ.push_back(packet);
            m_rtcpQ.pop_front();
        }        
    }

    // 立即发送，由WebRTC内部Pacer控制节奏
    while(!sendQ.empty()){
        Ptr<Packet> packet = sendQ.front();
        sendQ.pop_front();
        SendToNetwork(packet);
    }
}

void WebrtcSender::SendToNetwork(Ptr<Packet> p){
    NS_ASSERT(p->GetSize()>0);
    uint64_t send_time=Simulator::Now().GetMilliSeconds();
    WebrtcTag tag(m_seq,send_time);
    m_seq++;
    p->AddPacketTag(tag); 
    if (m_socket) {
        m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
    } else {
        NS_LOG_WARN("WebrtcSender: socket is null in SendToNetwork");
    }
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













///#################################ght 改
// #include "webrtc-sender.h"
// #include "ns3/log.h"
// #include "ns3/socket.h"
// #include "ns3/inet-socket-address.h"
// #include "ns3/udp-socket-factory.h"
// #include "rtc_base/net_helper.h"
// #include "rtc_base/network/sent_packet.h"
// #include "api/test/network_emulation/network_emulation_interfaces.h"
// #include "webrtc-tag.h"
// #include <random>
// #include <typeinfo>
// #include <cxxabi.h>
// #include <dlfcn.h>
// #include <cmath>
// #include <algorithm>

// namespace ns3 {

// NS_LOG_COMPONENT_DEFINE("WebrtcSender");

// namespace {
//     const uint32_t kIpv4HeaderSize = 20;
//     const int32_t kTraceInterval = 25;
//     constexpr char kDummyTransportName[] = "dummy";
// }

// // ==================== 构造函数 ====================
// WebrtcSender::WebrtcSender(WebrtcSessionManager *manager){
//     std::cout << "[DEBUG] WebrtcSender constructor called" << std::endl;

//     m_manager = manager;
//     m_clock = manager->time_controller_->GetClock();
//     m_manager->RegisterSenderTransport(this, false);
//     m_client = m_manager->sender_client_;
//     m_call = m_client->GetCall();
//     m_initial_time = Simulator::Now().GetMilliSeconds();

//     m_current_estimated_bandwidth = 0;
//     m_current_scaled_bandwidth = 0;
//     m_socket = nullptr;
//     m_bindPort = 0;
//     m_peerPort = 0;
//     m_context = 0;
//     m_lastTraceTime = 0;
//     m_packetOverhead = 0;
//     m_running = false;
//     m_seq = 0;
//     m_bandwidth_scale_factor = 1.0; // 默认1.0
//     m_rlManager = nullptr;
//     m_last_reported_mu = -1.0;

//     // pacing/token-bucket 初始化（与现有逻辑兼容）
//     m_tokenBits = 0.0;
//     m_pacingRateBps = 1000000.0; // 初始 fallback 1 Mbps
//     m_lastPacingTime = Simulator::Now();
//     m_pacingTickUs = 1000; // 1 ms 默认 tick

//     std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
// }

// WebrtcSender::~WebrtcSender(){
//     StopPacingLoop();
// }

// // ==================== 带宽缩放接口 ====================
// void WebrtcSender::SetBandwidthScaleFactor(double factor) {
//     if (factor > 0.8 && factor <= 1.3) {
//         m_bandwidth_scale_factor = factor;
//         NS_LOG_INFO("WebrtcSender: Initial bandwidth scale factor set to: " << factor);
//     } else {
//         NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor << ", using default 1.0");
//         m_bandwidth_scale_factor = 1.0;
//     }
// }

// double WebrtcSender::GetBandwidthScaleFactor() const {
//     return m_bandwidth_scale_factor;
// }

// double WebrtcSender::GetCurrentBandwidthScaleFactor() const {
//     return m_bandwidth_scale_factor;
// }

// void WebrtcSender::SetBandwidthScaleFactorDirect(double factor) {
//     if (factor > 0.8 && factor <= 1.3) {
//         double old_mu = m_bandwidth_scale_factor;
//         m_bandwidth_scale_factor = factor;
//         std::cout << "=== Direct Bandwidth Scaling ===\n"
//                   << "Time: " << Simulator::Now().GetSeconds() << "s\n"
//                   << "μ changed: " << old_mu << " -> " << factor << "\n"
//                   << "Bandwidth: " << (factor * 100) << "% of original\n"
//                   << "=================================" << std::endl;
//         NS_LOG_INFO("WebrtcSender: Bandwidth scale factor set directly to: " << factor);
//         // 立刻更新 pacing 速率
//         UpdateBandwidthScaleFactor(factor);
//     } else {
//         NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor);
//     }
// }

// uint32_t WebrtcSender::GetScaledBandwidth(uint32_t original_bw) {
//     double current_mu = GetCurrentBandwidthScaleFactor();
//     uint32_t scaled_bw = static_cast<uint32_t>(original_bw * current_mu);
//     if (original_bw != m_current_estimated_bandwidth || scaled_bw != m_current_scaled_bandwidth) {
//         NS_LOG_INFO("WebrtcSender: Bandwidth scaling - Original: " << original_bw
//                      << " bps * μ=" << current_mu
//                      << " = Scaled: " << scaled_bw << " bps");
//         m_current_estimated_bandwidth = original_bw;
//         m_current_scaled_bandwidth = scaled_bw;
//     }
//     return scaled_bw;
// }

// void WebrtcSender::SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
//     m_traceScaledBw = cb;
//     NS_LOG_INFO("WebrtcSender: Scaled bandwidth trace callback set");
// }

// void WebrtcSender::SetBwTraceFuc(TraceBandwidth cb) {
//     m_traceBw = cb;
//     NS_LOG_INFO("WebrtcSender: Bandwidth trace callback set");
// }

// // ==================== RLSM 交互接口 ====================
// void WebrtcSender::SetRLSM(RLSM* rl_manager) {
//     m_rlManager = rl_manager;
//     std::cout << "[DEBUG] WebrtcSender::SetRLSM called, rl_manager=" << rl_manager << std::endl;
// }

// // RL 管理器应在每次决策时调用此函数以使发送端立即生效
// void WebrtcSender::UpdateBandwidthScaleFactor(double new_factor) {
//     if (!(new_factor > 0.0)) {
//         NS_LOG_WARN("WebrtcSender::UpdateBandwidthScaleFactor called with invalid factor: " << new_factor);
//         return;
//     }
//     double old_mu = m_bandwidth_scale_factor;
//     m_bandwidth_scale_factor = new_factor;
//     std::cout << "[DEBUG] WebrtcSender::UpdateBandwidthScaleFactor: μ " << old_mu << " -> " << new_factor
//               << " at simtime=" << Simulator::Now().GetSeconds() << "s" << std::endl;
//     NS_LOG_INFO("WebrtcSender: Bandwidth scale factor updated to: " << new_factor);

//     // 立即更新 pacing 目标速率（如果有估计）
//     uint32_t last_est = 0;
//     if (m_call) last_est = m_call->last_bandwidth_bps();
//     double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
//     if (new_rate <= 0.0) new_rate = 1000000.0; // fallback 1 Mbps
//     m_pacingRateBps = new_rate;

//     // 唤醒/启动 pacing 循环以立即生效
//     StartPacingLoop();
// }

// // ==================== 绑定和配置 ====================
// void WebrtcSender::Bind(uint16_t port) {
//     NS_LOG_INFO("WebrtcSender: Binding to port " << port);
//     m_bindPort = port;
//     if (!m_socket) {
//         m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
//         auto local = InetSocketAddress{Ipv4Address::GetAny(), port};
//         NS_ASSERT(m_socket->Bind(local) == 0);
//         m_socket->SetRecvCallback(MakeCallback(&WebrtcSender::RecvPacket, this));
//         NS_LOG_INFO("WebrtcSender: Successfully bound to port " << port);
//     }
// }

// void WebrtcSender::ConfigurePeer(Ipv4Address addr, uint16_t port) {
//     m_peerIp = addr;
//     m_peerPort = port;
//     NS_LOG_INFO("WebrtcSender: Configured peer - IP: " << addr << ", Port: " << port);
// }

// InetSocketAddress WebrtcSender::GetLocalAddress() {
//     if (m_socket) {
//         Address addr;
//         m_socket->GetSockName(addr);
//         return InetSocketAddress::ConvertFrom(addr);
//     }
//     return InetSocketAddress(Ipv4Address::GetAny(), 0);
// }

// // ==================== RTP/RTCP 发送 ====================
// bool WebrtcSender::SendRtcp(const uint8_t* packet, size_t length) {
//     if (length == 0) {
//         NS_LOG_INFO("0 RTCP packet");
//         return true;
//     }
//     NS_ASSERT(length < 1500 && length > 0);
//     int64_t send_time_ms = m_clock->TimeInMilliseconds();
//     rtc::SentPacket sent_packet;
//     sent_packet.packet_id = -1;
//     sent_packet.send_time_ms = send_time_ms;
//     sent_packet.info.packet_size_bytes = length;
//     sent_packet.info.packet_type = rtc::PacketType::kData;
//     m_call->OnSentPacket(sent_packet);

//     {
//         rtc::CopyOnWriteBuffer buffer(packet, length);
//         LockScope ls(&m_rtcpLock);
//         m_rtcpQ.push_back(buffer);
//     }

//     if (m_running) {
//         Simulator::ScheduleWithContext(m_context, Time(0),
//                                       MakeEvent(&WebrtcSender::DeliveryPacket, this));
//     }
//     return true;
// }

// bool WebrtcSender::SendRtp(const uint8_t* packet,
//                size_t length,
//                const webrtc::PacketOptions& options){
//     if(length==0){
//       NS_LOG_INFO("0 packet");
//       return true;
//     }
//     NS_ASSERT(length<1500&&length>0);

//     int64_t send_time_ms = m_clock->TimeInMilliseconds();
//     rtc::SentPacket sent_packet;
//     sent_packet.packet_id = options.packet_id;
//     sent_packet.info.included_in_feedback = options.included_in_feedback;
//     sent_packet.info.included_in_allocation = options.included_in_allocation;
//     sent_packet.send_time_ms = send_time_ms;
//     sent_packet.info.packet_size_bytes = length;
//     sent_packet.info.packet_type = rtc::PacketType::kData;



//     int64_t webrtc_send_time_ms = m_clock->TimeInMilliseconds();
//     uint64_t ns3_send_time_ms = Simulator::Now().GetMilliSeconds();
    
//     NS_LOG_INFO("SendRtp called:");
//     NS_LOG_INFO("  - Packet size: " << length << " bytes");
//     NS_LOG_INFO("  - Packet ID: " << options.packet_id);
//     NS_LOG_INFO("  - WebRTC send time: " << webrtc_send_time_ms << " ms");
//     NS_LOG_INFO("  - NS-3 send time: " << ns3_send_time_ms << " ms");
//     NS_LOG_INFO("  - Time diff (NS3 - WebRTC): " << (ns3_send_time_ms - webrtc_send_time_ms) << " ms");
    
//     // 解析RTP头部获取序列号
//     if (length >= 4) {
//         uint16_t seq_num = (packet[2] << 8) | packet[3];
//         NS_LOG_INFO("  - RTP sequence: " << seq_num);
//     }

//     m_call->OnSentPacket(sent_packet);

//     {
//         rtc::CopyOnWriteBuffer buffer(packet,length);
//         LockScope ls(&m_rtpLock);
//         m_rtpQ.push_back(buffer);
//     }

//     // 带宽追踪
//     bool output=false;
//     uint32_t now=Simulator::Now().GetMilliSeconds();
//     if(m_lastTraceTime==0) m_lastTraceTime=now, output=true;
//     if(now>=m_lastTraceTime+kTraceInterval) m_lastTraceTime=now, output=true;

//     if(output && !m_traceBw.IsNull()) {
//         uint32_t original_bw = m_call->last_bandwidth_bps();
//         uint32_t scaled_bw = GetScaledBandwidth(original_bw);
//         NS_LOG_INFO("WebrtcSender: GCC Bandwidth - Original: " << original_bw
//                    << " bps, Scaled target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor << ")");
//         if(original_bw != 0){
//             std::cout << "[GCC-Bandwidth-TRANSPORT-FEEDBACK] Time: " << now << "ms, "
//                       << "Original: " << original_bw << " bps, "
//                       << "Target: " << scaled_bw << " bps (μ=" << m_bandwidth_scale_factor
//                       << ")" << std::endl;
//         }
//         m_traceBw(now, original_bw);
//         if(!m_traceScaledBw.IsNull())
//             m_traceScaledBw(now, original_bw, scaled_bw, m_bandwidth_scale_factor);
//     }

//     if(m_running)
//         Simulator::ScheduleWithContext(m_context, Time (0),MakeEvent(&WebrtcSender::DeliveryPacket, this));
//     return true;
// }

// // ==================== Start / Stop Application ====================
// void WebrtcSender::StartApplication(){
//     std::cout << "[DEBUG] StartApplication called" << std::endl;
//     m_running=true;
//     // 临时修复：禁用所有限制
//     // m_bandwidth_scale_factor = 1.0;  // 禁用缩放
//     // m_pacingRateBps = 0;             // 禁用pacing（0表示无限制）
//     // m_pacingTickUs = 0;              // 禁用pacing tick
    
//     std::cout << "[FIX] DISABLED ALL LIMITATIONS FOR DEBUGGING:" << std::endl;
//     std::cout << "[FIX]   Bandwidth scaling: DISABLED (μ=1.0)" << std::endl;
//     std::cout << "[FIX]   Pacing: DISABLED" << std::endl;
//     std::cout << "[FIX]   WebRTC will send at full rate" << std::endl;
    
//     m_manager->CreateStreamPair();
//     m_manager->Start();

    
//     // Ensure a valid context for ScheduleWithContext (use node id if available)
//     if (GetNode()) {
//         m_context = static_cast<uint32_t>(GetNode()->GetId());
//     }
//     m_manager->CreateStreamPair();
//     m_manager->Start();
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
//     // 启动 pacing 循环（如果有数据，会立即发送或排队）
//     StartPacingLoop();
//     // 尝试在应用层应用带宽缩放（注意：标准 transport controller 可能没有对应的 API）
//     Simulator::Schedule(Time(MilliSeconds(2000)), &WebrtcSender::ApplyBandwidthScalingToController, this);
//         // 添加定期状态检查

// }



// void WebrtcSender::StopApplication(){
//     std::cout << "[DEBUG] StopApplication called" << std::endl;
//     m_running=false;
//     StopPacingLoop();
//     if (m_manager) m_manager->Stop();
//     if (m_socket) { m_socket->Close(); m_socket = nullptr; }
//     {
//         LockScope ls(&m_rtpLock); m_rtpQ.clear();
//     }
//     {
//         LockScope ls(&m_rtcpLock); m_rtcpQ.clear();
//     }
//     {
//         // 清空 pacing 队列
//         LockScope ls(&m_rtpLock);
//         m_pacingQueue.clear();
//     }
//     NS_LOG_INFO("WebrtcSender: Stopping application, final bandwidth scale factor μ=" << m_bandwidth_scale_factor);
//     if (m_bandwidth_scale_factor != 1.0) {
//         std::cout << "WebrtcSender: Bandwidth scaling completed with μ=" << m_bandwidth_scale_factor << std::endl;
//     }
//     std::cout << "WebrtcSender: Application stopped" << std::endl;
// }

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

//     double current_mu = GetCurrentBandwidthScaleFactor();

//     NS_LOG_INFO("WebrtcSender: (local) Apply bandwidth scaling μ=" << current_mu);
//     std::cout << "=== WebrtcSender: (local) Applied Bandwidth Scaling (no transport API) ===\n"
//               << "Time: " << Simulator::Now().GetSeconds() << "s\n"
//               << "Bandwidth scale factor μ: " << current_mu << "\n"
//               << "================================================" << std::endl;
// }

// // ==================== 网络和包处理 ====================
// void WebrtcSender::NotifyRouteChange(){
//     rtc::NetworkRoute route;
//     route.connected = true;
//     route.local = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(1234));
//     route.remote = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(4321));
//     m_packetOverhead=webrtc::test::PacketOverhead::kDefault + kIpv4HeaderSize+cricket::kUdpHeaderSize;
//     route.packet_overhead = m_packetOverhead;                        
//     m_call->GetTransportControllerSend()->OnNetworkRouteChanged(kDummyTransportName, route);                         
// }

// // ==================== Pacing / Token-bucket 实现（与现有 DeliveryPacket 接口兼容） ====================
// void WebrtcSender::StartPacingLoop() {
//     // 如果事件已存在并在运行则返回
//     if (m_pacingEvent.IsRunning()) return;

//     // 初始化 token/time
//     m_lastPacingTime = Simulator::Now();
//     // 使用最新估计更新 pacing 速率
//     if (m_call) {
//         uint32_t last_est = m_call->last_bandwidth_bps();
//         double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
//         if (new_rate <= 0.0) new_rate = 1000000.0;
//         m_pacingRateBps = new_rate;
//     }
//     // 立即安排一次 tick（注意：使用 Schedule 而不是 ScheduleWithContext，因为在你的 ns-3 版本 ScheduleWithContext 返回 void）
//     m_pacingEvent = Simulator::Schedule(NanoSeconds(0), MakeEvent(&WebrtcSender::PacingTick, this));
// }

// void WebrtcSender::StopPacingLoop() {
//     if (m_pacingEvent.IsRunning()) {
//         m_pacingEvent.Cancel();
//     }
//     m_tokenBits = 0.0;
// }

// void WebrtcSender::PacingTick() {
//     // 计算时间差并增加 token
//     Time now = Simulator::Now();
//     uint64_t elapsed_ns = 0;
//     if (now >= m_lastPacingTime) {
//         elapsed_ns = (now - m_lastPacingTime).GetNanoSeconds();
//     } else {
//         elapsed_ns = 0;
//     }
//     if (elapsed_ns > 0) {
//         double added_bits = (static_cast<double>(elapsed_ns) / 1e9) * m_pacingRateBps;
//         m_tokenBits += added_bits;
//         m_lastPacingTime = now;
//     }

//     // 如果有新的估计，更新 pacing 速率（以便 mu 变化或 estimator 更新生效）
//     if (m_call) {
//         uint32_t last_est = m_call->last_bandwidth_bps();
//         double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
//         if (new_rate <= 0.0) new_rate = 1000000.0;
//         m_pacingRateBps = new_rate;
//     }

//     // 把 rtp/rtcp 缓冲区的包移动到 pacing 队列（线程安全）
//     {
//         LockScope ls(&m_rtpLock);
//         while(!m_rtpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             m_pacingQueue.push_back(packet);
//             m_rtpQ.pop_front();
//         }
//     }
//     {
//         LockScope ls(&m_rtcpLock);
//         while(!m_rtcpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             m_pacingQueue.push_back(packet);
//             m_rtcpQ.pop_front();
//         }
//     }

//     // 尽可能发送队列中的包
//     while(!m_pacingQueue.empty()) {
//         Ptr<Packet> pkt = m_pacingQueue.front();
//         uint64_t pkt_bits = static_cast<uint64_t>(pkt->GetSize()) * 8ULL;
//         if (m_tokenBits + 1e-9 >= static_cast<double>(pkt_bits)) {
//             m_tokenBits -= static_cast<double>(pkt_bits);
//             m_pacingQueue.pop_front();
//             // 发送包（保持原有 SendToNetwork 语义）
//             SendToNetwork(pkt);
//         } else {
//             break;
//         }
//     }

//     // 计划下一次 tick：如果队列非空，计算等待到下一个包所需时间；若空，则周期性唤醒
//     if (!m_pacingQueue.empty()) {
//         Ptr<Packet> next_pkt = m_pacingQueue.front();
//         uint64_t next_bits = static_cast<uint64_t>(next_pkt->GetSize()) * 8ULL;
//         double bits_needed = static_cast<double>(next_bits) - m_tokenBits;
//         if (bits_needed < 0.0) bits_needed = 0.0;
//         double time_needed_s = bits_needed / m_pacingRateBps;
//         uint64_t time_needed_ns = static_cast<uint64_t>(std::max(1.0, std::round(time_needed_s * 1e9)));
//         uint64_t min_tick_ns = static_cast<uint64_t>(m_pacingTickUs) * 1000ULL;
//         uint64_t schedule_ns = std::max(min_tick_ns, time_needed_ns);
//         // 使用 Simulator::Schedule，因为某些 ns-3 版本 ScheduleWithContext 返回 void
//         m_pacingEvent = Simulator::Schedule(NanoSeconds(schedule_ns), MakeEvent(&WebrtcSender::PacingTick, this));
//     } else {
//         uint64_t idle_ns = static_cast<uint64_t>(m_pacingTickUs) * 1000ULL;
//         m_pacingEvent = Simulator::Schedule(NanoSeconds(idle_ns), MakeEvent(&WebrtcSender::PacingTick, this));
//     }
// }

// // ==================== DeliveryPacket（兼容原有逻辑：把包放入 pacing 队列） ====================
// void WebrtcSender::DeliveryPacket() {
//     // 把 rtp/rtcp 队列中内容转入 pacing 队列，PacingTick 将处理发送
//     {
//         LockScope ls(&m_rtpLock);
//         while(!m_rtpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             m_pacingQueue.push_back(packet);
//             m_rtpQ.pop_front();
//         }
//     }
//     {
//         LockScope ls(&m_rtcpLock);
//         while(!m_rtcpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
//             Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
//             m_pacingQueue.push_back(packet);
//             m_rtcpQ.pop_front();
//         }        
//     }

//     // 更新 pacing 速率（使用最新估计与 μ）
//     uint32_t last_est = 0;
//     if (m_call) last_est = m_call->last_bandwidth_bps();
//     double computed_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
//     if (computed_rate > 0.0) {
//         m_pacingRateBps = computed_rate;
//     } else {
//         // 保持当前 m_pacingRateBps 或 fallback
//         if (m_pacingRateBps <= 0.0) m_pacingRateBps = 1000000.0;
//     }

//     // 唤醒/启动 pacing 循环
//     StartPacingLoop();
// }

// // ==================== 发送 / 接收 ====================
// void WebrtcSender::SendToNetwork(Ptr<Packet> p){
//     NS_ASSERT(p->GetSize()>0);
//     uint64_t send_time=Simulator::Now().GetMilliSeconds();
//     WebrtcTag tag(m_seq,send_time);
//     uint64_t seq_to_report = m_seq;
//     m_seq++;
//     p->AddPacketTag(tag); 
//     // DEBUG 打印，便于验证实际发送时间与 seq、速率和令牌
//     std::cout << "[SEND] seq=" << seq_to_report
//               << " t=" << send_time << "ms size=" << p->GetSize()
//               << " μ=" << m_bandwidth_scale_factor
//               << " rate=" << static_cast<uint32_t>(m_pacingRateBps) << "bps"
//               << " tokens=" << static_cast<uint64_t>(m_tokenBits) << std::endl;
//     if (m_socket) {
//         m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
//     } else {
//         NS_LOG_WARN("WebrtcSender: socket is null in SendToNetwork");
//     }
// }

// void WebrtcSender::RecvPacket(Ptr<Socket> socket) {
//     if (!m_running) return;
    
//     Address remoteAddr;
//     auto packet = socket->RecvFrom(remoteAddr);
//     if (!packet) return;
    
//     uint32_t recv = packet->GetSize();
//     if (recv == 0) return;
    
//     NS_ASSERT(recv <= 1500);
    
//     uint64_t recv_time_ms = Simulator::Now().GetMilliSeconds();
    
//     uint8_t buf[1500] = {'\0'};
//     packet->CopyData(buf, recv);
    
//     // 简化的处理：只修改TransportFeedback包的基本时间戳
//     bool is_rtcp = webrtc::RtpHeaderParser::IsRtcp(buf, recv);
    
//     if (is_rtcp && recv >= 14) {
//         uint8_t packet_type = buf[1];
        
//         if (packet_type == 205) { // Transport Feedback
//             // 只进行最基本的时间戳修复
//             SimpleFixTransportFeedback(buf, recv, recv_time_ms);
//         }
//     }
    
//     // 转发包
//     rtc::CopyOnWriteBuffer packet_data(buf, recv);
//     webrtc::EmulatedIpPacket emu_packet(
//         rtc::SocketAddress(),
//         rtc::SocketAddress(),
//         std::move(packet_data),
//         m_clock->CurrentTime(),
//         m_packetOverhead
//     );
    
//     m_client->OnPacketReceived(std::move(emu_packet));
// }

// // 简化的修复函数
// void WebrtcSender::SimpleFixTransportFeedback(uint8_t* packet, size_t length, uint64_t recv_time_ms) {
//     if (length < 14) return;
    
//     // 输出调试信息
//     uint16_t base_seq = (packet[8] << 8) | packet[9];
//     uint32_t ref_time_original = (packet[10] << 16) | (packet[11] << 8) | packet[12];
//     uint8_t feedback_seq = packet[13];
    
//     std::cout << "[SIMPLE-FIX] TransportFeedback:" << std::endl;
//     std::cout << "[SIMPLE-FIX]   Base seq: " << base_seq << std::endl;
//     std::cout << "[SIMPLE-FIX]   Original ref time: " << ref_time_original 
//               << " (=" << (ref_time_original * 250 / 1000) << "ms)" << std::endl;
//     std::cout << "[SIMPLE-FIX]   Feedback seq: " << (int)feedback_seq << std::endl;
//     std::cout << "[SIMPLE-FIX]   Recv time: " << recv_time_ms << "ms" << std::endl;
    
//     // 简单修复：设置一个合理的时间戳
//     static uint64_t last_fixed_time = 0;
//     uint64_t fixed_time_ms;
    
//     if (last_fixed_time == 0) {
//         fixed_time_ms = recv_time_ms - 20;  // 假设20ms前发送
//     } else {
//         // 保持与上一个包的间隔
//         uint64_t interval = recv_time_ms - last_fixed_time;
//         fixed_time_ms = last_fixed_time + interval - 20;
//     }
    
//     last_fixed_time = recv_time_ms;
    
//     // 转换为250us单位
//     uint32_t new_ref_time = (fixed_time_ms * 1000) / 250;
//     new_ref_time &= 0x00FFFFFF;  // 24位
    
//     // 更新包
//     packet[10] = (new_ref_time >> 16) & 0xFF;
//     packet[11] = (new_ref_time >> 8) & 0xFF;
//     packet[12] = new_ref_time & 0xFF;
    
//     std::cout << "[SIMPLE-FIX]   Fixed ref time: " << new_ref_time 
//               << " (=" << (new_ref_time * 250 / 1000) << "ms)" << std::endl;
// }

// // 添加诊断函数到类定义中
// void WebrtcSender::PerformFullDiagnostics(uint64_t current_time_ms) {
//     std::cout << "\n====== COMPLETE DIAGNOSTICS ======" << std::endl;
//     std::cout << "Diagnostic time: " << current_time_ms << " ms" << std::endl;
//     std::cout << "Running: " << (m_running ? "YES" : "NO") << std::endl;
//     std::cout << "Call object: " << (m_call ? "VALID" : "NULL") << std::endl;
//     std::cout << "Client object: " << (m_client ? "VALID" : "NULL") << std::endl;
//     std::cout << "Socket: " << (m_socket ? "VALID" : "NULL") << std::endl;
    
//     if (m_call) {
//         uint32_t bw = m_call->last_bandwidth_bps();
//         std::cout << "Current GCC bandwidth: " << bw << " bps" << std::endl;
        
//         // 尝试获取更多信息（如果可用）
//         std::cout << "Bandwidth scaling factor μ: " << m_bandwidth_scale_factor << std::endl;
//         std::cout << "Scaled target: " << GetScaledBandwidth(bw) << " bps" << std::endl;
//     }
    
//     // 队列状态
//     {
//         LockScope ls(&m_rtpLock);
//         std::cout << "RTP queue: " << m_rtpQ.size() << " packets" << std::endl;
//     }
//     {
//         LockScope ls(&m_rtcpLock);
//         std::cout << "RTCP queue: " << m_rtcpQ.size() << " packets" << std::endl;
//     }
//     std::cout << "Pacing queue: " << m_pacingQueue.size() << " packets" << std::endl;
    
//     // 网络信息
//     std::cout << "Peer IP: " << m_peerIp << ", Port: " << m_peerPort << std::endl;
//     std::cout << "Local bind port: " << m_bindPort << std::endl;
//     std::cout << "Packet overhead: " << m_packetOverhead << " bytes" << std::endl;
    
//     std::cout << "==================================\n" << std::endl;
// }

// void WebrtcSender::CheckAndFixPacingRate() {
//     std::cout << "\n=== PACING RATE DIAGNOSIS ===" << std::endl;
//     std::cout << "[DIAG] Current pacing rate: " << m_pacingRateBps << " bps" << std::endl;
//     std::cout << "[DIAG] GCC bandwidth: " << (m_call ? m_call->last_bandwidth_bps() : 0) << " bps" << std::endl;
//     std::cout << "[DIAG] Bandwidth scale factor μ: " << m_bandwidth_scale_factor << std::endl;
    
//     // 如果pacing rate太低，强制提高
//     if (m_pacingRateBps < 500000) {  // 低于500kbps
//         std::cout << "[FIX] Pacing rate too low, setting to 1 Mbps" << std::endl;
//         m_pacingRateBps = 1000000;  // 1 Mbps
        
//         // 立即重启pacing循环
//         StopPacingLoop();
//         StartPacingLoop();
//     }
    
//     std::cout << "============================\n" << std::endl;
// }



// } // namespace ns3