#ifndef GYM_MU_LEARNER_H
#define GYM_MU_LEARNER_H

#include "common_types.h"
#include "mu_learner.h"

// NOTE: This file requires ns3-gym to be installed
// Installation instructions:
// 1. cd ns-3.31/contrib
// 2. git clone https://github.com/tkn-tub/ns3-gym.git opengym
// 3. cd ../..
// 4. ./waf configure --enable-examples --enable-tests
// 5. ./waf build
//
// Python dependencies:
// pip install gym==0.21.0 protobuf==3.20.1 zmq

#ifdef NS3_OPENGYM

#include "ns3/opengym-module.h"

namespace oscc {

using namespace ns3;

// ============================================================================
// GymMuLearner - Bridge between ns-3 simulation and Python RL agent
// ============================================================================
class GymMuLearner : public IMuLearner {
public:
    GymMuLearner(const MuLearnerConfig& config = MuLearnerConfig());
    virtual ~GymMuLearner();
    
    // IMuLearner interface implementation
    MuAction Act(const MuState& state) override;
    void Observe(const MuExperience& exp) override;
    void MaybeUpdate() override;
    double CurrentMu() const override;
    std::string GetStatusString() const override;
    double GetThetaNorm() const override;
    double GetBaseline() const override;
    
    // Gym-specific methods
    void SetOpenGymInterface(Ptr<OpenGymInterface> interface);
    Ptr<OpenGymInterface> GetOpenGymInterface() const { return m_interface; }
    
    // Configuration
    const MuLearnerConfig& GetConfig() const { return config_; }
    
private:
    MuLearnerConfig config_;
    Ptr<OpenGymInterface> m_interface;
    
    double last_mu_;
    double baseline_;
    uint32_t step_count_;
    
    // Convert state to Gym observation (Box space)
    Ptr<OpenGymBoxContainer<float>> StateToObservation(const MuState& state);
    
    // Convert Gym action to MuAction
    MuAction ActionFromGym(Ptr<OpenGymDataContainer> action);
};

} // namespace oscc

#else
// Fallback dummy implementation when ns3-gym is not available
namespace oscc {

class GymMuLearner : public IMuLearner {
public:
    GymMuLearner(const MuLearnerConfig& config = MuLearnerConfig()) {
        std::cerr << "ERROR: GymMuLearner requires ns3-gym module!" << std::endl;
        std::cerr << "Please install ns3-gym following instructions in gym_mu_learner.h" << std::endl;
        exit(1);
    }
    
    MuAction Act(const MuState& state) override { return MuAction(1.0); }
    void Observe(const MuExperience& exp) override {}
    void MaybeUpdate() override {}
    double CurrentMu() const override { return 1.0; }
    std::string GetStatusString() const override { return "ns3-gym not available"; }
    double GetThetaNorm() const override { return 0.0; }
    double GetBaseline() const override { return 0.0; }
};

} // namespace oscc
#endif // NS3_OPENGYM

#endif // GYM_MU_LEARNER_H
