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
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
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

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

//     double current_mu = GetCurrentBandwidthScaleFactor();

//     try {
//         transport_controller->SetBandwidthScaleFactor(current_mu);
//     } catch (...) {
//         std::cout<<"[DEBUG] Exception in ApplyBandwidthScalingToController"<<std::endl;
//         return;
//     }
//     NS_LOG_INFO("WebrtcSender: Applied bandwidth scaling μ=" << current_mu << " to transport controller");
//     std::cout << "=== WebrtcSender: Applied Bandwidth Scaling ===\n"
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

// // ==================== DeliveryPacket（修改后，μ 调整包间隔） ====================
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

//     // ----------------- 按 μ 缩放间隔发送 -----------------
//     double mu = GetCurrentBandwidthScaleFactor(); // μ ∈ (0.8, 1.3] 或你设定的范围
//     double base_interval_ms = 10.0; // 原始每包间隔 10ms，可根据需要调整

//     Time interval = MilliSeconds(base_interval_ms / mu); // 缩放间隔
//     Time accumulated_delay = Time(0);

//     for (auto& packet : sendQ) {
//         Simulator::ScheduleWithContext(
//             m_context, accumulated_delay,
//             MakeEvent(&WebrtcSender::SendToNetwork, this, packet)
//         );
//         accumulated_delay += interval;
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
    
//     std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
// }

// WebrtcSender::~WebrtcSender(){}

// // ==================== 带宽缩放接口 ====================
// void WebrtcSender::SetBandwidthScaleFactor(double factor) {
//     if (factor > 0 && factor <= 2.0) {
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
//     if (factor > 0 && factor <= 2.0) {
//         double old_mu = m_bandwidth_scale_factor;
//         m_bandwidth_scale_factor = factor;
//         std::cout << "=== Direct Bandwidth Scaling ===\n"
//                   << "Time: " << Simulator::Now().GetSeconds() << "s\n"
//                   << "μ changed: " << old_mu << " -> " << factor << "\n"
//                   << "Bandwidth: " << (factor * 100) << "% of original\n"
//                   << "=================================" << std::endl;
//         NS_LOG_INFO("WebrtcSender: Bandwidth scale factor set directly to: " << factor);
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

// void WebrtcSender::SetBwTraceFuc(TraceBandwidth cb) {
//     m_traceBw = cb;
//     NS_LOG_INFO("WebrtcSender: Bandwidth trace callback set");
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
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
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

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }
    
//     double current_mu = GetCurrentBandwidthScaleFactor();
//     try {
//         transport_controller->SetBandwidthScaleFactor(current_mu);
//     } catch (...) {
//         std::cout<<"[DEBUG] Exception in ApplyBandwidthScalingToController"<<std::endl;
//         return;
//     }
    
//     NS_LOG_INFO("WebrtcSender: Applied bandwidth scaling μ=" << current_mu << " to transport controller");
//     std::cout << "=== WebrtcSender: Applied Bandwidth Scaling ===\n"
//               << "Time: " << Simulator::Now().GetSeconds() << "s\n"
//               << "Bandwidth scale factor μ: " << current_mu << "\n"
//               << "================================================" << std::endl;
// }

// // ==================== 网络和包处理 ====================
// void WebrtcSender::NotifyRouteChange(){
//   rtc::NetworkRoute route;
//   route.connected = true;
//   route.local = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(1234));
//   route.remote = rtc::RouteEndpoint::CreateWithNetworkId(static_cast<uint16_t>(4321));
//   m_packetOverhead=webrtc::test::PacketOverhead::kDefault + kIpv4HeaderSize+cricket::kUdpHeaderSize;
//   route.packet_overhead = m_packetOverhead;                        
//   m_call->GetTransportControllerSend()->OnNetworkRouteChanged(kDummyTransportName, route);                         
// }

// void WebrtcSender::DeliveryPacket(){
//     std::deque<Ptr<Packet>> sendQ;
//     {
//         LockScope ls(&m_rtpLock);
//         while(!m_rtpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer=m_rtpQ.front();
//             Ptr<Packet> packet=Create<Packet>(buffer.data(),buffer.size());
//             sendQ.push_back(packet);
//             m_rtpQ.pop_front();
//         }
//     }
//     {
//         LockScope ls(&m_rtcpLock);
//         while(!m_rtcpQ.empty()){
//             rtc::CopyOnWriteBuffer buffer=m_rtcpQ.front();
//             Ptr<Packet> packet=Create<Packet>(buffer.data(),buffer.size());
//             sendQ.push_back(packet);
//             m_rtcpQ.pop_front();
//         }        
//     }
//     while(!sendQ.empty()){
//         Ptr<Packet> packet=sendQ.front();
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
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
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

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

