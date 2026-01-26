#include "network_components.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("NetworkComponents");

// ============================================================================
// TriggerRandomLoss 实现
// ============================================================================

TriggerRandomLoss::TriggerRandomLoss(double loss_rate) 
    : m_loss_rate(loss_rate), m_last_applied_loss(-1.0),
      m_bandwidth_changer(nullptr), m_use_trace_loss(false), 
      m_update_interval_ms(100) {
    NS_LOG_INFO("TriggerRandomLoss initialized with loss rate: " << m_loss_rate);
}

TriggerRandomLoss::~TriggerRandomLoss() {
    if (m_timer.IsRunning()) {
        m_timer.Cancel();
    }
    if (m_update_timer.IsRunning()) {
        m_update_timer.Cancel();
    }
}

void TriggerRandomLoss::RegisterDevice(Ptr<NetDevice> dev) {
    m_dev = dev;
    NS_LOG_INFO("Device registered for random loss with rate: " << m_loss_rate);
}

void TriggerRandomLoss::SetBandwidthChanger(BandwidthChanger* changer) {
    m_bandwidth_changer = changer;
    m_use_trace_loss = (changer != nullptr);
    if (m_use_trace_loss) {
        std::cout << "[TriggerRandomLoss] BandwidthChanger set - will use DYNAMIC loss from trace!" << std::endl;
    }
}

void TriggerRandomLoss::SetUseTraceLoss(bool use_trace) {
    m_use_trace_loss = use_trace && (m_bandwidth_changer != nullptr);
}

void TriggerRandomLoss::SetUpdateInterval(uint32_t interval_ms) {
    m_update_interval_ms = interval_ms;
}

void TriggerRandomLoss::Start() {
    Time next = MilliSeconds(10);
    m_timer = Simulator::Schedule(next, &TriggerRandomLoss::ConfigureRandomLoss, this);
    NS_LOG_INFO("Scheduled random loss configuration at " << next.GetSeconds() << "s with rate: " << m_loss_rate);
}

void TriggerRandomLoss::ConfigureRandomLoss() {
    if (m_dev) {
        m_error_model = CreateObject<RateErrorModel>();
        m_error_model->SetAttribute("ErrorRate", DoubleValue(m_loss_rate));
        m_error_model->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(m_dev);
        if (p2pDev) {
            p2pDev->SetReceiveErrorModel(m_error_model);
            m_last_applied_loss = m_loss_rate;
            
            std::cout << "================================================" << std::endl;
            std::cout << "[TriggerRandomLoss] ReceiveErrorModel CONFIGURED!" << std::endl;
            std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
            std::cout << "  Initial Loss Rate: " << m_loss_rate << " (" << (m_loss_rate * 100) << "%)" << std::endl;
            std::cout << "  Device Type: PointToPointNetDevice" << std::endl;
            std::cout << "  Dynamic Update: " << (m_use_trace_loss ? "ENABLED (from trace)" : "DISABLED") << std::endl;
            std::cout << "  Update Interval: " << m_update_interval_ms << "ms" << std::endl;
            std::cout << "================================================" << std::endl;
            
            NS_LOG_INFO("Random loss configured at " << Simulator::Now().GetSeconds() 
                       << "s with rate: " << m_loss_rate);
            
            if (m_use_trace_loss && m_bandwidth_changer) {
                ScheduleNextUpdate();
            }
        } else {
            m_dev->SetAttribute("ReceiveErrorModel", PointerValue(m_error_model));
            m_last_applied_loss = m_loss_rate;
            
            std::cout << "[TriggerRandomLoss] ReceiveErrorModel CONFIGURED (fallback)!" << std::endl;
            std::cout << "  Loss Rate: " << m_loss_rate << " (" << (m_loss_rate * 100) << "%)" << std::endl;
            
            if (m_use_trace_loss && m_bandwidth_changer) {
                ScheduleNextUpdate();
            }
        }
    } else {
        NS_LOG_ERROR("No device registered for random loss configuration");
        std::cout << "[ERROR] TriggerRandomLoss: No device registered!" << std::endl;
    }
    
    m_timer.Cancel();
}

