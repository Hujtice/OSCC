#ifndef TORCH_MU_LEARNER_H
#define TORCH_MU_LEARNER_H

#include "common_types.h"
#include "mu_learner.h"

#ifdef OSCC_USE_TORCH
#include <torch/torch.h>
#endif

namespace oscc {

#ifdef OSCC_USE_TORCH

// ============================================================================
// MLP 网络定义：2 -> 16(ReLU) -> 8(ReLU) -> 1
// ============================================================================
struct MLPNetImpl : torch::nn::Module {
    torch::nn::Linear fc1{nullptr}, fc2{nullptr}, fc3{nullptr};
    
    MLPNetImpl(int64_t input_size = 2, int64_t hidden1 = 16, int64_t hidden2 = 8) {
        fc1 = register_module("fc1", torch::nn::Linear(input_size, hidden1));
        fc2 = register_module("fc2", torch::nn::Linear(hidden1, hidden2));
        fc3 = register_module("fc3", torch::nn::Linear(hidden2, 1));
    }
    
    torch::Tensor forward(torch::Tensor x) {
        x = torch::relu(fc1->forward(x));
        x = torch::relu(fc2->forward(x));
        x = fc3->forward(x);
        return x;
    }
};
TORCH_MODULE(MLPNet);

// ============================================================================
// TorchMLPMuLearner - 两层 MLP + 在线学习
// ============================================================================
class TorchMLPMuLearner : public IMuLearner {
public:
    TorchMLPMuLearner(const MuLearnerConfig& config = MuLearnerConfig());
    
    MuAction Act(const MuState& state) override;
    void Observe(const MuExperience& exp) override;
    void MaybeUpdate() override;
    double CurrentMu() const override;
    std::string GetStatusString() const override;
    double GetThetaNorm() const override;
    double GetBaseline() const override;
    
    // 额外接口
    const MuLearnerConfig& GetConfig() const { return config_; }
    void SetExplorationEnabled(bool enabled) { config_.exploration_enabled = enabled; }
    int64_t GetUpdateCount() const { return update_count_; }
    double GetLastLoss() const { return last_loss_; }
    
private:
    MuLearnerConfig config_;
    MLPNet model_;
    std::shared_ptr<torch::optim::Adam> optimizer_;
    double baseline_;
    double last_mu_;
    int64_t update_count_;
    double last_loss_;
    
    std::vector<MuExperience> pending_experiences_;
    std::mt19937 rng_;
    
    // 辅助函数
    void InitializeNetwork(double initial_mu);
    torch::Tensor NormalizeInput(const MuState& state);
    double MapToMuRange(double sigmoid_out);
};

#endif // OSCC_USE_TORCH

} // namespace oscc

#endif // TORCH_MU_LEARNER_H
