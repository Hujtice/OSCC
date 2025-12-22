// #pragma once 
// #include <deque>
// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"
// namespace ns3{
// //DataSize::Bytes(PacketOverhead::kDefault)
// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
//     //can not use here
//     void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
//   bool SendRtp(const uint8_t* packet,
//                size_t length,
//                const webrtc::PacketOptions& options) override;
//   bool SendRtcp(const uint8_t* packet, size_t length) override;

//   webrtc::Call* m_call{nullptr};

// private:
// 	virtual void StartApplication() override;
// 	virtual void StopApplication() override;
//     void NotifyRouteChange();
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);
//     bool m_running{false};
//     WebrtcSessionManager *m_manager{nullptr};
//     webrtc::Clock *m_clock;
//     uint16_t m_bindPort;
//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort;
//     webrtc::test::CallClient *m_client{nullptr};
//     uint64_t m_seq{1};
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
//     int64_t m_lastTraceTime{0};
//     uint32_t m_context=0;
//     TraceBandwidth m_traceBw;
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};
// };   
// }



// #pragma once 
// #include <deque>
// #include <random>
// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3{

// // 新增：带宽缩放回调类型
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明 RLSM 类
// class RLSM;

// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
    
//     // 新增：设置带宽缩放系数和回调
//     void SetBandwidthScaleFactor(double factor);
//     double GetBandwidthScaleFactor() const;
//     uint32_t GetScaledBandwidth(uint32_t original_bw);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);  // 新增缩放带宽回调
    
//     // 新增：RL状态管理器相关方法
//     void SetRLSM(RLSM* rl_manager);
//     void UpdateBandwidthScaleFactor(double new_factor);
//     double GetCurrentBandwidthScaleFactor() const;
    
//     //can not use here
//     void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
//   bool SendRtp(const uint8_t* packet,
//                size_t length,
//                const webrtc::PacketOptions& options) override;
//   bool SendRtcp(const uint8_t* packet, size_t length) override;
// private:
// 	virtual void StartApplication() override;
// 	virtual void StopApplication() override;
//     void NotifyRouteChange();
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);
    
//     // 新增：带宽限制检查方法
//     bool ShouldSendPacket(size_t packet_size);
//     bool ShouldSendPacketByProbability();

//     bool m_running{false};
//     WebrtcSessionManager *m_manager{nullptr};
//     webrtc::Clock *m_clock;
//     uint16_t m_bindPort;
//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort;
//     webrtc::test::CallClient *m_client{nullptr};
//     webrtc::Call* m_call{nullptr};
//     uint64_t m_seq{1};
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
//     int64_t m_lastTraceTime{0};
//     uint32_t m_context=0;
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;  // 新增缩放带宽回调
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};
    
//     // 新增：带宽缩放相关成员变量
//     double m_bandwidth_scale_factor{1.0};  // 带宽缩放系数 μ
//     uint32_t m_last_original_bandwidth{0}; // 上次原始带宽
//     uint32_t m_last_scaled_bandwidth{0};   // 上次缩放后带宽
//     uint32_t m_current_scaled_bandwidth{0}; // 当前使用的缩放带宽
//     uint64_t m_accumulated_bytes{0};       // 当前时间窗口内已发送的字节数
//     uint64_t m_last_bandwidth_check_time{0}; // 上次带宽检查时间
    
//     // 新增：RL状态管理器
//     RLSM* m_rlManager{nullptr};
//     double m_last_reported_mu{-1.0};       // 记录上次报告的μ值
// };   
// }


// #pragma once 
// #include <deque>
// #include <random>
// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3{

// // 新增：带宽缩放回调类型
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明 RLSM 类
// class RLSM;

// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
    
//     // 新增：设置带宽缩放系数和回调
//     void SetBandwidthScaleFactor(double factor);
//     double GetBandwidthScaleFactor() const;
//     uint32_t GetScaledBandwidth(uint32_t original_bw);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
//     // 新增：RL状态管理器相关方法
//     void SetRLSM(RLSM* rl_manager);
//     void UpdateBandwidthScaleFactor(double new_factor);
//     double GetCurrentBandwidthScaleFactor() const;
    
