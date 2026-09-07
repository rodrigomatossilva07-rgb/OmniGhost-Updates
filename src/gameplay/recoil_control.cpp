#include "recoil_control.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <random>

namespace Gameplay::RecoilControl {

    // RecoilController implementation
    RecoilController::RecoilController() {
        state_ = CompensationState{};
        current_weapon_hash_ = "";
        current_weapon_class_ = WeaponClass::Unknown;
        pattern_start_time_ = 0.0f;
    }

    void RecoilController::SetCurrentWeapon(const std::string& weapon_hash, WeaponClass weapon_class) {
        if (current_weapon_hash_ != weapon_hash) {
            Reset();
            current_weapon_hash_ = weapon_hash;
            current_weapon_class_ = weapon_class;
            pattern_start_time_ = 0.0f;
        }
    }

    void RecoilController::OnShotFired(float shot_time, const ImVec2& view_angles) {
        if (current_weapon_hash_.empty()) return;
        
        auto it = config_.weapon_configs.find(current_weapon_hash_);
        if (it == config_.weapon_configs.end()) return;
        const WeaponRecoilConfig& wconfig = it->second;
        if (!wconfig.enabled) return;
        
        if (state_.shots_fired == 0) {
            pattern_start_time_ = shot_time;
        }
        
        state_.shots_fired++;
        state_.last_shot_time = shot_time;
        state_.is_compensating = true;
        
        // Record for pattern learning
        if (config_.learn_patterns) {
            recent_shots_.push_back(view_angles);
            if (recent_shots_.size() > 100) recent_shots_.erase(recent_shots_.begin());
        }
    }

    ImVec2 RecoilController::GetCompensation(float dt, const ImVec2& current_view_angles) {
        if (current_weapon_hash_.empty() || !state_.is_compensating) {
            return ImVec2(0, 0);
        }
        
        auto it = config_.weapon_configs.find(current_weapon_hash_);
        if (it == config_.weapon_configs.end()) return ImVec2(0, 0);
        const WeaponRecoilConfig& wconfig = it->second;
        if (!wconfig.enabled) return ImVec2(0, 0);
        
        // Check if we should stop compensating
        float time_since_shot = dt; // Would need actual time tracking
        if (time_since_shot > wconfig.max_compensation_time) {
            state_.is_compensating = false;
            state_.shots_fired = 0;
            return ImVec2(0, 0);
        }
        
        ImVec2 compensation;
        
        switch (config_.mode) {
            case RecoilControlConfig::Mode::Simple:
                compensation = CalculateSimpleCompensation(dt, current_view_angles);
                break;
            case RecoilControlConfig::Mode::Pattern:
                compensation = CalculatePatternCompensation(dt, current_view_angles);
                break;
            case RecoilControlConfig::Mode::Hybrid:
            case RecoilControlConfig::Mode::Adaptive: {
                ImVec2 pattern = CalculatePatternCompensation(dt, current_view_angles);
                ImVec2 simple = CalculateSimpleCompensation(dt, current_view_angles);
                compensation = BlendCompensation(pattern, simple, wconfig.pattern_blend);
                break;
            }
            default:
                compensation = ImVec2(0, 0);
        }
        
        // Apply global multipliers
        compensation.x *= config_.global_horizontal;
        compensation.y *= config_.global_vertical;
        
        // Apply per-weapon modifiers
        float stance_mod = GetStanceModifier();
        float move_mod = GetMovementModifier();
        float attach_mod = GetAttachmentModifier();
        
        compensation.x *= stance_mod * move_mod * attach_mod;
        compensation.y *= stance_mod * move_mod * attach_mod;
        
        // Apply per-weapon strength
        compensation.x *= wconfig.horizontal_strength;
        compensation.y *= wconfig.vertical_strength;
        
        // Humanization
        if (wconfig.humanize) {
            compensation = ApplyHumanization(compensation, dt);
        }
        
        // Clamp to max angle
        float max_angle = config_.max_compensation_angle * 3.14159f / 180.0f;
        float comp_len = sqrtf(compensation.x * compensation.x + compensation.y * compensation.y);
        if (comp_len > max_angle) {
            float scale = max_angle / comp_len;
            compensation.x *= scale;
            compensation.y *= scale;
        }
        
        state_.accumulated_x += compensation.x;
        state_.accumulated_y += compensation.y;
        
        return compensation;
    }