void TriggerRandomLoss::UpdateLossFromTrace() {
    if (!m_dev || !m_bandwidth_changer) {
        ScheduleNextUpdate();
        return;
    }
    
    uint32_t now_ms = static_cast<uint32_t>(Simulator::Now().GetMilliSeconds());
    double trace_loss = m_bandwidth_changer->GetLossAtTime(now_ms);
    
    if (std::abs(trace_loss - m_last_applied_loss) > 0.001) {
        Ptr<RateErrorModel> new_error_model = CreateObject<RateErrorModel>();
        new_error_model->SetAttribute("ErrorRate", DoubleValue(trace_loss));
        new_error_model->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(m_dev);
        if (p2pDev) {
            p2pDev->SetReceiveErrorModel(new_error_model);
            m_error_model = new_error_model;
            m_last_applied_loss = trace_loss;
            m_loss_rate = trace_loss;
            
            std::cout << "[TriggerRandomLoss] Loss rate UPDATED from trace!" << std::endl;
            std::cout << "  Time: " << Simulator::Now().GetSeconds() << "s" << std::endl;
            std::cout << "  New Loss Rate: " << trace_loss << " (" << (trace_loss * 100) << "%)" << std::endl;
            
            NS_LOG_INFO("Loss rate updated from trace at " << Simulator::Now().GetSeconds() 
                       << "s to: " << trace_loss);
        }
    }
    
    ScheduleNextUpdate();
}

void TriggerRandomLoss::ScheduleNextUpdate() {
    m_update_timer = Simulator::Schedule(
        MilliSeconds(m_update_interval_ms), 
        &TriggerRandomLoss::UpdateLossFromTrace, 
        this
    );
}

void TriggerRandomLoss::SetLossRate(double loss_rate) {
    m_loss_rate = loss_rate;
    if (m_error_model) {
        m_error_model->SetAttribute("ErrorRate", DoubleValue(loss_rate));
        m_last_applied_loss = loss_rate;
    }
    NS_LOG_INFO("Loss rate updated to: " << m_loss_rate);
}

// ============================================================================
// BandwidthChanger 实现
// ============================================================================

BandwidthChanger::BandwidthChanger() 
    : m_initialRate(0), m_index(0), current_trace_bandwidth_bps_(0) {}

BandwidthChanger::~BandwidthChanger() {
    if (m_timer.IsRunning()) {
        m_timer.Cancel();
    }
}

void BandwidthChanger::RegisterDevice(Ptr<NetDevice> dev) {
    m_dev = dev;
}

void BandwidthChanger::Config(int64_t initial_bps, std::vector<std::pair<Time,int64_t>>& info) {
    m_initialRate = initial_bps;
    m_info.swap(info);
    current_trace_bandwidth_bps_ = initial_bps;
}

