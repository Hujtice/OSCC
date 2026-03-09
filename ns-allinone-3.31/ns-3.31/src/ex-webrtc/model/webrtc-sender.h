//###############调节发送速率
#pragma once 

#include <deque>
#include <random>
#include <map>
#include "ns3/event-id.h"
#include "ns3/callback.h"
#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/webrtc-config.h"
#include "ns3/atomic-lock.h"
#include "webrtc-tag.h"
#include "test/scenario/transport_base.h"
#include "call/call.h"

namespace ns3{

// RTP包的帧信息（用于发送时附加到WebrtcTag）
struct RtpFrameInfo {
    uint32_t frame_id;
    uint32_t rtp_timestamp;
    uint8_t is_keyframe;
    uint8_t is_first_packet;
    uint8_t is_last_packet;
    
    RtpFrameInfo() : frame_id(0), rtp_timestamp(0), is_keyframe(0), 
                     is_first_packet(0), is_last_packet(0) {}
};

// 新增：带宽缩放回调类型
typedef Callback<void, uint32_t, uint32_t, uint32_t, double> TraceScaledBandwidth;

// 前向声明 RLSM 类
// class RLSM;

class WebrtcSender:public webrtc::test::TransportBase,public Application{
public:
    WebrtcSender(WebrtcSessionManager *manager);
    ~WebrtcSender() override;
    InetSocketAddress GetLocalAddress();
    void Bind(uint16_t port);
    void ConfigurePeer(Ipv4Address addr,uint16_t port);
    typedef Callback<void,uint32_t,uint32_t> TraceBandwidth;
    void SetBwTraceFuc(TraceBandwidth cb);
    void EnableAdaptiveMu(bool enable) { m_adaptive_mu = enable; }
    
    // 新增：设置带宽缩放系数和回调
    void SetBandwidthScaleFactor(double factor);
    double GetBandwidthScaleFactor() const;
    uint32_t GetScaledBandwidth(uint32_t original_bw);
    void SetScaledBwTraceFuc(TraceScaledBandwidth cb);
    
    // 新增：RL状态管理器相关方法
    // void SetRLSM(RLSM* rl_manager);
    void UpdateBandwidthScaleFactor(double new_factor);
    double GetCurrentBandwidthScaleFactor() const;
    
    // 新增：带宽缩放集成方法
    void ApplyBandwidthScalingToController();
    void VerifyBandwidthScaling();
    void ApplyRealBandwidthScaling(uint32_t target_bandwidth_bps);
    void ApplyPeriodicBandwidthScaling();
    void SetBandwidthScaleFactorDirect(double factor);
    
    // OSCC集成：运行时动态更新μ值
    void UpdateMuDynamic(double new_mu);

    // 新增：由 trace 调用以请求 sender 下发目标比特率（单位 bps）
    void SetTargetBitrate(uint32_t target_bps);
    
    // 跳帧相关方法
    void SkipToFrame(uint32_t target_frame_id);  // 跳转到指定帧
    void RequestKeyFrame();                       // 请求编码器生成关键帧
    void ClearPendingPackets();                   // 清空待发送队列

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
    void SendToNetworkWithFrameInfo(Ptr<Packet> p, const RtpFrameInfo& frame_info);
    void RecvPacket(Ptr<Socket> socket);
    
    bool ShouldSendPacket(size_t packet_size);
    void UpdateBandwidthLimiter();
    void ResetBandwidthLimiter();

    // Apply pending bitrate in node context (called via Simulator::ScheduleWithContext)
    void ApplyPendingBitrate();

    bool m_running{false};
    WebrtcSessionManager *m_manager{nullptr};
    webrtc::Clock *m_clock;
    uint16_t m_bindPort{0};
    Ptr<Socket> m_socket;
    Ipv4Address m_peerIp;
    uint16_t m_peerPort{0};
    webrtc::test::CallClient *m_client{nullptr};
    webrtc::Call* m_call{nullptr};
    uint64_t m_seq{1};
    AtomicLock m_rtpLock;
    std::deque<rtc::CopyOnWriteBuffer> m_rtpQ;
    AtomicLock m_rtcpLock;
    std::deque<rtc::CopyOnWriteBuffer> m_rtcpQ;
    int64_t m_lastTraceTime{0};
    uint32_t m_context{0};
    TraceBandwidth m_traceBw;
    TraceScaledBandwidth m_traceScaledBw;
    uint32_t m_packetOverhead{0};
    uint32_t m_initial_time{0};
    
    // 带宽缩放相关成员变量
    double m_bandwidth_scale_factor{1.0};
    uint32_t m_current_estimated_bandwidth{0};
    uint32_t m_current_scaled_bandwidth{0};
    double m_last_reported_mu{-1.0};

    // Debounce / pending apply members (trace -> sender)
    uint32_t m_last_applied_scaled_bw{0};   // 最后实际下发到 transport controller 的 scaled bw
    uint32_t m_pending_scaled_bw{0};        // 待下发的 scaled bw（由 trace 请求）
    bool m_has_pending_bw{false};           // 是否存在待下发值
    double m_apply_threshold_ratio{0.05};   // 去抖比例阈值（默认 5%）
    uint32_t m_apply_threshold_abs{1000};   // 绝对阈值 (bps)，默认 1 kbps
    
    // OSCC动态调速：基准带宽（避免累积效应）
    uint32_t m_base_gcc_bandwidth{0};       // 基准GCC带宽（不被缩放污染）
    double m_last_applied_mu{1.0};          // 上次应用的μ值

    // used in some heuristics to suppress transient zeros
    uint32_t m_zeroCount{0};
    
    // 跳帧相关成员变量
    uint32_t m_skip_target_frame_id{UINT32_MAX};  // 跳帧目标
    uint32_t m_force_keyframe_frame_id{UINT32_MAX}; // 强制作为关键帧发送的帧ID
    bool m_skip_frame_active{false};              // 是否正在执行跳帧

    static const uint32_t BANDWIDTH_CHECK_INTERVAL_MS = 100;
    static const double BANDWIDTH_TOLERANCE;

    bool m_adaptive_mu{false};
    double ComputeMuBasedOnNetwork(); // 根据网络状态计算μ
    void UpdateAdaptiveMu();          // 周期性更新μ
    
    // ==================== 帧追踪相关成员 ====================
    // RTP包帧信息队列（与m_rtpQ一一对应）
    std::deque<RtpFrameInfo> m_rtpFrameInfoQ;
    
    // RTP时间戳到帧ID的映射（用于生成递增的帧ID）
    std::map<uint32_t, uint32_t> m_rtpTimestampToFrameId;
    std::deque<uint32_t> m_rtpTimestampOrder;  // 按插入顺序记录RTP时间戳（用于LRU清理）
    uint32_t m_nextFrameId{0};  // 下一个分配的帧ID
    uint32_t m_lastRtpTimestamp{0};  // 上一个RTP时间戳
    
    // 帧信息解析辅助方法
    RtpFrameInfo ParseRtpPacketInfo(const uint8_t* packet, size_t length);
    uint32_t GetOrCreateFrameId(uint32_t rtp_timestamp);
    bool IsVP8KeyFrame(const uint8_t* payload, size_t payload_length);
    bool IsH264KeyFrame(const uint8_t* payload, size_t payload_length);
};   
}