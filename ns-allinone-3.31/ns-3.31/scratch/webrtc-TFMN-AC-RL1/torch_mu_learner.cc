#include "torch_mu_learner.h"
#include "ns3/log.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace oscc {

#ifdef OSCC_USE_TORCH

NS_LOG_COMPONENT_DEFINE("TorchMuLearner");

TorchMLPMuLearner::TorchMLPMuLearner(const MuLearnerConfig& config)
    : config_(config), baseline_(0.0), last_mu_(1.0), update_count_(0), last_loss_(0.0) {
    
    // 初始化网络
    model_ = MLPNet(2, 16, 8);
    
    // 初始化网络权重，使其初始输出接近 config 中的 initial_mu
    InitializeNetwork(config_.initial_theta.empty() ? 1.0 : config_.initial_theta[0]);
    
    // 设置为训练模式
    model_->train();
    
    // 创建 Adam 优化器
    optimizer_ = std::make_shared<torch::optim::Adam>(
        model_->parameters(),
        torch::optim::AdamOptions(config_.learning_rate)
    );
    
    // 初始化随机数生成器
    std::random_device rd;
    rng_.seed(rd());
    
    std::cout << "=== TorchMLPMuLearner Initialized ===" << std::endl;
    std::cout << "  Network: 2 -> 16(ReLU) -> 8(ReLU) -> 1 -> sigmoid" << std::endl;
    std::cout << "  mu_range: [" << config_.mu_min << ", " << config_.mu_max << "]" << std::endl;
    std::cout << "  learning_rate: " << config_.learning_rate << std::endl;
    std::cout << "  exploration_sigma: " << config_.exploration_sigma << std::endl;
    std::cout << "  baseline_decay: " << config_.baseline_decay << std::endl;
    std::cout << "  grad_clip: " << config_.grad_clip << std::endl;
    std::cout << "=====================================" << std::endl;
}

void TorchMLPMuLearner::InitializeNetwork(double initial_mu) {
    // 计算目标 sigmoid 输出值
    // mu = mu_min + (mu_max - mu_min) * sigmoid
    // sigmoid = (mu - mu_min) / (mu_max - mu_min)
    double target_sigmoid = (initial_mu - config_.mu_min) / (config_.mu_max - config_.mu_min);
    target_sigmoid = std::max(0.01, std::min(0.99, target_sigmoid)); // 避免极值
    
    // 计算对应的 logit: logit = log(sigmoid / (1 - sigmoid))
    double target_logit = std::log(target_sigmoid / (1.0 - target_sigmoid));
    
    // 设置输出层的 bias 为 target_logit，权重初始化为小值
    torch::NoGradGuard no_grad;
    
    // 初始化隐藏层权重
    torch::nn::init::xavier_uniform_(model_->fc1->weight);
    torch::nn::init::zeros_(model_->fc1->bias);
    
    torch::nn::init::xavier_uniform_(model_->fc2->weight);
    torch::nn::init::zeros_(model_->fc2->bias);
    
    // 输出层：权重接近0，bias设为 target_logit
    torch::nn::init::uniform_(model_->fc3->weight, -0.01, 0.01);
    model_->fc3->bias.fill_(target_logit);
    
    std::cout << "[TorchMLPMuLearner] Network initialized for initial_mu=" << initial_mu 
              << " (target_sigmoid=" << target_sigmoid << ", target_logit=" << target_logit << ")" << std::endl;
}

torch::Tensor TorchMLPMuLearner::NormalizeInput(const MuState& state) {
    double norm_rt = std::min(state.rt / config_.rt_max, 1.0);
    double norm_loss = std::min(state.loss / config_.loss_max, 1.0);
    
    auto input = torch::tensor({{norm_rt, norm_loss}}, torch::kFloat32);
    return input;
}

double TorchMLPMuLearner::MapToMuRange(double sigmoid_out) {
    return config_.mu_min + (config_.mu_max - config_.mu_min) * sigmoid_out;
}

