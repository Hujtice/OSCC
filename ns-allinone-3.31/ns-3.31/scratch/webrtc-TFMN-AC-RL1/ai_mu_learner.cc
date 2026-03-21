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
    env->U = static_cast<float>(last_U_);
    env->p_delay = static_cast<float>(last_p_delay_);
    env->p_loss = static_cast<float>(last_p_loss_);
    env->p_mddl = static_cast<float>(last_p_mddl_);
    env->raw_delay_ms = static_cast<float>(last_raw_delay_ms_);
    env->raw_loss_rate = static_cast<float>(last_raw_loss_rate_);
    env->raw_miss_deadline_s = static_cast<float>(last_raw_miss_deadline_s_);
    env->gcc_bw_bps = static_cast<float>(last_gcc_bw_bps_);
    env->trace_bw_bps = static_cast<float>(last_trace_bw_bps_);
    env->frame_id = last_frame_id_;
    env->done = last_done_;

    // std::cout << "写入共享内存："
    //             << "norm_rt的值是: " << env->norm_rt
    //             << "norm_loss的值是: " << env->norm_loss
    //             << "reward的值是: " << env->reward
    //             << "U的值是: " << env->U
    //             << "p_delay的值是: " << env->p_delay
    //             << "p_loss的值是: " << env->p_loss
    //             << "p_mddl的值是: " << env->p_mddl
    //             << "raw_delay_ms的值是: " << env->raw_delay_ms
    //             << "raw_loss_rate的值是: " << env->raw_loss_rate
    //             << "raw_miss_deadline_s的值是: " << env->raw_miss_deadline_s
    //             << "gcc_bw_bps的值是: " << env->gcc_bw_bps
    //             << "trace_bw_bps的值是: " << env->trace_bw_bps
    //             << "frame_id的值是: " << env->frame_id
    //             << "done的值是: " << env->done
    //             << std::endl;
}

AiMuLearner::AiMuLearner(const MuLearnerConfig& config, uint16_t shm_id)
    : config_(config),
      shm_id_(shm_id),
      last_mu_(1.0),
      last_reward_(0.0),
      last_done_(0),
      baseline_(0.0),
      step_count_(0),
      last_U_(0.0), last_p_delay_(0.0), last_p_loss_(0.0), last_p_mddl_(0.0),
      last_raw_delay_ms_(0.0), last_raw_loss_rate_(0.0), last_raw_miss_deadline_s_(0.0),
      last_gcc_bw_bps_(0.0), last_trace_bw_bps_(0.0),
      last_frame_id_(0) {
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
    last_U_ = exp.U;
    last_p_delay_ = exp.p_delay;
    last_p_loss_ = exp.p_loss;
    last_p_mddl_ = exp.p_mddl;
    last_raw_delay_ms_ = exp.raw_delay_ms;
    last_raw_loss_rate_ = exp.raw_loss_rate;
    last_raw_miss_deadline_s_ = exp.raw_miss_deadline_s;
    last_gcc_bw_bps_ = exp.gcc_bw_bps;
    last_trace_bw_bps_ = exp.trace_bw_bps;
    last_frame_id_ = exp.frame_id;
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