    void RecoilController::Reset() {
        state_ = CompensationState{};
        recent_shots_.clear();
        pattern_start_time_ = 0.0f;
    }

    void RecoilController::ResetWeapon(const std::string& weapon_hash) {
        if (current_weapon_hash_ == weapon_hash) {
            Reset();
        }
    }

    void RecoilController::RecordShot(const ImVec2& view_angles, float time) {
        if (!config_.learn_patterns || current_weapon_hash_.empty()) return;
        
        if (recent_shots_.empty()) {
            pattern_start_time_ = time;
        }
        recent_shots_.push_back(view_angles);
    }

    void RecoilController::FinalizePattern() {
        if (!config_.learn_patterns || current_weapon_hash_.empty() || recent_shots_.size() < config_.min_shots_to_learn) {
            return;
        }
        
        RecoilPattern pattern;
        pattern.weapon_hash = current_weapon_hash_;
        pattern.weapon_class = current_weapon_class_;
        
        // Analyze shot pattern to create recoil pattern
        if (recent_shots_.size() >= 2) {
            ImVec2 first = recent_shots_[0];
            for (size_t i = 1; i < recent_shots_.size(); ++i) {
                RecoilPoint pt;
                pt.x = recent_shots_[i].x - first.x;
                pt.y = recent_shots_[i].y - first.y;
                pt.time = (i / 10.0f); // Approximate
                pt.weight = 1.0f - (i / (float)recent_shots_.size()) * 0.5f;
                pattern.pattern.push_back(pt);
            }
        }
        
        pattern.shots_in_pattern = (int)recent_shots_.size();
        pattern.vertical_recoil = recent_shots_.back().y - recent_shots_[0].y;
        pattern.horizontal_spread = 0;
        for (const auto& shot : recent_shots_) {
            pattern.horizontal_spread = std::max(pattern.horizontal_spread, fabsf(shot.x - recent_shots_[0].x));
        }
        
        // Generate compensation (inverse)
        for (const auto& pt : pattern.pattern) {
            RecoilPoint comp_pt = pt;
            comp_pt.x = -pt.x;
            comp_pt.y = -pt.y;
            pattern.compensation.push_back(comp_pt);
        }
        
        learned_patterns_[current_weapon_hash_] = pattern;
        recent_shots_.clear();
    }

    bool RecoilController::HasLearnedPattern(const std::string& weapon_hash) const {
        return learned_patterns_.find(weapon_hash) != learned_patterns_.end();
    }