MuAction TorchMLPMuLearner::Act(const MuState& state) {
    torch::NoGradGuard no_grad;
    
    // 前向传播
    auto input = NormalizeInput(state);
    auto logit = model_->forward(input);
    auto sigmoid_out = torch::sigmoid(logit).item<double>();
    
    // 映射到 mu 范围
    double mu_base = MapToMuRange(sigmoid_out);
    
    // 添加探索噪声
    double mu_final = mu_base;
    if (config_.exploration_enabled && config_.exploration_sigma > 0) {
        std::normal_distribution<double> noise(0.0, config_.exploration_sigma);
        mu_final = mu_base + noise(rng_);
    }
    
    // Clip 到有效范围
    mu_final = std::max(config_.mu_min, std::min(config_.mu_max, mu_final));
    
    last_mu_ = mu_final;
    
    NS_LOG_DEBUG("TorchMLPMuLearner::Act: rt=" << state.rt << ", loss=" << state.loss 
                << " -> sigmoid=" << sigmoid_out << ", mu_base=" << mu_base 
                << ", mu_final=" << mu_final);
    
    return MuAction(mu_final, 0.0);
}

void TorchMLPMuLearner::Observe(const MuExperience& exp) {
    pending_experiences_.push_back(exp);
}

void TorchMLPMuLearner::MaybeUpdate() {
    if (pending_experiences_.empty()) {
        return;
    }
    
    // 批量处理所有待处理的经验
    for (const auto& exp : pending_experiences_) {
        // 更新 baseline（EMA）
        baseline_ = config_.baseline_decay * baseline_ + (1.0 - config_.baseline_decay) * exp.reward;
        
        // 计算 advantage
        double advantage = exp.reward - baseline_;
        
        // 前向传播
        auto input = NormalizeInput(exp.state);
        auto logit = model_->forward(input);
        auto sigmoid_out = torch::sigmoid(logit);
        auto mu_pred_tensor = config_.mu_min + (config_.mu_max - config_.mu_min) * sigmoid_out;
        
        // 计算损失: loss = -advantage * mu_pred
        // 因为我们要最大化 advantage * mu_pred，即最小化 -advantage * mu_pred
        auto loss = -advantage * mu_pred_tensor;
        
        // 反向传播
        optimizer_->zero_grad();
        loss.backward();
        
        // 梯度裁剪
        if (config_.grad_clip > 0) {
            torch::nn::utils::clip_grad_norm_(model_->parameters(), config_.grad_clip);
        }
        
        // 更新参数
        optimizer_->step();
        
        update_count_++;
        last_loss_ = loss.item<double>();
        
        NS_LOG_DEBUG("TorchMLPMuLearner update: reward=" << exp.reward 
                    << ", baseline=" << baseline_ << ", advantage=" << advantage
                    << ", loss=" << last_loss_ << ", updates=" << update_count_);
        
        if (update_count_ % 10 == 0) {
            std::cout << "[TorchMLPMuLearner] Update #" << update_count_ 
                      << ": reward=" << std::fixed << std::setprecision(3) << exp.reward
                      << ", baseline=" << baseline_ << ", advantage=" << advantage
                      << ", loss=" << last_loss_ << std::endl;
        }
    }
    
    pending_experiences_.clear();
}

double TorchMLPMuLearner::CurrentMu() const {
    return last_mu_;
}

std::string TorchMLPMuLearner::GetStatusString() const {
    std::ostringstream oss;
    oss << "TorchMLP[updates=" << update_count_ 
        << ", baseline=" << std::fixed << std::setprecision(4) << baseline_
        << ", last_loss=" << last_loss_
        << ", theta_norm=" << GetThetaNorm() << "]";
    return oss.str();
}

double TorchMLPMuLearner::GetThetaNorm() const {
    torch::NoGradGuard no_grad;
    double norm_sq = 0.0;
    for (const auto& param : model_->parameters()) {
        norm_sq += torch::sum(param * param).item<double>();
    }
    return std::sqrt(norm_sq);
}

double TorchMLPMuLearner::GetBaseline() const {
    return baseline_;
}

#endif // OSCC_USE_TORCH

} // namespace oscc