//     //can not use here
//     void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
//   bool SendRtp(const uint8_t* packet,
//                size_t length,
//                const webrtc::PacketOptions& options) override;
//   bool SendRtcp(const uint8_t* packet, size_t length) override;
// private:
// 	virtual void StartApplication() override;
// 	virtual void StopApplication() override;
//     void NotifyRouteChange();
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);
    
//     // 新增：带宽限制检查方法
//     bool ShouldSendPacket(size_t packet_size);
//     void UpdateBandwidthLimiter();
//     void ResetBandwidthLimiter();

//     bool m_running{false};
//     WebrtcSessionManager *m_manager{nullptr};
//     webrtc::Clock *m_clock;
//     uint16_t m_bindPort;
//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort;
//     webrtc::test::CallClient *m_client{nullptr};
//     webrtc::Call* m_call{nullptr};
//     uint64_t m_seq{1};
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
//     int64_t m_lastTraceTime{0};
//     uint32_t m_context=0;
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};
    
//     // 修改：带宽缩放相关成员变量 - 重新设计
//     double m_bandwidth_scale_factor{1.0};  // 带宽缩放系数 μ
//     uint32_t m_current_estimated_bandwidth{0}; // 当前GCC估计的带宽
//     uint32_t m_current_scaled_bandwidth{0};    // 当前应用的缩放带宽
    
//     // 新增：带宽限制器状态
//     uint64_t m_accumulated_bytes{0};           // 当前时间窗口内已发送的字节数
//     uint64_t m_last_bandwidth_check_time{0};   // 上次带宽检查时间
//     uint64_t m_dropped_packets{0};             // 统计丢弃的包数量
//     uint64_t m_total_packets{0};               // 统计总包数量
    
//     // 新增：RL状态管理器
//     RLSM* m_rlManager{nullptr};
//     double m_last_reported_mu{-1.0};
    
//     // 新增：带宽限制器配置
//     static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100; // 带宽检查间隔
//     static const double BANDWIDTH_TOLERANCE; // 带宽容忍度
// };   
// }



// #pragma once 
// #include <deque>
// #include <random>
// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3{

// // 新增：带宽缩放回调类型
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明 RLSM 类
// class RLSM;

// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
    
//     // 新增：设置带宽缩放系数和回调
//     void SetBandwidthScaleFactor(double factor);
//     double GetBandwidthScaleFactor() const;
//     uint32_t GetScaledBandwidth(uint32_t original_bw);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
//     // 新增：RL状态管理器相关方法
//     void SetRLSM(RLSM* rl_manager);
//     void UpdateBandwidthScaleFactor(double new_factor);
//     double GetCurrentBandwidthScaleFactor() const;
    
//     // 新增：带宽缩放集成方法
//     void ApplyBandwidthScalingToController();
//     void VerifyBandwidthScaling();
//     void ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps);
//     void ApplyPeriodicBandwidthScaling();


//     // Application 接口方法
//     virtual void StartApplication() override;
//     virtual void StopApplication() override;

//     // TransportBase 接口方法
//     void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
//     bool SendRtp(const uint8_t* packet,
//                  size_t length,
//                  const webrtc::PacketOptions& options) override;
//     bool SendRtcp(const uint8_t* packet, size_t length) override;
    
// private:
//     void NotifyRouteChange();
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);
    
//     bool ShouldSendPacket(size_t packet_size);
//     void UpdateBandwidthLimiter();
//     void ResetBandwidthLimiter();

//     bool m_running{false};
//     WebrtcSessionManager *m_manager{nullptr};
//     webrtc::Clock *m_clock;
//     uint16_t m_bindPort;
//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort;
//     webrtc::test::CallClient *m_client{nullptr};
//     webrtc::Call* m_call{nullptr};
//     uint64_t m_seq{1};
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
//     int64_t m_lastTraceTime{0};
//     uint32_t m_context=0;
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};
    
//     // 带宽缩放相关成员变量
//     double m_bandwidth_scale_factor{1.0};
//     uint32_t m_current_estimated_bandwidth{0};
//     uint32_t m_current_scaled_bandwidth{0};
    
//     // RL状态管理器
//     RLSM* m_rlManager{nullptr};
//     double m_last_reported_mu{-1.0};
    