//     double current_mu = GetCurrentBandwidthScaleFactor();

//     try {
//         transport_controller->SetBandwidthScaleFactor(current_mu);
//     } catch (...) {
//         std::cout<<"[DEBUG] Exception in ApplyBandwidthScalingToController"<<std::endl;
//         return;
//     }
//     NS_LOG_INFO("WebrtcSender: Applied bandwidth scaling μ=" << current_mu << " to transport controller");
//     std::cout << "=== WebrtcSender: Applied Bandwidth Scaling ===\n"
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

// // ==================== DeliveryPacket（修改后，μ 调整包间隔） ====================
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

//     // ----------------- 按 μ 缩放间隔发送 -----------------
//     double mu = GetCurrentBandwidthScaleFactor(); // μ ∈ (0.8, 1.3] 或你设定的范围
//     double base_interval_ms = 10.0; // 原始每包间隔 10ms，可根据需要调整

//     Time interval = MilliSeconds(base_interval_ms / mu); // 缩放间隔
//     Time accumulated_delay = Time(0);

//     for (auto& packet : sendQ) {
//         Simulator::ScheduleWithContext(
//             m_context, accumulated_delay,
//             MakeEvent(&WebrtcSender::SendToNetwork, this, packet)
//         );
//         accumulated_delay += interval;
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
//         // 安排延迟以让 transport controller 接口稳定（注意：此函数不会调用不存在的 transport API）
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
//     // Ensure a valid context for ScheduleWithContext (use node id if available)
//     if (GetNode()) {
//         m_context = static_cast<uint32_t>(GetNode()->GetId());
//     }
//     m_manager->CreateStreamPair();
//     m_manager->Start();
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
//     // 尝试在应用层应用带宽缩放（注意：标准 transport controller 可能没有对应的 API）
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

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

//     double current_mu = GetCurrentBandwidthScaleFactor();

//     // NOTE:
//     // The standard webrtc::RtpTransportControllerSendInterface in public WebRTC
//     // does not expose a SetBandwidthScaleFactor(...) method. Calling such a method
//     // will produce a compile error. If you have a custom WebRTC build that adds
//     // that API, call it there. Otherwise, we apply bandwidth scaling at the
//     // application/pacing layer (see DeliveryPacket).
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

// // ==================== DeliveryPacket（按目标带宽计算间隔发送） ====================
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

//     if (sendQ.empty()) return;

//     // 基于 call 的估计带宽并应用 μ 缩放，得到目标 bps
//     uint32_t original_bw = 0;
//     if (m_call) {
//         original_bw = m_call->last_bandwidth_bps();
//     }
//     uint32_t target_bps = GetScaledBandwidth(original_bw);

//     // 防止除0或无效值：若 target_bps 为 0，则使用一个合理的默认值（例如 1 Mbps）进行发送
//     if (target_bps == 0) {
//         target_bps = 1000000; // 1 Mbps 作为测试 fallback
//         NS_LOG_WARN("WebrtcSender: target_bps was 0, fallback to 1Mbps for pacing");
//     }

//     Time accumulated_delay = Time(0);

//     for (auto& packet : sendQ) {
//         uint32_t pkt_size_bytes = packet->GetSize();
//         // 计算发送此包所需的间隔（ms）
//         double inter_ms = (static_cast<double>(pkt_size_bytes) * 8.0 * 1000.0) / static_cast<double>(target_bps);
//         if (inter_ms < 0.001) inter_ms = 0.001; // 最小间隔以避免零
//         Simulator::ScheduleWithContext(
//             m_context, accumulated_delay,
//             MakeEvent(&WebrtcSender::SendToNetwork, this, packet)
//         );
//         accumulated_delay += MilliSeconds(inter_ms);
//     }
// }

