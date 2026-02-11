#include "gym_mu_learner.h"
#include "ns3/log.h"
#include <iostream>

#ifdef NS3_OPENGYM

namespace oscc {

NS_LOG_COMPONENT_DEFINE("GymMuLearner");

GymMuLearner::GymMuLearner(const MuLearnerConfig& config)
    : config_(config), last_mu_(1.0), baseline_(0.0), step_count_(0) {
    
    std::cout << "=== GymMuLearner Initialized ===" << std::endl;
    std::cout << "  mu_range: [" << config_.mu_min << ", " << config_.mu_max << "]" << std::endl;
    std::cout << "  Waiting for Python agent connection via ns3-gym..." << std::endl;
    std::cout << "====================================" << std::endl;
}

GymMuLearner::~GymMuLearner() {
    if (m_interface) {
        // Notify Python agent that simulation is ending
        m_interface->NotifySimulationEnd();
    }
}

void GymMuLearner::SetOpenGymInterface(Ptr<OpenGymInterface> interface) {
    m_interface = interface;
    NS_LOG_INFO("OpenGymInterface set for GymMuLearner");
}

Ptr<OpenGymBoxContainer<float>> GymMuLearner::StateToObservation(const MuState& state) {
    // Observation space: Box([Rt, loss_rate], dtype=float32)
    Ptr<OpenGymBoxContainer<float>> obs = CreateObject<OpenGymBoxContainer<float>>();
    
    // Normalize inputs
    float norm_rt = static_cast<float>(std::min(state.rt / config_.rt_max, 1.0));
    float norm_loss = static_cast<float>(std::min(state.loss / config_.loss_max, 1.0));
    
    obs->AddValue(norm_rt);
    obs->AddValue(norm_loss);
    
    NS_LOG_DEBUG("State to observation: Rt=" << state.rt << " (norm: " << norm_rt 
                 << "), Loss=" << state.loss << " (norm: " << norm_loss << ")");
    
    return obs;
}

MuAction GymMuLearner::ActionFromGym(Ptr<OpenGymDataContainer> action) {
    if (!action) {
        NS_LOG_WARN("Received null action from Gym, using default mu=1.0");
        return MuAction(1.0);
    }
    
    // Action space: Box([mu], low=0.5, high=1.5, dtype=float32)
    Ptr<OpenGymBoxContainer<float>> box = DynamicCast<OpenGymBoxContainer<float>>(action);
    
    if (!box || box->GetSize() == 0) {
        NS_LOG_WARN("Invalid action format, using default mu=1.0");
        return MuAction(1.0);
    }
    
    float mu_raw = box->GetValue(0);
    
    // Clip to valid range
    double mu = std::max(config_.mu_min, std::min(config_.mu_max, static_cast<double>(mu_raw)));
    
    NS_LOG_DEBUG("Action from Gym: raw=" << mu_raw << ", clipped=" << mu);
    
    return MuAction(mu);
}

MuAction GymMuLearner::Act(const MuState& state) {
    if (!m_interface) {
        NS_LOG_ERROR("OpenGymInterface not set! Cannot communicate with Python agent.");
        return MuAction(1.0);  // Fallback to default
    }
    
    // Convert state to observation
    Ptr<OpenGymBoxContainer<float>> obs = StateToObservation(state);
    
    // Send observation to Python and get action
    Ptr<OpenGymDataContainer> action = m_interface->GetActionSpaceData();
    
    if (!action) {
        NS_LOG_WARN("Failed to get action from Gym agent, using default");
        return MuAction(1.0);
    }
    
    MuAction mu_action = ActionFromGym(action);
    last_mu_ = mu_action.mu;
    step_count_++;
    
    NS_LOG_INFO("Step " << step_count_ << ": State(Rt=" << state.rt 
                << ", loss=" << state.loss << ") -> Action(mu=" << mu_action.mu << ")");
    
    return mu_action;
}

void GymMuLearner::Observe(const MuExperience& exp) {
    if (!m_interface) {
        NS_LOG_ERROR("OpenGymInterface not set! Cannot send reward to Python agent.");
        return;
    }
    
    // Update baseline (exponential moving average)
    baseline_ = config_.baseline_decay * baseline_ + (1.0 - config_.baseline_decay) * exp.reward;
    
    // Send reward to Python agent
    m_interface->NotifyReward(exp.reward);
    
    // Episode never ends in this continuous task (unless simulation stops)
    m_interface->NotifyGameOver(false);
    
    NS_LOG_DEBUG("Reward sent to Gym: " << exp.reward << " (baseline: " << baseline_ << ")");
}

void GymMuLearner::MaybeUpdate() {
    // Updates are handled by Python agent
    // This method is kept for interface compatibility
    NS_LOG_DEBUG("MaybeUpdate called (updates handled by Python agent)");
}

double GymMuLearner::CurrentMu() const {
    return last_mu_;
}

std::string GymMuLearner::GetStatusString() const {
    std::ostringstream oss;
    oss << "GymMuLearner: steps=" << step_count_ 
        << ", last_mu=" << last_mu_ 
        << ", baseline=" << baseline_;
    return oss.str();
}

double GymMuLearner::GetThetaNorm() const {
    // Not applicable for Gym learner (parameters are in Python)
    return 0.0;
}

double GymMuLearner::GetBaseline() const {
    return baseline_;
}

} // namespace oscc

#endif // NS3_OPENGYM