std::pair<float,float> BandwidthChanger::ConfigwithReadNetworkTrace(int64_t initial_bps, const std::string& trace_file) {
    std::pair<float,float> dur_time;

    std::cout << "[DEBUG] Reading trace file: " << trace_file << std::endl;
    m_initialRate = initial_bps;
    current_trace_bandwidth_bps_ = initial_bps;

    if (m_timer.IsRunning()) {
        m_timer.Cancel();
    }
    m_info.clear();
    m_index = 0;

    std::ifstream singlefile(trace_file);
    if (!singlefile.is_open()) {
        NS_LOG_ERROR("Cannot open bandwidth file: " << trace_file);
        dur_time.first = 0;
        dur_time.second = 0;
        return dur_time;
    }

    float start_time = -1.0;
    float end_time = 0.0;
    float time, bandwidth, rtt, loss;
    
    m_trace_data.clear();
    
    while (singlefile >> time >> bandwidth >> rtt >> loss) {
        TraceData trace_data;
        trace_data.timestamp = Seconds(time);
        trace_data.bandwidth = bandwidth * 1 * 1000000.0;
        trace_data.rtt = rtt;
        trace_data.loss = loss;
        m_trace_data.push_back(trace_data);
        
        int64_t FIX_bandwidth_bps = static_cast<int64_t>(bandwidth * 1 * 1000000.0);
        m_info.push_back(std::make_pair(Seconds(time), FIX_bandwidth_bps));
        
        if (start_time < 0 || time < start_time)
            start_time = time;
        if (time > end_time)
            end_time = time;
    }
    
    singlefile.close();
    
    std::cout << "Original trace time range: " << start_time << "s to " << end_time << "s" << std::endl;
    std::cout << "Total events: " << m_info.size() << std::endl;
    std::cout << "Trace data entries: " << m_trace_data.size() << std::endl;
    
    for (size_t i = 0; i < std::min(m_trace_data.size(), size_t(5)); i++) {
        const TraceData& data = m_trace_data[i];
        std::cout << "Trace data " << i << ": time=" << data.timestamp.GetSeconds() 
                  << "s, bw=" << data.bandwidth << " bps, rtt=" << data.rtt 
                  << "ms, loss=" << data.loss << std::endl;
    }
    
    if (!m_info.empty()) {
        dur_time.first = 0.0;
        dur_time.second = (end_time - start_time) + 10.0;
    } else {
        dur_time.first = 0;
        dur_time.second = 30.0;
    }
    
    return dur_time;
}

void BandwidthChanger::ResetBandwidthFromTrace() {
    if (m_timer.IsExpired() && m_dev && m_index < m_info.size()) {
        PointToPointNetDevice* device = static_cast<PointToPointNetDevice*>(PeekPointer(m_dev));
        device->SetDataRate(DataRate(m_info[m_index].second));
        current_trace_bandwidth_bps_ = m_info[m_index].second;
        
        NS_LOG_INFO("Bandwidth changed at " << Simulator::Now().GetSeconds() 
                << "s to " << m_info[m_index].second << " bps (" 
                << (m_info[m_index].second / 1000000.0) << " Mbps)");
        
        m_index++;
        if (m_index < m_info.size()) {
            NS_ASSERT(m_info[m_index].first > m_info[m_index-1].first);
            Time next = m_info[m_index].first - m_info[m_index-1].first;
            m_timer = Simulator::Schedule(next, &BandwidthChanger::ResetBandwidthFromTrace, this);
        } else {
            NS_LOG_INFO("All bandwidth changes completed at " << Simulator::Now().GetSeconds() << "s");
        }
    }
}

void BandwidthChanger::TotalThroughput(Time stop, int64_t& channel_bit) {
    int index = lower_bound_index(stop);
    if (0 == index) {
        channel_bit = 0;
    } else {
        int64_t bit = 0;
        for (int i = 0; i < index; i++) {
            if (0 == i) {
                bit += m_initialRate * (m_info.at(i).first.GetMilliSeconds() / 1000);
            } else {
                bit += m_info.at(i-1).second * ((m_info.at(i).first - m_info.at(i-1).first).GetMilliSeconds() / 1000);
            }
        }
        if (stop > m_info.back().first) {
            bit += m_info.back().second * ((stop - m_info.back().first).GetMilliSeconds() / 1000);
        }
        channel_bit = bit;
    }
}

void BandwidthChanger::Start() {
    if (m_info.size() > 0) {
        m_index = 0;
        Time next = m_info[m_index].first;
        m_timer = Simulator::Schedule(next, &BandwidthChanger::ResetBandwidthFromTrace, this);
    }
}

int BandwidthChanger::lower_bound_index(Time point) {
    auto ele = std::make_pair(point, 0);
    auto iter = std::lower_bound(m_info.begin(), m_info.end(), ele, CompareV());
    return iter - m_info.begin();
}