//     static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100;
//     static const double BANDWIDTH_TOLERANCE;
// };   
// }


// #pragma once

// #include <deque>
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/callback.h"
// #include "ns3/event-id.h"
// #include "ns3/atomic-lock.h"

// #include "ns3/webrtc-config.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3 {

// // ====================== Trace 回调类型 ======================
// typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;
// // now, original_bw, scaled_bw, mu
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明
// class RLSM;
// class WebrtcSessionManager;

// // ===========================================================
// // WebrtcSender
// // ===========================================================
// class WebrtcSender :
//     public webrtc::test::TransportBase,
//     public Application
// {
// public:
//     explicit WebrtcSender(WebrtcSessionManager* manager);
//     ~WebrtcSender() override;

//     // ---------------- Application ----------------
//     void StartApplication() override;
//     void StopApplication() override;

//     // ---------------- TransportBase ----------------
//     void Construct(webrtc::Clock*, webrtc::Call*) override {}
//     bool SendRtp(const uint8_t* packet,
//                  size_t length,
//                  const webrtc::PacketOptions& options) override;
//     bool SendRtcp(const uint8_t* packet, size_t length) override;

//     // ---------------- Socket / Network ----------------
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr, uint16_t port);
//     InetSocketAddress GetLocalAddress();

//     // ---------------- Bandwidth tracing ----------------
//     void SetBwTraceFuc(TraceBandwidth cb);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);

//     // ---------------- μ 控制接口（核心） ----------------
//     void SetBandwidthScaleFactor(double factor);
//     void SetBandwidthScaleFactorDirect(double factor);
//     double GetBandwidthScaleFactor() const;
//     double GetCurrentBandwidthScaleFactor() const;

//     uint32_t GetScaledBandwidth(uint32_t original_bw);

//     // ---------------- RL 接入 ----------------
//     void SetRLSM(RLSM* rl_manager);

//     // ---------------- 与 GCC 的“逻辑同步”（非真实调速） ----------------
//     void ApplyBandwidthScalingToController();

// private:
//     // ================= 内部发送流程 =================
//     void NotifyRouteChange();
//     void DeliveryPacket();                 // ⭐ μ 在这里真正生效
//     void SendToNetwork(Ptr<Packet> packet);
//     void RecvPacket(Ptr<Socket> socket);

// private:
//     // ================= 运行状态 =================
//     bool m_running{false};

//     // ================= WebRTC / NS3 =================
//     WebrtcSessionManager* m_manager{nullptr};
//     webrtc::test::CallClient* m_client{nullptr};
//     webrtc::Call* m_call{nullptr};
//     webrtc::Clock* m_clock{nullptr};

//     Ptr<Socket> m_socket;
//     uint16_t m_bindPort{0};
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort{0};

//     uint32_t m_context{0};
//     uint64_t m_seq{1};

//     // ================= RTP / RTCP Queue =================
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;

//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;

//     // ================= Trace =================
//     int64_t m_lastTraceTime{0};
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;

//     // ================= 带宽 & μ =================
//     double   m_bandwidth_scale_factor{1.0};
//     uint32_t m_current_estimated_bandwidth{0};
//     uint32_t m_current_scaled_bandwidth{0};

//     // ================= Packet / Route =================
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};

//     // ================= RL =================
//     RLSM* m_rlManager{nullptr};
// };

// } // namespace ns3


// #pragma once

// #include <deque>
// #include <random>

// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"

// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3 {

// // 原始带宽 trace
// typedef Callback<void, uint32_t, uint32_t> TraceBandwidth;

// // 带 μ 缩放后的带宽 trace
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明
// class RLSM;

// class WebrtcSender : public webrtc::test::TransportBase,
//                      public Application {
// public:
//     explicit WebrtcSender(WebrtcSessionManager* manager);
//     ~WebrtcSender() override;

//     // socket / peer
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr, uint16_t port);

//     // trace
//     void SetBwTraceFuc(TraceBandwidth cb);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);

//     // μ 控制接口
//     void SetBandwidthScaleFactor(double mu);
//     double GetCurrentBandwidthScaleFactor() const;

//     // RL
//     void SetRLSM(RLSM* rl_manager);

//     // Application
//     void StartApplication() override;
//     void StopApplication() override;