// void WebrtcSender::SendToNetwork(Ptr<Packet> p){
//     NS_ASSERT(p->GetSize()>0);
//     uint64_t send_time=Simulator::Now().GetMilliSeconds();
//     WebrtcTag tag(m_seq,send_time);
//     // 打印使用当前 seq（在 AddPacketTag 之前记录）
//     uint64_t seq_to_report = m_seq;
//     m_seq++;
//     p->AddPacketTag(tag); 
//     // DEBUG 打印，便于验证实际发送时间与 seq
//     std::cout << "[SEND] seq=" << seq_to_report
//               << " t=" << send_time << "ms size=" << p->GetSize()
//               << " μ=" << m_bandwidth_scale_factor << std::endl;
//     if (m_socket) {
//         m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
//     } else {
//         NS_LOG_WARN("WebrtcSender: socket is null in SendToNetwork");
//     }
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

//copilot 版本
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

//     std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
// }

// WebrtcSender::~WebrtcSender(){}

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
//         // 安排延迟以让 transport controller 接口稳定（注意：此函数不会调用不存在的 transport API）
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

// // ==================== RLSM 交互接口 ====================
// void WebrtcSender::SetRLSM(RLSM* rl_manager) {
//     m_rlManager = rl_manager;
//     std::cout << "[DEBUG] WebrtcSender::SetRLSM called, rl_manager=" << rl_manager << std::endl;
// }

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

//     // 触发立即应用（应用层 pacing 使用该值；transport controller API 可能不存在）
//     Simulator::Schedule(Time(MilliSeconds(0)), &WebrtcSender::ApplyBandwidthScalingToController, this);
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
//     // Ensure a valid context for ScheduleWithContext (use node id if available)
//     if (GetNode()) {
//         m_context = static_cast<uint32_t>(GetNode()->GetId());
//     }
//     m_manager->CreateStreamPair();
//     m_manager->Start();
//     NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
//     std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
//     // 尝试在应用层应用带宽缩放（注意：标准 transport controller 可能没有对应的 API）
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

// // ==================== Apply Bandwidth Scaling ====================
// void WebrtcSender::ApplyBandwidthScalingToController() {
//     std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
//     if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
//     auto* transport_controller = m_call->GetTransportControllerSend();
//     if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

//     double current_mu = GetCurrentBandwidthScaleFactor();

//     // NOTE:
//     // The standard webrtc::RtpTransportControllerSendInterface in public WebRTC
//     // does not expose a SetBandwidthScaleFactor(...) method. Calling such a method
//     // will produce a compile error. If you have a custom WebRTC build that adds
//     // that API, call it there. Otherwise, we apply bandwidth scaling at the
//     // application/pacing layer (see DeliveryPacket).
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

// // ==================== DeliveryPacket（按批次总比特分配时间，优先使用已更新的 μ） ====================
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

//     if (sendQ.empty()) return;

//     // 1) 获取当前估计带宽并应用 μ 缩放，得到目标 bps
//     uint32_t original_bw = 0;
//     if (m_call) {
//         original_bw = m_call->last_bandwidth_bps();
//     }

//     // 如果 RLSM 主动推送了值，应当在 RLSM 决策点调用 UpdateBandwidthScaleFactor(new_mu).
//     // 这里我们直接使用当前 m_bandwidth_scale_factor（会被 UpdateBandwidthScaleFactor 更新）。
//     double current_mu = GetCurrentBandwidthScaleFactor();

//     // --- DEBUG: 输出当前 mu 与原始 bw ---
//     // 若你怀疑 original_bw 为0，可在此临时强制一个值用于测试（取消注释）
//     // original_bw = std::max<uint32_t>(original_bw, 2000000);

//     uint32_t target_bps = GetScaledBandwidth(original_bw);

//     // 2) 计算总比特数和需要的总时间，然后均匀分配到每包
//     uint64_t total_bits = 0;
//     for (auto &pkt : sendQ) {
//         total_bits += static_cast<uint64_t>(pkt->GetSize()) * 8ULL;
//     }

//     if (target_bps == 0) {
//         // fallback，避免除0
//         std::cout << "[PACING] target_bps is 0 (original_bw=" << original_bw << "), fallback to 1Mbps" << std::endl;
//         target_bps = 1000000;
//     }

//     double total_ns_d = (static_cast<double>(total_bits) * 1e9) / static_cast<double>(target_bps);
//     uint64_t total_ns = static_cast<uint64_t>(std::max(0.0, std::round(total_ns_d)));
//     size_t n = sendQ.size();
//     uint64_t per_packet_ns = (n > 0) ? static_cast<uint64_t>(std::max(1.0, std::round(static_cast<double>(total_ns) / static_cast<double>(n)))) : 0;

