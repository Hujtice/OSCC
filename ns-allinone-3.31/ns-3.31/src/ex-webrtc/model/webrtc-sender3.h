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
// // class RLSM;

// class WebrtcSender:public webrtc::test::TransportBase,public Application{
// public:
//     WebrtcSender(WebrtcSessionManager *manager);
//     ~WebrtcSender() override;
//     InetSocketAddress GetLocalAddress();
//     void Bind(uint16_t port);
//     void ConfigurePeer(Ipv4Address addr,uint16_t port);
//     typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
//     void SetBwTraceFuc(TraceBandwidth cb);
//     void EnableAdaptiveMu(bool enable) { m_adaptive_mu = enable; }
    
//     // 新增：设置带宽缩放系数和回调
//     void SetBandwidthScaleFactor(double factor);
//     double GetBandwidthScaleFactor() const;
//     uint32_t GetScaledBandwidth(uint32_t original_bw);
//     void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
//     // 新增：RL状态管理器相关方法
//     // void SetRLSM(RLSM* rl_manager);
//     void UpdateBandwidthScaleFactor(double new_factor);
//     double GetCurrentBandwidthScaleFactor() const;
    
//     // 新增：带宽缩放集成方法
//     void ApplyBandwidthScalingToController();
//     void VerifyBandwidthScaling();
//     void ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps);
//     void ApplyPeriodicBandwidthScaling();
//     void SetBandwidthScaleFactorDirect(double factor);

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
//     // double m_bandwidth_scale_factor;
//     // RL状态管理器
//     // RLSM* m_rlManager{nullptr};
//     double m_last_reported_mu{-1.0};
    
//     static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100;
//     static const double BANDWIDTH_TOLERANCE;

//     bool m_adaptive_mu{false};
//     double ComputeMuBasedOnNetwork(); // 根据网络状态计算μ
//     void UpdateAdaptiveMu();          // 周期性更新μ
//     // void SetBandwidthScaleFactorDirect(double factor); // 直接设置μ
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


    void PerformFullDiagnostics(uint64_t current_time_ms);
    void SimpleFixTransportFeedback(uint8_t* packet, size_t length, uint64_t recv_time_ms);
    void CheckAndFixPacingRate();
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