//     // TransportBase
//     void Construct(webrtc::Clock*, webrtc::Call*) override {}
//     bool SendRtp(const uint8_t* packet,
//                  size_t length,
//                  const webrtc::PacketOptions& options) override;
//     bool SendRtcp(const uint8_t* packet, size_t length) override;

// private:
//     // 内部逻辑
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);

// private:
//     bool m_running{false};

//     WebrtcSessionManager* m_manager{nullptr};

//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort{0};
//     uint16_t m_bindPort{0};

//     uint32_t m_context{0};

//     // RTP / RTCP 队列
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;

//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;

//     // trace
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;

//     // μ 相关
//     double m_bandwidth_scale_factor{1.0};
//     double m_last_reported_mu{-1.0};

//     // 关键：跨 DeliveryPacket 的发送时间轴 
//     Time m_next_send_time;

//     // RL
//     RLSM* m_rlManager{nullptr};

//     // pacing 参数
//     static constexpr double BASE_PACING_INTERVAL_MS = 10.0;
// };

// } // namespace ns3


// #pragma once 
// #include <deque>
// #include <random>
// #include "ns3/event-id.h"
// #include "ns3/callback.h"
// #include "ns3/application.h"
// #include "ns3/socket.h"
// #include "ns3/network-module.h"
// #include "ns3/internet-module.h"
// #include "ns3/webrtc-config.h"
// #include "ns3/atomic-lock.h"
// #include "test/scenario/transport_base.h"
// #include "call/call.h"

// namespace ns3{

// // 新增：带宽缩放回调类型
// typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// // 前向声明 RLSM 类
// class RLSM;

// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
    
//     // 新增：设置带宽缩放系数和回调
//     void SetBandwidthScaleFactor(double factor);
//     void SetBandwidthScaleFactorDirect(double factor);  // 新增这一行
//     double GetBandwidthScaleFactor() const;
//     uint32_t GetScaledBandwidth(uint32_t original_bw);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
//     // 新增：RL状态管理器相关方法
//     void SetRLSM(RLSM* rl_manager);
//     void UpdateBandwidthScaleFactor(double new_factor);
//     double GetCurrentBandwidthScaleFactor() const;
    
//     // 新增：带宽缩放集成方法
//     void ApplyBandwidthScalingToController();
//     void VerifyBandwidthScaling();
//     void ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps);
//     void ApplyPeriodicBandwidthScaling();


//     // Application 接口方法
//     virtual void StartApplication() override;
//     virtual void StopApplication() override;

//     // TransportBase 接口方法
//     void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
//     bool SendRtp(const uint8_t* packet,
//                  size_t length,
//                  const webrtc::PacketOptions& options) override;
//     bool SendRtcp(const uint8_t* packet, size_t length) override;
    
// private:
//     void NotifyRouteChange();
//     void DeliveryPacket();
//     void SendToNetwork(Ptr<Packet> p);
//     void RecvPacket(Ptr<Socket> socket);
    
//     bool ShouldSendPacket(size_t packet_size);
//     void UpdateBandwidthLimiter();
//     void ResetBandwidthLimiter();

//     bool m_running{false};
//     WebrtcSessionManager *m_manager{nullptr};
//     webrtc::Clock *m_clock;
//     uint16_t m_bindPort;
//     Ptr<Socket> m_socket;
//     Ipv4Address m_peerIp;
//     uint16_t m_peerPort;
//     webrtc::test::CallClient *m_client{nullptr};
//     webrtc::Call* m_call{nullptr};
//     uint64_t m_seq{1};
//     AtomicLock m_rtpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
//     AtomicLock m_rtcpLock;
//     std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
//     int64_t m_lastTraceTime{0};
//     uint32_t m_context=0;
//     TraceBandwidth m_traceBw;
//     TraceScaledBandwidth m_traceScaledBw;
//     uint32_t m_packetOverhead{0};
//     uint32_t m_initial_time{0};
    
//     // 带宽缩放相关成员变量
//     double m_bandwidth_scale_factor{1.0};
//     uint32_t m_current_estimated_bandwidth{0};
//     uint32_t m_current_scaled_bandwidth{0};
    