//     // Debug 输出，帮助诊断
//     std::cout << "[PACING] original_bw=" << original_bw << " bps, μ=" << current_mu
//               << ", target_bps=" << target_bps << " bps, queue=" << n
//               << ", total_bits=" << total_bits << " bits"
//               << ", total_ns=" << total_ns << " ns"
//               << ", per_packet_ns=" << per_packet_ns << " ns" << std::endl;

//     // 可选：在调试时，强制最小间隔（例如 1 ms）以便在日志中更容易观察
//     // uint64_t debug_min_ns = 1000000; // 1 ms
//     // per_packet_ns = std::max(per_packet_ns, debug_min_ns);

//     // 3) 基于 per_packet_ns 依次调度
//     uint64_t accumulated_ns = 0;
//     for (auto &pkt : sendQ) {
//         Time delay = NanoSeconds(accumulated_ns);
//         Simulator::ScheduleWithContext(
//             m_context, delay,
//             MakeEvent(&WebrtcSender::SendToNetwork, this, pkt)
//         );
//         accumulated_ns += per_packet_ns;
//     }
// }

// void WebrtcSender::SendToNetwork(Ptr<Packet> p){
//     NS_ASSERT(p->GetSize()>0);
//     uint64_t send_time = Simulator::Now().GetMilliSeconds();
//     WebrtcTag tag(m_seq,send_time);
//     uint64_t seq_to_report = m_seq;
//     m_seq++;
//     p->AddPacketTag(tag); 
//     // DEBUG 打印，便于验证实际发送时间与 seq
//     std::cout << "[SEND] seq=" << seq_to_report
//               << " t=" << send_time << "ms size=" << p->GetSize()
//               << " μ=" << m_bandwidth_scale_factor << std::endl;
//     if (m_socket) {
//         m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
//     } else {
//         NS_LOG_WARN("WebrtcSender: socket is null in SendToNetwork");
//     }
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







////修复版
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
#include <cmath>
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WebrtcSender");

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
    m_rlManager = nullptr;
    m_last_reported_mu = -1.0;

    // pacing/token-bucket 初始化（与现有逻辑兼容）
    m_tokenBits = 0.0;
    m_pacingRateBps = 1000000.0; // 初始 fallback 1 Mbps
    m_lastPacingTime = Simulator::Now();
    m_pacingTickUs = 1000; // 1 ms 默认 tick

    std::cout << "[DEBUG] WebrtcSender constructor completed, initial μ=" << m_bandwidth_scale_factor << std::endl;
}

WebrtcSender::~WebrtcSender(){
    StopPacingLoop();
}

// ==================== 带宽缩放接口 ====================
void WebrtcSender::SetBandwidthScaleFactor(double factor) {
    if (factor > 0.8 && factor <= 1.3) {
        m_bandwidth_scale_factor = factor;
        NS_LOG_INFO("WebrtcSender: Initial bandwidth scale factor set to: " << factor);
    } else {
        NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor << ", using default 1.0");
        m_bandwidth_scale_factor = 1.0;
    }
}

double WebrtcSender::GetBandwidthScaleFactor() const {
    return m_bandwidth_scale_factor;
}

double WebrtcSender::GetCurrentBandwidthScaleFactor() const {
    return m_bandwidth_scale_factor;
}

void WebrtcSender::SetBandwidthScaleFactorDirect(double factor) {
    if (factor > 0.8 && factor <= 1.3) {
        double old_mu = m_bandwidth_scale_factor;
        m_bandwidth_scale_factor = factor;
        std::cout << "=== Direct Bandwidth Scaling ===\n"
                  << "Time: " << Simulator::Now().GetSeconds() << "s\n"
                  << "μ changed: " << old_mu << " -> " << factor << "\n"
                  << "Bandwidth: " << (factor * 100) << "% of original\n"
                  << "=================================" << std::endl;
        NS_LOG_INFO("WebrtcSender: Bandwidth scale factor set directly to: " << factor);
        // 立刻更新 pacing 速率
        UpdateBandwidthScaleFactor(factor);
    } else {
        NS_LOG_WARN("WebrtcSender: Invalid bandwidth scale factor: " << factor);
    }
}

