#include "webrtc_trace.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <fstream>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("WebrtcTraceExt");

FrameAwareWebrtcTrace::FrameAwareWebrtcTrace(QoEIntegrationManager* qoe_manager, 
                                             RLStateManager* rl_manager, 
                                             double bandwidth_scale_factor) 
    : qoe_manager_(qoe_manager), rl_manager_(rl_manager), 
      total_bw_changes(0), bandwidth_scale_factor_(bandwidth_scale_factor),
      m_changer(nullptr),
      current_mu(1.0), current_loss_rate(0.01) {
    NS_LOG_INFO("FrameAwareWebrtcTrace created with bandwidth scale factor: " << bandwidth_scale_factor_);
}

FrameAwareWebrtcTrace::~FrameAwareWebrtcTrace() {}

void FrameAwareWebrtcTrace::SetCurrentParameters(double mu, double loss_rate) {
    current_mu = mu;
    current_loss_rate = loss_rate;
    NS_LOG_INFO("FrameAwareWebrtcTrace parameters updated: μ=" << mu << ", L=" << loss_rate);
}

void FrameAwareWebrtcTrace::SetBwTraceFuc(TraceBandwidth cb) {
    m_traceBw = cb;
    NS_LOG_INFO("Bandwidth trace callback set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::SetScaledBwTraceFuc(TraceScaledBandwidth cb) {
    m_traceScaledBw = cb;
    NS_LOG_INFO("Scaled bandwidth trace callback set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::SetTraceBandwidthCallback(GetTraceBandwidthCallback cb) {
    m_getTraceBw = cb;
    NS_LOG_INFO("Trace bandwidth callback set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::SetBandwidthChanger(BandwidthChanger* changer) {
    m_changer = changer;
    NS_LOG_INFO("BandwidthChanger set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::OnReceiptPktInfo(uint32_t now, uint32_t seq, uint32_t owd) {
    NS_LOG_DEBUG("FrameAwareWebrtcTrace::OnReceiptPktInfo called - time: " << now 
                 << "ms, seq: " << seq << ", owd: " << owd << "ms");
    
    WebrtcTrace::OnReceiptPktInfo(now, seq, owd);
    
    double trace_rtt_ms = 30.0;
    double trace_loss_rate = 0.01;
    
    if (m_changer) {
        trace_rtt_ms = m_changer->GetRTTAtTime(now);
        trace_loss_rate = m_changer->GetLossAtTime(now);
    }
    
    if (rl_manager_) {
        double current_delay_ms = static_cast<double>(owd);
        rl_manager_->UpdateNetworkState(current_delay_ms, trace_loss_rate, MilliSeconds(trace_rtt_ms));
        NS_LOG_DEBUG("Updated current delay in RL manager: " << current_delay_ms << "ms, RTT=" 
                   << trace_rtt_ms << "ms, loss=" << trace_loss_rate);
    }

    // 将 seq 喂入 QoE 管理器的丢包追踪器（P1：实际观测丢包）
    if (qoe_manager_) {
        qoe_manager_->ReportPacketSeq(seq);
    }
}

void FrameAwareWebrtcTrace::Log(const std::string& name, uint32_t flags) {
    std::string filename = name;
    if (filename.find("_gcc_1") != std::string::npos) {
        size_t pos = filename.find("_mu=");
        if (pos != std::string::npos) {
            filename = filename.substr(0, pos);
        }
        pos = filename.find("_L=");
        if (pos != std::string::npos) {
            filename = filename.substr(0, pos);
        }
        
        filename += "_mu=" + std::to_string(current_mu) + 
                   "_L=" + std::to_string(current_loss_rate);
    }
    
    WebrtcTrace::Log(filename, flags);
    
    NS_LOG_INFO("FrameAwareWebrtcTrace logging to files with suffix: _mu=" 
               << current_mu << "_L=" << current_loss_rate);
}

void FrameAwareWebrtcTrace::OnBW(uint32_t now, uint32_t bps) {
    std::cout << "\n[GCC-DEBUG] OnBW called at time: " << now << "ms, bandwidth: " << bps << " bps" << std::endl;
    
    WebrtcTrace::OnBW(now, bps);
    
    total_bw_changes++;
    original_bw_history.push_back(std::make_pair(now, bps));
    
    std::cout << "[GCC-Original] Time: " << now << "ms, BW: " << bps << " bps (" 
            << (bps / 1000000.0) << " Mbps)" << std::endl;

    double current_mu_val = 1.0;
    if (rl_manager_) {
        current_mu_val = rl_manager_->GetCurrentMu();
        std::cout << "[DEBUG] Got current mu from RL manager: " << current_mu_val << std::endl;
    } else {
        std::cout << "[WARNING] RL manager is null, using default mu=1.0" << std::endl;
    }
    
    uint32_t scaled_bw = static_cast<uint32_t>(bps * current_mu_val);
    
    scaled_bw_history.push_back(std::make_tuple(now, bps, scaled_bw, current_mu_val));
    
    TraceData trace_data;
    if (m_changer) {
        trace_data = m_changer->GetTraceDataAtTime(now);
        std::cout << "[Real-Trace-Data] Time: " << now << "ms:" << std::endl;
        std::cout << "  Real Trace BW: " << trace_data.bandwidth << " bps (" 
                << (trace_data.bandwidth / 1000000.0) << " Mbps)" << std::endl;
        std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
        std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
    } else {
        trace_data.bandwidth = bps;
        trace_data.rtt = 30.0;
        trace_data.loss = 0.01;
        std::cout << "[WARNING] No BandwidthChanger available, using default trace data" << std::endl;
    }
    
    if (qoe_manager_) {
        Time timestamp = MilliSeconds(now);
        
        const auto& history = qoe_manager_->GetBandwidthHistory();
        bool should_add = true;
        if (!history.empty()) {
            const auto& last_record = history.back();
            if (Abs(timestamp - last_record.timestamp) < MilliSeconds(1)) {
                should_add = false;
                std::cout << "[FrameManager-Update] Similar timestamp exists, skipping duplicate record" << std::endl;
            }
        }
        
        if (should_add) {
            qoe_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, bps, scaled_bw, current_mu_val);
            
            std::cout << "[FrameManager-Bandwidth] Time: " << now << "ms:" << std::endl;
            std::cout << "  Trace BW: " << trace_data.bandwidth << " bps" << std::endl;
            std::cout << "  GCC BW: " << bps << " bps" << std::endl;
            std::cout << "  Scaled BW: " << scaled_bw << " bps (μ=" << current_mu_val << ")" << std::endl;
            std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
            std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
            std::cout << "  Bandwidth history size: " << history.size() + 1 << std::endl;
        }
    }
    
    if (!m_traceBw.IsNull()) {
        m_traceBw(now, bps);
    }
    
    if (!m_traceScaledBw.IsNull()) {
        m_traceScaledBw(now, bps, scaled_bw, current_mu_val);
    }
}

void FrameAwareWebrtcTrace::OnScaledBandwidth(uint32_t now, uint32_t original_bps, uint32_t scaled_bps, double scale_factor) {
    NS_LOG_INFO("Scaled Bandwidth - Time: " << now << "ms, Original: " << original_bps 
               << " bps, Scaled: " << scaled_bps << " bps, μ: " << scale_factor);
    
    scaled_bw_history.push_back(std::make_tuple(now, original_bps, scaled_bps, scale_factor));
    
    std::cout << "[GCC-Scaled] Time: " << now << "ms, Original: " << original_bps 
              << " bps -> Scaled: " << scaled_bps << " bps (μ=" << scale_factor << ")" << std::endl;
    
    bool found = false;
    for (auto& entry : original_bw_history) {
        if (entry.first == now) {
            entry.second = original_bps;
            found = true;
            break;
        }
    }
    if (!found) {
        original_bw_history.push_back(std::make_pair(now, original_bps));
    }
    
    if (qoe_manager_) {
        Time timestamp = MilliSeconds(now);
        
        TraceData trace_data;
        if (m_changer) {
            trace_data = m_changer->GetTraceDataAtTime(now);
        } else {
            trace_data.bandwidth = original_bps;
            trace_data.rtt = 30.0;
            trace_data.loss = 0.01;
        }
        
        qoe_manager_->AddBandwidthRecord(timestamp, trace_data.bandwidth, original_bps, scaled_bps, scale_factor);
        
        std::cout << "[FrameManager-Update] Updated with scaled bandwidth:" << std::endl;
        std::cout << "  Time: " << now << "ms" << std::endl;
        std::cout << "  Trace BW: " << trace_data.bandwidth << " bps" << std::endl;
        std::cout << "  GCC BW: " << original_bps << " bps" << std::endl;
        std::cout << "  Scaled BW: " << scaled_bps << " bps (μ=" << scale_factor << ")" << std::endl;
        std::cout << "  RTT from trace: " << trace_data.rtt << "ms" << std::endl;
        std::cout << "  Loss from trace: " << trace_data.loss << std::endl;
    }
}

void FrameAwareWebrtcTrace::OutputBandwidthStatistics(const std::string& filename, double loss_rate) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        NS_LOG_ERROR("Cannot open bandwidth statistics file: " << filename);
        return;
    }
    
    file << "# Bandwidth scale factor (μ): " << bandwidth_scale_factor_ << std::endl;
    file << "# Loss rate (L): " << loss_rate << std::endl;
    file << "timestamp_ms,trace_bandwidth_bps,trace_bandwidth_mbps,trace_rtt_ms,trace_loss,original_bandwidth_bps,original_bandwidth_mbps,"
        << "scaled_bandwidth_bps,scaled_bandwidth_mbps,scale_factor" << std::endl;
    
    std::set<uint32_t> processed_timestamps;
    
    for (const auto& scaled_entry : scaled_bw_history) {
        uint32_t timestamp = std::get<0>(scaled_entry);
        uint32_t original_bw = std::get<1>(scaled_entry);
        uint32_t scaled_bw = std::get<2>(scaled_entry);
        double scale_factor = std::get<3>(scaled_entry);
        
        TraceData trace_data;
        if (m_changer) {
            trace_data = m_changer->GetTraceDataAtTime(timestamp);
        } else if (!m_getTraceBw.IsNull()) {
            trace_data.bandwidth = m_getTraceBw(timestamp);
            trace_data.rtt = 30.0;
            trace_data.loss = 0.01;
        } else {
            trace_data.bandwidth = original_bw;
            trace_data.rtt = 30.0;
            trace_data.loss = 0.01;
            NS_LOG_WARN("No trace data callback available, using default values");
        }
        
        file << timestamp << "," 
             << trace_data.bandwidth << "," << (trace_data.bandwidth / 1000000.0) << ","
             << trace_data.rtt << "," << trace_data.loss << ","
             << original_bw << "," << (original_bw / 1000000.0) << ","
             << scaled_bw << "," << (scaled_bw / 1000000.0) << "," 
             << scale_factor << std::endl;
        
        processed_timestamps.insert(timestamp);
    }
    
    for (const auto& original_entry : original_bw_history) {
        uint32_t timestamp = original_entry.first;
        uint32_t original_bw = original_entry.second;
        
        if (processed_timestamps.find(timestamp) == processed_timestamps.end()) {
            uint32_t scaled_bw = original_bw;
            double scale_factor = 1.0;
            
            TraceData trace_data;
            if (m_changer) {
                trace_data = m_changer->GetTraceDataAtTime(timestamp);
            } else if (!m_getTraceBw.IsNull()) {
                trace_data.bandwidth = m_getTraceBw(timestamp);
                trace_data.rtt = 30.0;
                trace_data.loss = 0.01;
            } else {
                trace_data.bandwidth = original_bw;
                trace_data.rtt = 30.0;
                trace_data.loss = 0.01;
            }
            
            file << timestamp << ","
                 << trace_data.bandwidth << "," << (trace_data.bandwidth / 1000000.0) << ","
                 << trace_data.rtt << "," << trace_data.loss << ","
                 << original_bw << "," << (original_bw / 1000000.0) << ","
                 << scaled_bw << "," << (scaled_bw / 1000000.0) << "," 
                 << scale_factor << std::endl;
        }
    }
    
    file.close();
    
    NS_LOG_INFO("Bandwidth statistics saved to: " << filename 
            << " with μ=" << bandwidth_scale_factor_ << ", L=" << loss_rate);
    std::cout << "Bandwidth statistics saved to: " << filename 
              << " (10 columns: timestamp_ms, trace_bandwidth_bps, trace_bandwidth_mbps, trace_rtt_ms, trace_loss, "
              << "original_bandwidth_bps, original_bandwidth_mbps, "
              << "scaled_bandwidth_bps, scaled_bandwidth_mbps, scale_factor)" << std::endl;
    
    if (!scaled_bw_history.empty()) {
        auto sample = scaled_bw_history[0];
        std::cout << "Sample data from bandwidth statistics:" << std::endl;
        std::cout << "  Timestamp: " << std::get<0>(sample) << "ms" << std::endl;
        std::cout << "  Original BW: " << std::get<1>(sample) << " bps" << std::endl;
        std::cout << "  Scaled BW: " << std::get<2>(sample) << " bps" << std::endl;
        std::cout << "  Scale factor: " << std::get<3>(sample) << std::endl;
    }
}

void FrameAwareWebrtcTrace::GetBandwidthStats(uint32_t& total_changes, uint32_t& avg_original_bw, uint32_t& avg_scaled_bw) const {
    total_changes = this->total_bw_changes;
    
    if (!original_bw_history.empty()) {
        uint64_t sum_original = 0;
        for (const auto& entry : original_bw_history) {
            sum_original += entry.second;
        }
        avg_original_bw = sum_original / original_bw_history.size();
    } else {
        avg_original_bw = 0;
    }
    
    if (!scaled_bw_history.empty()) {
        uint64_t sum_scaled = 0;
        for (const auto& entry : scaled_bw_history) {
            sum_scaled += std::get<2>(entry);
        }
        avg_scaled_bw = sum_scaled / scaled_bw_history.size();
    } else {
        avg_scaled_bw = 0;
    }
}

void FrameAwareWebrtcTrace::SetQoEManager(QoEIntegrationManager* qoe_manager) {
    qoe_manager_ = qoe_manager;
    NS_LOG_INFO("QoEIntegrationManager set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::SetRLStateManager(RLStateManager* rl_manager) {
    rl_manager_ = rl_manager;
    NS_LOG_INFO("RLStateManager set in FrameAwareWebrtcTrace");
}

void FrameAwareWebrtcTrace::SetBandwidthScaleFactor(double factor) {
    bandwidth_scale_factor_ = factor;
    NS_LOG_INFO("Bandwidth scale factor updated to: " << bandwidth_scale_factor_);
}

double FrameAwareWebrtcTrace::GetMuAtTimestamp(double timestamp_s) const {
    // Use current mu from RL manager if available
    if (rl_manager_) {
        return rl_manager_->GetCurrentMu();
    }
    return bandwidth_scale_factor_;
}

} // namespace oscc