//     // RL状态管理器
//     RLSM* m_rlManager{nullptr};
//     double m_last_reported_mu{-1.0};
    
//     static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100;
//     static const double BANDWIDTH_TOLERANCE;
// };   
// }


#pragma once 
#include <deque>
#include <random>
#include "ns3/event-id.h"
#include "ns3/callback.h"
#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/webrtc-config.h"
#include "ns3/atomic-lock.h"
#include "test/scenario/transport_base.h"
#include "call/call.h"

namespace ns3{

// 新增：带宽缩放回调类型
typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// 前向声明 RLSM 类
class RLSM;

class WebrtcSender:public webrtc::test::TransportBase,public Application{
public:
    WebrtcSender(WebrtcSessionManager *manager);
    ~WebrtcSender() override;
    InetSocketAddress GetLocalAddress();
    void Bind(uint16_t port);
    void ConfigurePeer(Ipv4Address addr,uint16_t port);
    typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
    void SetBwTraceFuc(TraceBandwidth cb);
    
    // 新增：设置带宽缩放系数和回调
    void SetBandwidthScaleFactor(double factor);
    void SetBandwidthScaleFactorDirect(double factor);  // 新增这一行
    double GetBandwidthScaleFactor() const;
    uint32_t GetScaledBandwidth(uint32_t original_bw);
    void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
    // 新增：RL状态管理器相关方法
    void SetRLSM(RLSM* rl_manager);
    void UpdateBandwidthScaleFactor(double new_factor);
    double GetCurrentBandwidthScaleFactor() const;
    
    // 新增：带宽缩放集成方法
    void ApplyBandwidthScalingToController();
    void VerifyBandwidthScaling();
    void ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps);
    void ApplyPeriodicBandwidthScaling();


    // Application 接口方法
    virtual void StartApplication() override;
    virtual void StopApplication() override;

    // TransportBase 接口方法
    void Construct(webrtc::Clock* sender_clock, webrtc::Call* sender_call) override{}
    bool SendRtp(const uint8_t* packet,
                 size_t length,
                 const webrtc::PacketOptions& options) override;
    bool SendRtcp(const uint8_t* packet, size_t length) override;
    
private:
    void NotifyRouteChange();
    void DeliveryPacket();
    void SendToNetwork(Ptr<Packet> p);
    void RecvPacket(Ptr<Socket> socket);
    
    bool ShouldSendPacket(size_t packet_size);
    void UpdateBandwidthLimiter();
    void ResetBandwidthLimiter();

    // 新增：Pacing / Token-bucket 成员与方法（为了最小改动，保留原有接口，同时增加内部 pacing）
    void StartPacingLoop();
    void StopPacingLoop();
    void PacingTick(); // 被调度的成员方法
    std::deque<Ptr<Packet>> m_pacingQueue;
    EventId m_pacingEvent;
    double m_tokenBits;               // 当前令牌（比特）
    double m_pacingRateBps;           // 当前 pacing 目标速率（bps）
    Time m_lastPacingTime;            // 上次补充令牌时间
    uint32_t m_pacingTickUs{1000};    // 默认 1ms tick

    bool m_running{false};
    WebrtcSessionManager *m_manager{nullptr};
    webrtc::Clock *m_clock;
    uint16_t m_bindPort;
    Ptr<Socket> m_socket;
    Ipv4Address m_peerIp;
    uint16_t m_peerPort;
    webrtc::test::CallClient *m_client{nullptr};
    webrtc::Call* m_call{nullptr};
    uint64_t m_seq{1};
    AtomicLock m_rtpLock;
    std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
    AtomicLock m_rtcpLock;
    std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
    int64_t m_lastTraceTime{0};
    uint32_t m_context=0;
    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    uint32_t m_packetOverhead{0};
    uint32_t m_initial_time{0};
    
    // 带宽缩放相关成员变量
    double m_bandwidth_scale_factor{1.0};
    uint32_t m_current_estimated_bandwidth{0};
    uint32_t m_current_scaled_bandwidth{0};
    
    // RL状态管理器
    RLSM* m_rlManager{nullptr};
    double m_last_reported_mu{-1.0};
    
    static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100;
    static const double BANDWIDTH_TOLERANCE;
};   
}