uint32_t WebrtcSender::GetScaledBandwidth(uint32_t original_bw) {
    double current_mu = GetCurrentBandwidthScaleFactor();
    uint32_t scaled_bw = static_cast<uint32_t>(original_bw * current_mu);
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

// ==================== RLSM 交互接口 ====================
void WebrtcSender::SetRLSM(RLSM* rl_manager) {
    m_rlManager = rl_manager;
    std::cout << "[DEBUG] WebrtcSender::SetRLSM called, rl_manager=" << rl_manager << std::endl;
}

// RL 管理器应在每次决策时调用此函数以使发送端立即生效
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

    // 立即更新 pacing 目标速率（如果有估计）
    uint32_t last_est = 0;
    if (m_call) last_est = m_call->last_bandwidth_bps();
    double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
    if (new_rate <= 0.0) new_rate = 1000000.0; // fallback 1 Mbps
    m_pacingRateBps = new_rate;

    // 唤醒/启动 pacing 循环以立即生效
    StartPacingLoop();
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



    int64_t webrtc_send_time_ms = m_clock->TimeInMilliseconds();
    uint64_t ns3_send_time_ms = Simulator::Now().GetMilliSeconds();
    
    NS_LOG_INFO("SendRtp called:");
    NS_LOG_INFO("  - Packet size: " << length << " bytes");
    NS_LOG_INFO("  - Packet ID: " << options.packet_id);
    NS_LOG_INFO("  - WebRTC send time: " << webrtc_send_time_ms << " ms");
    NS_LOG_INFO("  - NS-3 send time: " << ns3_send_time_ms << " ms");
    NS_LOG_INFO("  - Time diff (NS3 - WebRTC): " << (ns3_send_time_ms - webrtc_send_time_ms) << " ms");
    
    // 解析RTP头部获取序列号
    if (length >= 4) {
        uint16_t seq_num = (packet[2] << 8) | packet[3];
        NS_LOG_INFO("  - RTP sequence: " << seq_num);
    }

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
    // 临时修复：禁用所有限制
    // m_bandwidth_scale_factor = 1.0;  // 禁用缩放
    // m_pacingRateBps = 0;             // 禁用pacing（0表示无限制）
    // m_pacingTickUs = 0;              // 禁用pacing tick
    
    std::cout << "[FIX] DISABLED ALL LIMITATIONS FOR DEBUGGING:" << std::endl;
    std::cout << "[FIX]   Bandwidth scaling: DISABLED (μ=1.0)" << std::endl;
    std::cout << "[FIX]   Pacing: DISABLED" << std::endl;
    std::cout << "[FIX]   WebRTC will send at full rate" << std::endl;
    
    m_manager->CreateStreamPair();
    m_manager->Start();

    
    // Ensure a valid context for ScheduleWithContext (use node id if available)
    if (GetNode()) {
        m_context = static_cast<uint32_t>(GetNode()->GetId());
    }
    m_manager->CreateStreamPair();
    m_manager->Start();
    NS_LOG_INFO("WebrtcSender: Starting application with transport feedback bandwidth scaling");
    std::cout << "WebrtcSender: Starting with bandwidth scale factor μ=" << m_bandwidth_scale_factor << std::endl;
    // 启动 pacing 循环（如果有数据，会立即发送或排队）
    StartPacingLoop();
    // 尝试在应用层应用带宽缩放（注意：标准 transport controller 可能没有对应的 API）
    Simulator::Schedule(Time(MilliSeconds(2000)), &WebrtcSender::ApplyBandwidthScalingToController, this);
        // 添加定期状态检查

}



void WebrtcSender::StopApplication(){
    std::cout << "[DEBUG] StopApplication called" << std::endl;
    m_running=false;
    StopPacingLoop();
    if (m_manager) m_manager->Stop();
    if (m_socket) { m_socket->Close(); m_socket = nullptr; }
    {
        LockScope ls(&m_rtpLock); m_rtpQ.clear();
    }
    {
        LockScope ls(&m_rtcpLock); m_rtcpQ.clear();
    }
    {
        // 清空 pacing 队列
        LockScope ls(&m_rtpLock);
        m_pacingQueue.clear();
    }
    NS_LOG_INFO("WebrtcSender: Stopping application, final bandwidth scale factor μ=" << m_bandwidth_scale_factor);
    if (m_bandwidth_scale_factor != 1.0) {
        std::cout << "WebrtcSender: Bandwidth scaling completed with μ=" << m_bandwidth_scale_factor << std::endl;
    }
    std::cout << "WebrtcSender: Application stopped" << std::endl;
}

