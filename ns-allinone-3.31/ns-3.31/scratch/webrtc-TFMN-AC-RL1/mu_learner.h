#ifndef MU_LEARNER_H
#define MU_LEARNER_H

#include "common_types.h"
#include <string>

namespace oscc {

// ============================================================================
// 学习器接口（纯虚类）
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

} // namespace oscc

#endif // MU_LEARNER_H
