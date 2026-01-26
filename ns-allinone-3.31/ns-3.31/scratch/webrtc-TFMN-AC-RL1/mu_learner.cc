#include "mu_learner.h"
#include "ns3/log.h"

namespace oscc {

NS_LOG_COMPONENT_DEFINE("MuLearner");

BanditMuLearner::BanditMuLearner(const MuLearnerConfig& config)
    : config_(config), baseline_(0.0), last_mu_(1.0), update_count_(0) {
    // 初始化 theta
    theta_ = config_.initial_theta;
    if (theta_.size() < 3) {
        theta_.resize(3, 0.0);
    }
    
    // 初始化随机数生成器
    std::random_device rd;
    rng_.seed(rd());
    
    std::cout << "=== BanditMuLearner Initialized ===" << std::endl;
    std::cout << "  mu_range: [" << config_.mu_min << ", " << config_.mu_max << "]" << std::endl;
    std::cout << "  learning_rate: " << config_.learning_rate << std::endl;
    std::cout << "  exploration_sigma: " << config_.exploration_sigma << std::endl;
    std::cout << "  baseline_decay: " << config_.baseline_decay << std::endl;
    std::cout << "  theta: [" << theta_[0] << ", " << theta_[1] << ", " << theta_[2] << "]" << std::endl;
    std::cout << "====================================" << std::endl;
}

MuAction BanditMuLearner::Act(const MuState& state) {
    // 特征归一化
    double norm_rt = std::min(state.rt / config_.rt_max, 1.0);
    double norm_loss = std::min(state.loss / config_.loss_max, 1.0);
    
    // 线性组合 + sigmoid 输出
    double y = theta_[0] * norm_rt + theta_[1] * norm_loss + theta_[2];
    double sigmoid_y = 1.0 / (1.0 + std::exp(-y));
    
    // 映射到 [mu_min, mu_max]
    double mu_base = config_.mu_min + (config_.mu_max - config_.mu_min) * sigmoid_y;
    
    // 添加探索噪声
    double mu_final = mu_base;
    if (config_.exploration_enabled && config_.exploration_sigma > 0) {
        std::normal_distribution<double> noise(0.0, config_.exploration_sigma);
        mu_final = mu_base + noise(rng_);
    }
    
    // Clip 到有效范围
    mu_final = std::max(config_.mu_min, std::min(config_.mu_max, mu_final));
    
    last_mu_ = mu_final;
    last_state_ = state;
    last_features_ = {norm_rt, norm_loss, 1.0};
    last_sigmoid_ = sigmoid_y;
    
    return MuAction(mu_final, 0.0);
}

void BanditMuLearner::Observe(const MuExperience& exp) {
    pending_experiences_.push_back(exp);
}

void BanditMuLearner::MaybeUpdate() {
    if (pending_experiences_.empty()) {
        return;
    }
    
    // 处理所有待处理的经验
    for (const auto& exp : pending_experiences_) {
        // 更新 baseline（EMA）
        baseline_ = config_.baseline_decay * baseline_ + (1.0 - config_.baseline_decay) * exp.reward;
        
        // 计算 advantage（reward - baseline）
        double advantage = exp.reward - baseline_;
        
        // 计算特征（重新归一化）
        double norm_rt = std::min(exp.state.rt / config_.rt_max, 1.0);
        double norm_loss = std::min(exp.state.loss / config_.loss_max, 1.0);
        std::vector<double> features = {norm_rt, norm_loss, 1.0};
        
        // 计算 sigmoid 及其导数
        double y = theta_[0] * norm_rt + theta_[1] * norm_loss + theta_[2];
        double sigmoid_y = 1.0 / (1.0 + std::exp(-y));
        double sigmoid_grad = sigmoid_y * (1.0 - sigmoid_y);
        
        // 计算 mu 对 theta 的梯度
        double scale = (config_.mu_max - config_.mu_min) * sigmoid_grad;
        
        std::vector<double> grad(3);
        for (size_t i = 0; i < 3; ++i) {
            grad[i] = scale * features[i];
        }
        
        // 梯度裁剪
        double grad_norm = std::sqrt(grad[0]*grad[0] + grad[1]*grad[1] + grad[2]*grad[2]);
        if (grad_norm > config_.grad_clip) {
            double clip_scale = config_.grad_clip / grad_norm;
            for (auto& g : grad) {
                g *= clip_scale;
            }
        }
        
        // 更新 theta
        for (size_t i = 0; i < 3; ++i) {
            theta_[i] += config_.learning_rate * advantage * grad[i];
        }
        
        update_count_++;
        
        NS_LOG_DEBUG("BanditMuLearner update: reward=" << exp.reward 
                    << ", baseline=" << baseline_ << ", advantage=" << advantage
                    << ", theta=[" << theta_[0] << "," << theta_[1] << "," << theta_[2] << "]");
    }
    
    pending_experiences_.clear();
}

double BanditMuLearner::CurrentMu() const {
    return last_mu_;
}

std::string BanditMuLearner::GetStatusString() const {
    std::ostringstream oss;
    oss << "theta=[" << std::fixed << std::setprecision(4) 
        << theta_[0] << "," << theta_[1] << "," << theta_[2] << "]"
        << ", baseline=" << baseline_
        << ", updates=" << update_count_;
    return oss.str();
}

double BanditMuLearner::GetThetaNorm() const {
    return std::sqrt(theta_[0]*theta_[0] + theta_[1]*theta_[1] + theta_[2]*theta_[2]);
}

double BanditMuLearner::GetBaseline() const {
    return baseline_;
}

} // namespace oscc