// ==================== Apply Bandwidth Scaling ====================
void WebrtcSender::ApplyBandwidthScalingToController() {
    std::cout << "[DEBUG] ApplyBandwidthScalingToController called" << std::endl;
    if(!m_call){ std::cout<<"[DEBUG] m_call is null!"<<std::endl; return; }
    auto* transport_controller = m_call->GetTransportControllerSend();
    if(!transport_controller){ std::cout<<"[DEBUG] transport_controller is null!"<<std::endl; return; }

    double current_mu = GetCurrentBandwidthScaleFactor();

    NS_LOG_INFO("WebrtcSender: (local) Apply bandwidth scaling μ=" << current_mu);
    std::cout << "=== WebrtcSender: (local) Applied Bandwidth Scaling (no transport API) ===\n"
              << "Time: " << Simulator::Now().GetSeconds() << "s\n"
              << "Bandwidth scale factor μ: " << current_mu << "\n"
              << "================================================" << std::endl;
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

// ==================== Pacing / Token-bucket 实现（与现有 DeliveryPacket 接口兼容） ====================
void WebrtcSender::StartPacingLoop() {
    // 如果事件已存在并在运行则返回
    if (m_pacingEvent.IsRunning()) return;

    // 初始化 token/time
    m_lastPacingTime = Simulator::Now();
    // 使用最新估计更新 pacing 速率
    if (m_call) {
        uint32_t last_est = m_call->last_bandwidth_bps();
        double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
        if (new_rate <= 0.0) new_rate = 1000000.0;
        m_pacingRateBps = new_rate;
    }
    // 立即安排一次 tick（注意：使用 Schedule 而不是 ScheduleWithContext，因为在你的 ns-3 版本 ScheduleWithContext 返回 void）
    m_pacingEvent = Simulator::Schedule(NanoSeconds(0), MakeEvent(&WebrtcSender::PacingTick, this));
}

void WebrtcSender::StopPacingLoop() {
    if (m_pacingEvent.IsRunning()) {
        m_pacingEvent.Cancel();
    }
    m_tokenBits = 0.0;
}

void WebrtcSender::PacingTick() {
    // 计算时间差并增加 token
    Time now = Simulator::Now();
    uint64_t elapsed_ns = 0;
    if (now >= m_lastPacingTime) {
        elapsed_ns = (now - m_lastPacingTime).GetNanoSeconds();
    } else {
        elapsed_ns = 0;
    }
    if (elapsed_ns > 0) {
        double added_bits = (static_cast<double>(elapsed_ns) / 1e9) * m_pacingRateBps;
        m_tokenBits += added_bits;
        m_lastPacingTime = now;
    }

    // 如果有新的估计，更新 pacing 速率（以便 mu 变化或 estimator 更新生效）
    if (m_call) {
        uint32_t last_est = m_call->last_bandwidth_bps();
        double new_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
        if (new_rate <= 0.0) new_rate = 1000000.0;
        m_pacingRateBps = new_rate;
    }

    // 把 rtp/rtcp 缓冲区的包移动到 pacing 队列（线程安全）
    {
        LockScope ls(&m_rtpLock);
        while(!m_rtpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            m_pacingQueue.push_back(packet);
            m_rtpQ.pop_front();
        }
    }
    {
        LockScope ls(&m_rtcpLock);
        while(!m_rtcpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            m_pacingQueue.push_back(packet);
            m_rtcpQ.pop_front();
        }
    }

    // 尽可能发送队列中的包
    while(!m_pacingQueue.empty()) {
        Ptr<Packet> pkt = m_pacingQueue.front();
        uint64_t pkt_bits = static_cast<uint64_t>(pkt->GetSize()) * 8ULL;
        if (m_tokenBits + 1e-9 >= static_cast<double>(pkt_bits)) {
            m_tokenBits -= static_cast<double>(pkt_bits);
            m_pacingQueue.pop_front();
            // 发送包（保持原有 SendToNetwork 语义）
            SendToNetwork(pkt);
        } else {
            break;
        }
    }

    // 计划下一次 tick：如果队列非空，计算等待到下一个包所需时间；若空，则周期性唤醒
    if (!m_pacingQueue.empty()) {
        Ptr<Packet> next_pkt = m_pacingQueue.front();
        uint64_t next_bits = static_cast<uint64_t>(next_pkt->GetSize()) * 8ULL;
        double bits_needed = static_cast<double>(next_bits) - m_tokenBits;
        if (bits_needed < 0.0) bits_needed = 0.0;
        double time_needed_s = bits_needed / m_pacingRateBps;
        uint64_t time_needed_ns = static_cast<uint64_t>(std::max(1.0, std::round(time_needed_s * 1e9)));
        uint64_t min_tick_ns = static_cast<uint64_t>(m_pacingTickUs) * 1000ULL;
        uint64_t schedule_ns = std::max(min_tick_ns, time_needed_ns);
        // 使用 Simulator::Schedule，因为某些 ns-3 版本 ScheduleWithContext 返回 void
        m_pacingEvent = Simulator::Schedule(NanoSeconds(schedule_ns), MakeEvent(&WebrtcSender::PacingTick, this));
    } else {
        uint64_t idle_ns = static_cast<uint64_t>(m_pacingTickUs) * 1000ULL;
        m_pacingEvent = Simulator::Schedule(NanoSeconds(idle_ns), MakeEvent(&WebrtcSender::PacingTick, this));
    }
}

// ==================== DeliveryPacket（兼容原有逻辑：把包放入 pacing 队列） ====================
void WebrtcSender::DeliveryPacket() {
    // 把 rtp/rtcp 队列中内容转入 pacing 队列，PacingTick 将处理发送
    {
        LockScope ls(&m_rtpLock);
        while(!m_rtpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            m_pacingQueue.push_back(packet);
            m_rtpQ.pop_front();
        }
    }
    {
        LockScope ls(&m_rtcpLock);
        while(!m_rtcpQ.empty()){
            rtc::CopyOnWriteBuffer buffer = m_rtcpQ.front();
            Ptr<Packet> packet = Create<Packet>(buffer.data(), buffer.size());
            m_pacingQueue.push_back(packet);
            m_rtcpQ.pop_front();
        }        
    }

    // 更新 pacing 速率（使用最新估计与 μ）
    uint32_t last_est = 0;
    if (m_call) last_est = m_call->last_bandwidth_bps();
    double computed_rate = static_cast<double>(last_est) * m_bandwidth_scale_factor;
    if (computed_rate > 0.0) {
        m_pacingRateBps = computed_rate;
    } else {
        // 保持当前 m_pacingRateBps 或 fallback
        if (m_pacingRateBps <= 0.0) m_pacingRateBps = 1000000.0;
    }

    // 唤醒/启动 pacing 循环
    StartPacingLoop();
}

// ==================== 发送 / 接收 ====================
void WebrtcSender::SendToNetwork(Ptr<Packet> p){
    NS_ASSERT(p->GetSize()>0);
    uint64_t send_time=Simulator::Now().GetMilliSeconds();
    WebrtcTag tag(m_seq,send_time);
    uint64_t seq_to_report = m_seq;
    m_seq++;
    p->AddPacketTag(tag); 
    // DEBUG 打印，便于验证实际发送时间与 seq、速率和令牌
    std::cout << "[SEND] seq=" << seq_to_report
              << " t=" << send_time << "ms size=" << p->GetSize()
              << " μ=" << m_bandwidth_scale_factor
              << " rate=" << static_cast<uint32_t>(m_pacingRateBps) << "bps"
              << " tokens=" << static_cast<uint64_t>(m_tokenBits) << std::endl;
    if (m_socket) {
        m_socket->SendTo(p,0,InetSocketAddress{m_peerIp,m_peerPort});
    } else {
        NS_LOG_WARN("WebrtcSender: socket is null in SendToNetwork");
    }
}

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

void WebrtcSender::RecvPacket(Ptr<Socket> socket) {
    if (!m_running) return;
    
    Address remoteAddr;
    auto packet = socket->RecvFrom(remoteAddr);
    if (!packet) return;
    
    uint32_t recv = packet->GetSize();
    if (recv == 0) return;
    
    NS_ASSERT(recv <= 1500);
    
    uint64_t recv_time_ms = Simulator::Now().GetMilliSeconds();
    
    uint8_t buf[1500] = {'\0'};
    packet->CopyData(buf, recv);
    
    // 简化的处理：只修改TransportFeedback包的基本时间戳
    bool is_rtcp = webrtc::RtpHeaderParser::IsRtcp(buf, recv);
    
    if (is_rtcp && recv >= 14) {
        uint8_t packet_type = buf[1];
        
        if (packet_type == 205) { // Transport Feedback
            // 只进行最基本的时间戳修复
            SimpleFixTransportFeedback(buf, recv, recv_time_ms);
        }
    }
    
    // 转发包
    rtc::CopyOnWriteBuffer packet_data(buf, recv);
    webrtc::EmulatedIpPacket emu_packet(
        rtc::SocketAddress(),
        rtc::SocketAddress(),
        std::move(packet_data),
        m_clock->CurrentTime(),
        m_packetOverhead
    );
    
    m_client->OnPacketReceived(std::move(emu_packet));
}

// 简化的修复函数
void WebrtcSender::SimpleFixTransportFeedback(uint8_t* packet, size_t length, uint64_t recv_time_ms) {
    if (length < 14) return;
    
    uint8_t packet_type = packet[1];
    if (packet_type != 205) return; // 只处理Transport Feedback (205)
    
    // 解析现有时间戳
    uint32_t ref_time_original = (packet[10] << 16) | (packet[11] << 8) | packet[12];
    
    // 计算新的时间戳：假设在15-25ms前发送
    uint64_t estimated_send_time = recv_time_ms - 20;
    uint32_t new_ref_time = (estimated_send_time * 1000) / 250; // 转换为250us单位
    new_ref_time &= 0x00FFFFFF;  // 24位
    
    // 更新包
    packet[10] = (new_ref_time >> 16) & 0xFF;
    packet[11] = (new_ref_time >> 8) & 0xFF;
    packet[12] = new_ref_time & 0xFF;
    
    NS_LOG_INFO("Fixed TransportFeedback timestamp: " << ref_time_original 
               << " -> " << new_ref_time << " (recv_time=" << recv_time_ms << "ms)");
}

// 添加诊断函数到类定义中
void WebrtcSender::PerformFullDiagnostics(uint64_t current_time_ms) {
    std::cout << "\n====== COMPLETE DIAGNOSTICS ======" << std::endl;
    std::cout << "Diagnostic time: " << current_time_ms << " ms" << std::endl;
    std::cout << "Running: " << (m_running ? "YES" : "NO") << std::endl;
    std::cout << "Call object: " << (m_call ? "VALID" : "NULL") << std::endl;
    std::cout << "Client object: " << (m_client ? "VALID" : "NULL") << std::endl;
    std::cout << "Socket: " << (m_socket ? "VALID" : "NULL") << std::endl;
    
    if (m_call) {
        uint32_t bw = m_call->last_bandwidth_bps();
        std::cout << "Current GCC bandwidth: " << bw << " bps" << std::endl;
        
        // 尝试获取更多信息（如果可用）
        std::cout << "Bandwidth scaling factor μ: " << m_bandwidth_scale_factor << std::endl;
        std::cout << "Scaled target: " << GetScaledBandwidth(bw) << " bps" << std::endl;
    }
    
    // 队列状态
    {
        LockScope ls(&m_rtpLock);
        std::cout << "RTP queue: " << m_rtpQ.size() << " packets" << std::endl;
    }
    {
        LockScope ls(&m_rtcpLock);
        std::cout << "RTCP queue: " << m_rtcpQ.size() << " packets" << std::endl;
    }
    std::cout << "Pacing queue: " << m_pacingQueue.size() << " packets" << std::endl;
    
    // 网络信息
    std::cout << "Peer IP: " << m_peerIp << ", Port: " << m_peerPort << std::endl;
    std::cout << "Local bind port: " << m_bindPort << std::endl;
    std::cout << "Packet overhead: " << m_packetOverhead << " bytes" << std::endl;
    
    std::cout << "==================================\n" << std::endl;
}

void WebrtcSender::CheckAndFixPacingRate() {
    std::cout << "\n=== PACING RATE DIAGNOSIS ===" << std::endl;
    std::cout << "[DIAG] Current pacing rate: " << m_pacingRateBps << " bps" << std::endl;
    std::cout << "[DIAG] GCC bandwidth: " << (m_call ? m_call->last_bandwidth_bps() : 0) << " bps" << std::endl;
    std::cout << "[DIAG] Bandwidth scale factor μ: " << m_bandwidth_scale_factor << std::endl;
    
    // 如果pacing rate太低，强制提高
    if (m_pacingRateBps < 500000) {  // 低于500kbps
        std::cout << "[FIX] Pacing rate too low, setting to 1 Mbps" << std::endl;
        m_pacingRateBps = 1000000;  // 1 Mbps
        
        // 立即重启pacing循环
        StopPacingLoop();
        StartPacingLoop();
    }
    
    std::cout << "============================\n" << std::endl;
}



} // namespace ns3