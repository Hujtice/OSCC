#ifndef MU_LEARNER_H
#define MU_LEARNER_H

#include "common_types.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace oscc {

// ============================================================================
// 学习器接口（纯虚类）- 未来可扩展为 Actor-Critic
// ============================================================================
class IMuLearner {
public:
    virtual ~IMuLearner() = default;
    
    // 推理：给定状态输出动作
    virtual MuAction Act(const MuState& state) = 0;
    
    // 接收经验样本（组级）
    virtual void Observe(const MuExperience& exp) = 0;
    
    // 触发更新（可由调度器控制频率）
    virtual void MaybeUpdate() = 0;
    
    // 获取当前 mu 值（用于日志）
    virtual double CurrentMu() const = 0;
    
    // 获取学习器状态用于日志
    virtual std::string GetStatusString() const = 0;
    
    // 获取 theta 的 L2 范数
    virtual double GetThetaNorm() const = 0;
    
    // 获取当前 baseline
    virtual double GetBaseline() const = 0;
};

// ============================================================================
// BanditMuLearner - 轻量确定性策略 + 噪声探索 + 组级梯度更新
// ============================================================================
class BanditMuLearner : public IMuLearner {
public:
    BanditMuLearner(const MuLearnerConfig& config = MuLearnerConfig());
    
    MuAction Act(const MuState& state) override;
    void Observe(const MuExperience& exp) override;
    void MaybeUpdate() override;
    double CurrentMu() const override;
    std::string GetStatusString() const override;
    double GetThetaNorm() const override;
    double GetBaseline() const override;
    
    // 获取配置（用于日志）
    const MuLearnerConfig& GetConfig() const { return config_; }
    
    // 设置探索开关
    void SetExplorationEnabled(bool enabled) {
        config_.exploration_enabled = enabled;
    }
    
    // 获取 theta 向量（用于日志）
    const std::vector<double>& GetTheta() const { return theta_; }
    
private:
    MuLearnerConfig config_;
    std::vector<double> theta_;
    double baseline_;
    double last_mu_;
    MuState last_state_;
    std::vector<double> last_features_;
    double last_sigmoid_;
    uint32_t update_count_;
    
    std::vector<MuExperience> pending_experiences_;
    std::mt19937 rng_;
};

} // namespace oscc

#endif // MU_LEARNER_H
