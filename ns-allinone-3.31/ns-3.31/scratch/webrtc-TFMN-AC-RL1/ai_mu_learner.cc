#include "ai_mu_learner.h"
#include "ns3/log.h"
#include <iostream>
#include <sstream>

namespace oscc {

NS_LOG_COMPONENT_DEFINE("AiMuLearner");

void AiMuLearner::WriteStateToEnv(ShmEnv* env, const MuState& state) {
    env->norm_rt = static_cast<float>(std::min(state.rt / config_.rt_max, 1.0));
    env->norm_loss = static_cast<float>(std::min(state.loss / config_.loss_max, 1.0));
    env->reward = static_cast<float>(last_reward_);
    env->done = last_done_;
}

AiMuLearner::AiMuLearner(const MuLearnerConfig& config, uint16_t shm_id)
    : config_(config),
      shm_id_(shm_id),
      last_mu_(1.0),
      last_reward_(0.0),
      last_done_(0),
      baseline_(0.0),
      step_count_(0) {
    rl_ = std::make_unique<Ns3AIRL<ShmEnv, ShmAction, ns3::RLEmptyInfo>>(shm_id_);
    rl_->SetCond(2, 0);  // C++ waits for even version (Python Release increments version to even)
    std::cout << "=== AiMuLearner Initialized (ns3-ai) ===" << std::endl;
    std::cout << "  mu_range: [" << config_.mu_min << ", " << config_.mu_max << "]" << std::endl;
    std::cout << "  shm_id: " << shm_id_ << " (must match Python Ns3AIRL id)" << std::endl;
    std::cout << "  Waiting for Python agent connection..." << std::endl;
    std::cout << "=========================================" << std::endl;
}

AiMuLearner::~AiMuLearner() {
    // Intentionally empty: SetFinish() is called via Simulator::ScheduleDestroy (Ns3AIRL ctor)
    // and/or the explicit NotifySimulationEnd() in simulation.cc before _exit(0).
    // Calling it here would be redundant, and _exit() skips destructors anyway.
}

void AiMuLearner::NotifySimulationEnd() {
    if (rl_) {
        rl_->SetFinish();
    }
}

MuAction AiMuLearner::Act(const MuState& state) {
    if (!rl_) {
        NS_LOG_ERROR("Ns3AIRL not set");
        return MuAction(1.0);
    }
    if (rl_->GetIsFinish()) {
        NS_LOG_WARN("Python side finished, using default mu=1.0");
        return MuAction(1.0);
    }

    // 1. Write observation (and previous reward/done) to shared memory
    //    EnvSetterCond() waits for version % 2 == 0 (our turn)
    ShmEnv* env = rl_->EnvSetterCond();
    WriteStateToEnv(env, state);
    rl_->SetCompleted();  // version: even -> odd (Python's turn)

    // 2. Wait for Python to read env and write action
    //    ActionGetterCond() waits for version % 2 == 0 (Python has Released, version is even again)
    ShmAction* act = rl_->ActionGetterCond();
    if (!act) {
        NS_LOG_WARN("ActionGetter returned null (simulation finished?), using mu=1.0");
        return MuAction(1.0);
    }

    double mu = std::max(config_.mu_min, std::min(config_.mu_max, static_cast<double>(act->mu)));
    rl_->GetCompleted();

    last_mu_ = mu;
    step_count_++;
    NS_LOG_INFO("Step " << step_count_ << ": State(Rt=" << state.rt << ", loss=" << state.loss
                        << ") -> Action(mu=" << mu << ")");
    return MuAction(mu);
}

void AiMuLearner::Observe(const MuExperience& exp) {
    baseline_ = config_.baseline_decay * baseline_ + (1.0 - config_.baseline_decay) * exp.reward;
    last_reward_ = exp.reward;
    last_done_ = 0;  // continuous task, never done until simulation stops
    NS_LOG_DEBUG("Reward recorded: " << exp.reward << " (baseline: " << baseline_ << ")");
}

void AiMuLearner::MaybeUpdate() {
    NS_LOG_DEBUG("MaybeUpdate (updates handled by Python agent)");
}

double AiMuLearner::CurrentMu() const {
    return last_mu_;
}

std::string AiMuLearner::GetStatusString() const {
    std::ostringstream oss;
    oss << "AiMuLearner: steps=" << step_count_ << ", last_mu=" << last_mu_
        << ", baseline=" << baseline_;
    return oss.str();
}

double AiMuLearner::GetThetaNorm() const {
    return 0.0;
}

double AiMuLearner::GetBaseline() const {
    return baseline_;
}

}  // namespace oscc