    const RecoilPattern* RecoilController::GetLearnedPattern(const std::string& weapon_hash) const {
        auto it = learned_patterns_.find(weapon_hash);
        if (it != learned_patterns_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::string RecoilController::ExportPatterns() const {
        std::ostringstream out;
        out << learned_patterns_.size() << '\n';
        for (const auto& [hash, pattern] : learned_patterns_) {
            out << hash << '|' << pattern.weapon_name << '|' 
                << static_cast<int>(pattern.weapon_class) << '|'
                << pattern.pattern.size() << '|'
                << pattern.vertical_recoil << '|'
                << pattern.horizontal_spread << '|'
                << pattern.compensation_strength << '\n';
            for (const auto& pt : pattern.pattern) {
                out << pt.x << ',' << pt.y << ',' << pt.time << ',' << pt.weight << '\n';
            }
        }
        return out.str();
    }

    bool RecoilController::ImportPatterns(const std::string& data) {
        std::istringstream in(data);
        std::string line;
        
        if (!std::getline(in, line)) return false;
        size_t count = std::stoul(line);
        
        for (size_t i = 0; i < count; ++i) {
            RecoilPattern pattern;
            if (!std::getline(in, line)) return false;
            
            std::istringstream header(line);
            std::string token;
            std::getline(header, pattern.weapon_hash, '|');
            std::getline(header, pattern.weapon_name, '|');
            std::getline(header, token, '|'); pattern.weapon_class = static_cast<WeaponClass>(std::stoi(token));
            std::getline(header, token, '|'); size_t pt_count = std::stoul(token);
            std::getline(header, token, '|'); pattern.vertical_recoil = std::stof(token);
            std::getline(header, token, '|'); pattern.horizontal_spread = std::stof(token);
            std::getline(header, token, '|'); pattern.compensation_strength = std::stof(token);
            
            pattern.pattern.reserve(pt_count);
            for (size_t j = 0; j < pt_count; ++j) {
                if (!std::getline(in, line)) return false;
                RecoilPoint pt;
                std::istringstream pt_stream(line);
                std::getline(pt_stream, token, ','); pt.x = std::stof(token);
                std::getline(pt_stream, token, ','); pt.y = std::stof(token);
                std::getline(pt_stream, token, ','); pt.time = std::stof(token);
                std::getline(pt_stream, token, ','); pt.weight = std::stof(token);
                pattern.pattern.push_back(pt);
            }
            
            // Generate compensation
            for (const auto& pt : pattern.pattern) {
                RecoilPoint comp = pt;
                comp.x = -pt.x;
                comp.y = -pt.y;
                pattern.compensation.push_back(comp);
            }
            
            learned_patterns_[pattern.weapon_hash] = pattern;
        }
        return true;
    }

    const WeaponRecoilConfig* RecoilController::GetCurrentWeaponConfig() const {
        auto it = config_.weapon_configs.find(current_weapon_hash_);
        if (it != config_.weapon_configs.end()) {
            return &it->second;
        }
        return nullptr;
    }

    ImVec2 RecoilController::CalculatePatternCompensation(float dt, const ImVec2& view_angles) {
        auto pattern_it = learned_patterns_.find(current_weapon_hash_);
        if (pattern_it == learned_patterns_.end() || pattern_it->second.compensation.empty()) {
            return ImVec2(0, 0);
        }
        
        const RecoilPattern& pattern = pattern_it->second;
        const WeaponRecoilConfig& wconfig = *GetCurrentWeaponConfig();
        
        float pattern_time = dt; // Would need actual pattern time tracking
        if (state_.pattern_index >= (int)pattern.compensation.size()) {
            state_.pattern_index = pattern.compensation.size() - 1;
        }
        
        const RecoilPoint& comp = pattern.compensation[state_.pattern_index];
        float strength = wconfig.pattern_blend * pattern.compensation_strength;
        
        return ImVec2(comp.x * strength * wconfig.horizontal_strength,
                     comp.y * strength * wconfig.vertical_strength);
    }

    ImVec2 RecoilController::CalculateSimpleCompensation(float dt, const ImVec2& view_angles) {
        const WeaponRecoilConfig* wconfig = GetCurrentWeaponConfig();
        if (!wconfig) return ImVec2(0, 0);
        
        // Simple pull-down based on shots fired
        float shots = (float)state_.shots_fired;
        float vertical = shots * 0.5f * wconfig->vertical_strength;
        float horizontal = 0.0f;
        
        // Add some horizontal correction based on pattern
        if (shots > 3) {
            horizontal = sinf(shots * 0.5f) * shots * 0.2f * wconfig->horizontal_strength;
        }
        
        return ImVec2(horizontal, vertical);
    }

    ImVec2 RecoilController::BlendCompensation(const ImVec2& pattern, const ImVec2& simple, float blend) {
        float pattern_weight = std::clamp(blend, 0.0f, 1.0f);
        float simple_weight = 1.0f - pattern_weight;
        
        return ImVec2(
            pattern.x * pattern_weight + simple.x * simple_weight,
            pattern.y * pattern_weight + simple.y * simple_weight
        );
    }

    float RecoilController::GetStanceModifier() const {
        // Would check actual player stance
        return 1.0f;
    }

    float RecoilController::GetMovementModifier() const {
        // Would check actual player movement
        return 1.0f;
    }

    float RecoilController::GetAttachmentModifier() const {
        // Would check weapon attachments
        return 1.0f;
    }

    ImVec2 RecoilController::ApplyHumanization(const ImVec2& compensation, float dt) {
        const WeaponRecoilConfig* wconfig = GetCurrentWeaponConfig();
        if (!wconfig || !wconfig->humanize) return compensation;
        
        ImVec2 result = compensation;
        
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        
        // Micro jitter
        if (wconfig->micro_jitter > 0) {
            result.x += dist(rng) * wconfig->micro_jitter;
            result.y += dist(rng) * wconfig->micro_jitter;
        }
        
        // Overcorrection
        if (wconfig->overcorrection_chance > 0) {
            static std::uniform_real_distribution<float> chance_dist(0, 1);
            if (chance_dist(rng) < wconfig->overcorrection_chance) {
                result.x *= 1.0f + wconfig->overcorrection_amount;
                result.y *= 1.0f + wconfig->overcorrection_amount;
            }
        }
        
        // Reaction variance
        if (wconfig->reaction_variance > 0) {
            float variance = dist(rng) * wconfig->reaction_variance;
            result.x *= 1.0f + variance;
            result.y *= 1.0f + variance;
        }
        
        return result;
    }

    // RecoilPatternDatabase implementation
    RecoilPatternDatabase& RecoilPatternDatabase::Instance() {
        static RecoilPatternDatabase instance;
        return instance;
    }

    void RecoilPatternDatabase::AddPattern(const RecoilPattern& pattern) {
        patterns_[pattern.weapon_hash] = pattern;
    }

    bool RecoilPatternDatabase::RemovePattern(const std::string& weapon_hash) {
        return patterns_.erase(weapon_hash) > 0;
    }

    const RecoilPattern* RecoilPatternDatabase::GetPattern(const std::string& weapon_hash) const {
        auto it = patterns_.find(weapon_hash);
        if (it != patterns_.end()) return &it->second;
        return nullptr;
    }

    const RecoilPattern* RecoilPatternDatabase::GetPatternByName(const std::string& weapon_name) const {
        for (const auto& [hash, pattern] : patterns_) {
            if (pattern.weapon_name == weapon_name) return &pattern;
        }
        return nullptr;
    }

    std::vector<const RecoilPattern*> RecoilPatternDatabase::FindPatterns(WeaponClass weapon_class) const {
        std::vector<const RecoilPattern*> result;
        for (const auto& [hash, pattern] : patterns_) {
            if (pattern.weapon_class == weapon_class) {
                result.push_back(&pattern);
            }
        }
        return result;
    }

    std::vector<const RecoilPattern*> RecoilPatternDatabase::SearchPatterns(const std::string& query) const {
        std::vector<const RecoilPattern*> result;
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
        
        for (const auto& [hash, pattern] : patterns_) {
            std::string lower_name = pattern.weapon_name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_name.find(lower_query) != std::string::npos) {
                result.push_back(&pattern);
            }
        }
        return result;
    }

    bool RecoilPatternDatabase::LoadFromFile(const char* path) {
        std::ifstream file(path);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return LoadFromJSON(buffer.str());
    }

    bool RecoilPatternDatabase::SaveToFile(const char* path) const {
        std::ofstream file(path);
        if (!file) return false;
        file << ExportToJSON();
        return true;
    }

    std::string RecoilPatternDatabase::ExportToJSON() const {
        std::ostringstream out;
        out << "{\n  \"patterns\": [\n";
        bool first = true;
        for (const auto& [hash, pattern] : patterns_) {
            if (!first) out << ",\n";
            first = false;
            out << "    {\n";
            out << "      \"hash\": \"" << pattern.weapon_hash << "\",\n";
            out << "      \"name\": \"" << pattern.weapon_name << "\",\n";
            out << "      \"class\": " << static_cast<int>(pattern.weapon_class) << ",\n";
            out << "      \"vertical\": " << pattern.vertical_recoil << ",\n";
            out << "      \"horizontal\": " << pattern.horizontal_spread << ",\n";
            out << "      \"duration\": " << pattern.pattern_duration << ",\n";
            out << "      \"shots\": " << pattern.shots_in_pattern << ",\n";
            out << "      \"compensation_strength\": " << pattern.compensation_strength << ",\n";
            out << "      \"pattern\": [\n";
            for (size_t i = 0; i < pattern.pattern.size(); ++i) {
                const auto& pt = pattern.pattern[i];
                out << "        {\"x\": " << pt.x << ", \"y\": " << pt.y << ", \"t\": " << pt.time << ", \"w\": " << pt.weight << "}";
                if (i < pattern.pattern.size() - 1) out << ",";
                out << "\n";
            }
            out << "      ]\n";
            out << "    }";
        }
        out << "\n  ]\n}";
        return out.str();
    }

    bool RecoilPatternDatabase::LoadFromJSON(const std::string& json) {
        // Simple JSON parsing - would use proper JSON library in production
        return false;
    }

    bool RecoilPatternDatabase::DownloadCommunityPatterns(const char* url) {
        // Would implement HTTP download
        return false;
    }

    bool RecoilPatternDatabase::UploadPattern(const RecoilPattern& pattern) {
        // Would implement HTTP upload
        return false;
    }

    // RecoilCrosshair implementation
    RecoilCrosshair::RecoilCrosshair(const Config& config) : config_(config) {}

    void RecoilCrosshair::Update(const ImVec2& recoil_offset, const ImVec2& compensation,
                                const RecoilPattern* pattern, int pattern_index) {
        current_recoil_ = recoil_offset;
        current_compensation_ = compensation;
        current_pattern_ = pattern;
        current_pattern_index_ = pattern_index;
    }

    void RecoilCrosshair::Draw(ImDrawList* draw_list, const ImVec2& center) const {
        if (!draw_list || !config_.enabled) return;
        
        ImU32 color = config_.base_color;
        float size = config_.crosshair_size;
        float gap = config_.gap;
        float thick = config_.thickness;
        
        // Dynamic gap based on recoil
        if (config_.dynamic_gap) {
            float recoil_mag = sqrtf(current_recoil_.x * current_recoil_.x + current_recoil_.y * current_recoil_.y);
            gap = std::min(config_.gap + recoil_mag * 2.0f, config_.max_gap);
        }
        
        // Base crosshair
        draw_list->AddLine(ImVec2(center.x - size - gap, center.y), ImVec2(center.x - gap, center.y), color, thick);
        draw_list->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + size + gap, center.y), color, thick);
        draw_list->AddLine(ImVec2(center.x, center.y - size - gap), ImVec2(center.x, center.y - gap), color, thick);
        draw_list->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + size + gap), color, thick);
        
        // Recoil offset indicator
        if (config_.show_recoil_offset && (current_recoil_.x != 0 || current_recoil_.y != 0)) {
            ImVec2 recoil_pos(center.x + current_recoil_.x * 10.0f, center.y + current_recoil_.y * 10.0f);
            draw_list->AddCircle(recoil_pos, 4.0f, config_.recoil_color, 12, 2.0f);
        }
        
        // Compensation indicator
        if (config_.show_compensation && (current_compensation_.x != 0 || current_compensation_.y != 0)) {
            ImVec2 comp_pos(center.x + current_compensation_.x * 10.0f, center.y + current_compensation_.y * 10.0f);
            draw_list->AddCircle(comp_pos, 3.0f, config_.compensation_color, 8, 1.5f);
        }
    }

    // Helper functions
    WeaponClass GetWeaponClassFromName(const std::string& weapon_name) {
        std::string lower = weapon_name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        if (lower.find("pistol") != std::string::npos || lower.find("revolver") != std::string::npos ||
            lower.find("deagle") != std::string::npos || lower.find("glock") != std::string::npos ||
            lower.find("usp") != std::string::npos || lower.find("cz75") != std::string::npos ||
            lower.find("tec9") != std::string::npos || lower.find("fiveseven") != std::string::npos)
            return WeaponClass::Pistol;
        
        if (lower.find("smg") != std::string::npos || lower.find("mp5") != std::string::npos ||
            lower.find("mp7") != std::string::npos || lower.find("mp9") != std::string::npos ||
            lower.find("p90") != std::string::npos || lower.find("bizon") != std::string::npos ||
            lower.find("ump") != std::string::npos || lower.find("mac10") != std::string::npos)
            return WeaponClass::SMG;
        
        if (lower.find("rifle") != std::string::npos || lower.find("ak47") != std::string::npos ||
            lower.find("ak74") != std::string::npos || lower.find("m4") != std::string::npos ||
            lower.find("ar15") != std::string::npos || lower.find("galil") != std::string::npos ||
            lower.find("famas") != std::string::npos || lower.find("sg553") != std::string::npos ||
            lower.find("aug") != std::string::npos)
            return WeaponClass::Rifle;
        
        if (lower.find("sniper") != std::string::npos || lower.find("awp") != std::string::npos ||
            lower.find("ssg08") != std::string::npos || lower.find("scar20") != std::string::npos ||
            lower.find("g3sg1") != std::string::npos)
            return WeaponClass::Sniper;
        
        if (lower.find("shotgun") != std::string::npos || lower.find("nova") != std::string::npos ||
            lower.find("xm1014") != std::string::npos || lower.find("mag7") != std::string::npos ||
            lower.find("sawedoff") != std::string::npos)
            return WeaponClass::Shotgun;
        
        if (lower.find("lmg") != std::string::npos || lower.find("m249") != std::string::npos ||
            lower.find("negev") != std::string::npos)
            return WeaponClass::LMG;
        
        return WeaponClass::Unknown;
    }

    const char* GetWeaponClassName(WeaponClass cls) {
        switch (cls) {
            case WeaponClass::Pistol: return "Pistol";
            case WeaponClass::SMG: return "SMG";
            case WeaponClass::Rifle: return "Rifle";
            case WeaponClass::Sniper: return "Sniper";
            case WeaponClass::Shotgun: return "Shotgun";
            case WeaponClass::LMG: return "LMG";
            case WeaponClass::Marksman: return "Marksman";
            case WeaponClass::Heavy: return "Heavy";
            case WeaponClass::Special: return "Special";
            case WeaponClass::Melee: return "Melee";
            case WeaponClass::Grenade: return "Grenade";
            default: return "Unknown";
        }
    }

    // Preset configurations
    WeaponRecoilConfig GetDefaultPistolConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = true;
        cfg.vertical_strength = 0.8f;
        cfg.horizontal_strength = 0.4f;
        cfg.start_delay = 0.05f;
        cfg.max_compensation_time = 1.0f;
        cfg.use_pattern = true;
        cfg.pattern_blend = 0.7f;
        cfg.humanize = true;
        cfg.micro_jitter = 0.15f;
        return cfg;
    }

    WeaponRecoilConfig GetDefaultSMGConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = true;
        cfg.vertical_strength = 1.0f;
        cfg.horizontal_strength = 0.6f;
        cfg.start_delay = 0.03f;
        cfg.max_compensation_time = 1.5f;
        cfg.use_pattern = true;
        cfg.pattern_blend = 0.8f;
        cfg.humanize = true;
        cfg.micro_jitter = 0.12f;
        return cfg;
    }

    WeaponRecoilConfig GetDefaultRifleConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = true;
        cfg.vertical_strength = 1.0f;
        cfg.horizontal_strength = 0.5f;
        cfg.start_delay = 0.02f;
        cfg.max_compensation_time = 2.0f;
        cfg.use_pattern = true;
        cfg.pattern_blend = 0.85f;
        cfg.humanize = true;
        cfg.micro_jitter = 0.1f;
        return cfg;
    }

    WeaponRecoilConfig GetDefaultSniperConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = false; // Snipers usually don't need recoil control
        cfg.vertical_strength = 0.5f;
        cfg.horizontal_strength = 0.3f;
        cfg.start_delay = 0.1f;
        cfg.max_compensation_time = 1.0f;
        cfg.use_pattern = false;
        cfg.humanize = false;
        return cfg;
    }

    WeaponRecoilConfig GetDefaultShotgunConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = true;
        cfg.vertical_strength = 0.7f;
        cfg.horizontal_strength = 0.3f;
        cfg.start_delay = 0.1f;
        cfg.max_compensation_time = 1.0f;
        cfg.use_pattern = false;
        cfg.humanize = true;
        cfg.micro_jitter = 0.2f;
        return cfg;
    }

    WeaponRecoilConfig GetDefaultLMGConfig() {
        WeaponRecoilConfig cfg;
        cfg.enabled = true;
        cfg.vertical_strength = 1.2f;
        cfg.horizontal_strength = 0.7f;
        cfg.start_delay = 0.01f;
        cfg.max_compensation_time = 3.0f;
        cfg.use_pattern = true;
        cfg.pattern_blend = 0.9f;
        cfg.humanize = true;
        cfg.micro_jitter = 0.08f;
        return cfg;
    }

    // Serialization
    std::string SerializeWeaponConfig(const WeaponRecoilConfig& config) {
        std::ostringstream out;
        out << config.weapon_name << '|' << config.weapon_hash << '|'
            << (config.enabled ? 1 : 0) << '|'
            << config.vertical_strength << '|' << config.horizontal_strength << '|'
            << config.start_delay << '|' << config.compensation_delay << '|'
            << config.max_compensation_time << '|'
            << (config.use_pattern ? 1 : 0) << '|' << config.pattern_blend << '|'
            << (config.adapt_to_attachments ? 1 : 0) << '|'
            << config.suppressor_mod << '|' << config.compensator_mod << '|'
            << config.muzzle_brake_mod << '|' << config.flash_hider_mod << '|'
            << config.vertical_grip_mod << '|' << config.angled_grip_mod << '|'
            << config.standing_mod << '|' << config.crouching_mod << '|' << config.prone_mod << '|'
            << config.stationary_mod << '|' << config.walking_mod << '|' << config.running_mod << '|'
            << (config.humanize ? 1 : 0) << '|'
            << config.micro_jitter << '|' << config.overcorrection_chance << '|'
            << config.overcorrection_amount << '|' << config.reaction_variance << '|'
            << (config.show_pattern_preview ? 1 : 0) << '|' << (config.show_compensation_preview ? 1 : 0);
        return out.str();
    }

    bool DeserializeWeaponConfig(const std::string& data, WeaponRecoilConfig& config) {
        std::istringstream in(data);
        std::string token;
        
        std::getline(in, config.weapon_name, '|');
        std::getline(in, config.weapon_hash, '|');
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        
        read_bool(config.enabled);
        read_float(config.vertical_strength);
        read_float(config.horizontal_strength);
        read_float(config.start_delay);
        read_float(config.compensation_delay);
        read_float(config.max_compensation_time);
        read_bool(config.use_pattern);
        read_float(config.pattern_blend);
        read_bool(config.adapt_to_attachments);
        read_float(config.suppressor_mod);
        read_float(config.compensator_mod);
        read_float(config.muzzle_brake_mod);
        read_float(config.flash_hider_mod);
        read_float(config.vertical_grip_mod);
        read_float(config.angled_grip_mod);
        read_float(config.standing_mod);
        read_float(config.crouching_mod);
        read_float(config.prone_mod);
        read_float(config.stationary_mod);
        read_float(config.walking_mod);
        read_float(config.running_mod);
        read_bool(config.humanize);
        read_float(config.micro_jitter);
        read_float(config.overcorrection_chance);
        read_float(config.overcorrection_amount);
        read_float(config.reaction_variance);
        read_bool(config.show_pattern_preview);
        read_bool(config.show_compensation_preview);
        
        return true;
    }

    std::string SerializeRecoilConfig(const RecoilControlConfig& config) {
        std::ostringstream out;
        out << (config.enabled ? 1 : 0) << '|'
            << (config.only_when_aiming ? 1 : 0) << '|'
            << (config.require_attack_held ? 1 : 0) << '|'
            << config.global_vertical << '|' << config.global_horizontal << '|'
            << static_cast<int>(config.mode) << '|'
            << config.learn_patterns << '|'
            << config.min_shots_to_learn << '|'
            << config.pattern_confidence_threshold << '|'
            << config.max_patterns_per_weapon << '|'
            << (config.require_valid_target ? 1 : 0) << '|'
            << config.max_compensation_angle << '|'
            << (config.stop_on_target_switch ? 1 : 0) << '|'
            << (config.show_crosshair_offset ? 1 : 0) << '|'
            << config.pattern_color << '|' << config.compensation_color << '|'
            << config.weapon_configs.size() << '|';
        
        for (const auto& [hash, wcfg] : config.weapon_configs) {
            out << hash << '|' << SerializeWeaponConfig(wcfg) << '|';
        }
        return out.str();
    }

    bool DeserializeRecoilConfig(const std::string& data, RecoilControlConfig& config) {
        std::istringstream in(data);
        std::string token;
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        auto read_int = [&](int& i) {
            std::getline(in, token, '|');
            i = std::stoi(token);
        };
        
        read_bool(config.enabled);
        read_bool(config.only_when_aiming);
        read_bool(config.require_attack_held);
        read_float(config.global_vertical);
        read_float(config.global_horizontal);
        read_int(*reinterpret_cast<int*>(&config.mode));
        read_bool(config.learn_patterns);
        read_int(config.min_shots_to_learn);
        read_float(config.pattern_confidence_threshold);
        read_int(config.max_patterns_per_weapon);
        read_bool(config.require_valid_target);
        read_float(config.max_compensation_angle);
        read_bool(config.stop_on_target_switch);
        read_bool(config.show_crosshair_offset);
        
        std::getline(in, token, '|'); config.pattern_color = std::stoul(token);
        std::getline(in, token, '|'); config.compensation_color = std::stoul(token);
        
        read_int(*reinterpret_cast<int*>(&config.weapon_configs)); // size
        
        for (int i = 0; i < (int)config.weapon_configs.size(); ++i) {
            std::getline(in, token, '|'); // hash
            std::string hash = token;
            std::string wcfg_data;
            std::getline(in, wcfg_data, '|');
            WeaponRecoilConfig wcfg;
            wcfg.weapon_hash = hash;
            DeserializeWeaponConfig(wcfg_data, wcfg);
            config.weapon_configs[hash] = wcfg;
        }
        return true;
    }

} // namespace Gameplay::RecoilControl