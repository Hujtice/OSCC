#ifndef AI_MU_LEARNER_H
#define AI_MU_LEARNER_H

#include "common_types.h"
#include "mu_learner.h"
#include <memory>
#include <string>

// AiMuLearner: bridge between ns-3 and Python RL agent via ns3-ai (shared memory).
// Requires ns3-ai module. Python side uses py_interface.Ns3AIRL with same ShmEnv/ShmAction.
// Shared memory ID must match Python (default 1234).

#include "ns3/ns3-ai-module.h"

namespace oscc {

using namespace ns3;

// ============================================================================
// AiMuLearner - ns3-ai shared memory adapter implementing IMuLearner
// ============================================================================
class AiMuLearner : public IMuLearner {
public:
    static constexpr uint16_t kDefaultShmId = 1234;

    AiMuLearner(const MuLearnerConfig& config = MuLearnerConfig(), uint16_t shm_id = kDefaultShmId);
    virtual ~AiMuLearner();

    // IMuLearner interface
    MuAction Act(const MuState& state) override;
    void Observe(const MuExperience& exp) override;
    void MaybeUpdate() override;
    double CurrentMu() const override;
    std::string GetStatusString() const override;
    double GetThetaNorm() const override;
    double GetBaseline() const override;

    const MuLearnerConfig& GetConfig() const { return config_; }
    uint16_t GetShmId() const { return shm_id_; }

    void SetMaskTable(std::shared_ptr<MuMaskTable> table) { mask_table_ = table; }
    bool HasMaskTable() const { return mask_table_ && !mask_table_->Empty(); }

    /** Call before process exit (e.g. before _exit(0)) so Python can release shared memory. */
    void NotifySimulationEnd();

private:
    MuLearnerConfig config_;
    uint16_t shm_id_;
    std::unique_ptr<Ns3AIRL<ShmEnv, ShmAction, ns3::RLEmptyInfo>> rl_;
    double last_mu_;
    double last_reward_;
    uint8_t last_done_;
    double baseline_;
    uint32_t step_count_;
    double last_U_;
    double last_p_delay_;
    double last_p_loss_;
    double last_p_mddl_;
    double last_raw_delay_ms_;
    double last_raw_loss_rate_;
    double last_raw_miss_deadline_s_;
    double last_gcc_bw_bps_;
    double last_trace_bw_bps_;
    uint32_t last_frame_id_;

    std::shared_ptr<MuMaskTable> mask_table_;

    void WriteStateToEnv(ShmEnv* env, const MuState& state);
};

} // namespace oscc

#endif // AI_MU_LEARNER_H