uint32_t BandwidthChanger::GetTraceBandwidthAtTime(uint32_t timestamp_ms) const {
    Time query_time = MilliSeconds(timestamp_ms);
    
    if (m_info.empty()) {
        return static_cast<uint32_t>(current_trace_bandwidth_bps_);
    }
    
    int64_t trace_bw = m_initialRate;
    
    for (const auto& change : m_info) {
        if (change.first <= query_time) {
            trace_bw = change.second;
        } else {
            break;
        }
    }
    
    std::cout << "[BandwidthChanger] GetTraceBandwidthAtTime: " << timestamp_ms 
              << "ms -> " << trace_bw << " bps (" << (trace_bw / 1000000.0) << " Mbps)" << std::endl;
    
    return static_cast<uint32_t>(trace_bw);
}

double BandwidthChanger::GetRTTAtTime(uint32_t timestamp_ms) const {
    Time query_time = MilliSeconds(timestamp_ms);
    
    if (m_trace_data.empty()) {
        return 30.0;
    }
    
    double rtt = 30.0;
    Time min_diff = Seconds(1000000);
    
    for (const auto& data : m_trace_data) {
        Time diff = Abs(data.timestamp - query_time);
        if (diff < min_diff) {
            min_diff = diff;
            rtt = data.rtt;
        }
    }
    
    if (min_diff > Seconds(0.5)) {
        return 30.0;
    }
    
    std::cout << "[BandwidthChanger] GetRTTAtTime: " << timestamp_ms 
              << "ms -> " << rtt << "ms (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
    
    return rtt;
}

double BandwidthChanger::GetLossAtTime(uint32_t timestamp_ms) const {
    Time query_time = MilliSeconds(timestamp_ms);
    
    if (m_trace_data.empty()) {
        return 0.01;
    }
    
    double loss = 0.01;
    Time min_diff = Seconds(1000000);
    
    for (const auto& data : m_trace_data) {
        Time diff = Abs(data.timestamp - query_time);
        if (diff < min_diff) {
            min_diff = diff;
            loss = data.loss;
        }
    }
    
    if (min_diff > Seconds(0.5)) {
        return 0.01;
    }
    
    std::cout << "[BandwidthChanger] GetLossAtTime: " << timestamp_ms 
              << "ms -> " << loss << " (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
    
    return loss;
}

TraceData BandwidthChanger::GetTraceDataAtTime(uint32_t timestamp_ms) const {
    Time query_time = MilliSeconds(timestamp_ms);
    
    if (m_trace_data.empty()) {
        TraceData default_data;
        default_data.timestamp = query_time;
        default_data.bandwidth = current_trace_bandwidth_bps_;
        default_data.rtt = 30.0;
        default_data.loss = 0.01;
        return default_data;
    }
    
    const TraceData* closest_data = &m_trace_data[0];
    Time min_diff = Abs(closest_data->timestamp - query_time);
    
    for (const auto& data : m_trace_data) {
        Time diff = Abs(data.timestamp - query_time);
        if (diff < min_diff) {
            min_diff = diff;
            closest_data = &data;
        }
    }
    
    if (min_diff > Seconds(0.5)) {
        TraceData default_data;
        default_data.timestamp = query_time;
        default_data.bandwidth = current_trace_bandwidth_bps_;
        default_data.rtt = 30.0;
        default_data.loss = 0.01;
        return default_data;
    }
    
    std::cout << "[BandwidthChanger] GetTraceDataAtTime: " << timestamp_ms 
              << "ms -> bw=" << closest_data->bandwidth << " bps, rtt=" << closest_data->rtt 
              << "ms, loss=" << closest_data->loss << " (time diff: " << min_diff.GetSeconds() << "s)" << std::endl;
    
    return *closest_data;
}

} // namespace oscc
