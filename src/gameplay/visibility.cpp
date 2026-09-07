#include "visibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace Gameplay::Visibility {

    // VisibilityManager implementation
    VisibilityManager& VisibilityManager::Instance() {
        static VisibilityManager instance;
        return instance;
    }

    void VisibilityManager::UpdateEntity(const EntityData& entity) {
        entities_[entity.id] = entity;
    }

    void VisibilityManager::RemoveEntity(uint64_t id) {
        entities_.erase(id);
        // Also remove from cache
        for (auto& [obs_id, cache_list] : cache_) {
            cache_list.erase(
                std::remove_if(cache_list.begin(), cache_list.end(),
                    [id](const CacheEntry& e) { return e.target_id == id; }),
                cache_list.end()
            );
        }
    }

    void VisibilityManager::ClearEntities() {
        entities_.clear();
        ClearCache();
    }

    VisibilityResult VisibilityManager::CheckVisibility(uint64_t target_id, int bone_index) {
        if (local_player_id_ == 0) return VisibilityResult{};
        return CheckVisibilityInternal(local_player_id_, target_id, bone_index);
    }

    VisibilityResult VisibilityManager::CheckVisibility(uint64_t observer_id, uint64_t target_id, int bone_index) {
        return CheckVisibilityInternal(observer_id, target_id, bone_index);
    }

    VisibilityResult VisibilityManager::CheckVisibilityInternal(uint64_t observer_id, uint64_t target_id, int bone_index) {
        stats_.checks_per_frame++;
        
        // Check cache first
        if (config_.enable_caching) {
            VisibilityResult cached = GetFromCache(observer_id, target_id, bone_index);
            if (cached.visible || (!cached.visible && cached.check_time.count() > 0)) {
                stats_.cache_hits++;
                return cached;
            }
        }
        stats_.cache_misses++;
        
        // Get entities
        auto obs_it = entities_.find(observer_id);
        auto tgt_it = entities_.find(target_id);
        if (obs_it == entities_.end() || tgt_it == entities_.end()) {
            return VisibilityResult{};
        }
        
        const EntityData& observer = obs_it->second;
        const EntityData& target = tgt_it->second;
        
        if (!target.is_alive) {
            return VisibilityResult{};
        }
        
        VisibilityResult result;
        
        // Choose check method
        switch (config_.method) {
            case CheckMethod::MultiPoint:
                result = PerformMultiPointCheck(observer, target);
                break;
            case CheckMethod::Raycast:
                result = PerformRaycastCheck(observer.position, target.bone_positions[bone_index >= 0 ? bone_index : 0]);
                break;
            case CheckMethod::Hybrid:
                result = PerformMultiPointCheck(observer, target);
                if (result.visibility < config_.multi_point.min_visible_points) {
                    // Fallback to single raycast for confirmation
                    VisibilityResult raycast = PerformRaycastCheck(observer.position, target.bone_positions[bone_index >= 0 ? bone_index : 0]);
                    if (raycast.visible) result.visible = true;
                }
                break;
            default:
                result = PerformRaycastCheck(observer.position, target.bone_positions[bone_index >= 0 ? bone_index : 0]);
        }
        
        // Update stats
        if (result.visible) stats_.visible_count++;
        else if (result.visibility > 0) stats_.partial_count++;
        else stats_.occluded_count++;
        
        // Cache result
        if (config_.enable_caching) {
            AddToCache(observer_id, target_id, bone_index, result);
        }
        
        return result;
    }

    std::vector<std::pair<uint64_t, VisibilityResult>> VisibilityManager::CheckMultiple(
        const std::vector<uint64_t>& targets, int bone_index) {
        std::vector<std::pair<uint64_t, VisibilityResult>> results;
        results.reserve(targets.size());
        
        int checks = 0;
        for (uint64_t target_id : targets) {
            if (checks >= config_.max_checks_per_frame) break;
            
            VisibilityResult result = CheckVisibility(target_id, bone_index);
            results.emplace_back(target_id, result);
            checks++;
        }
        
        return results;
    }

    VisibilityResult VisibilityManager::PerformMultiPointCheck(const EntityData& observer, const EntityData& target) {
        VisibilityResult result;
        result.visible = false;
        result.visibility = 0.0f;
        
        if (!config_.multi_point.check_head && !config_.multi_point.check_neck &&
            !config_.multi_point.check_chest && !config_.multi_point.check_pelvis) {
            return result;
        }
        
        // Define check points based on config
        struct CheckPoint {
            int bone_index;
            float weight;
        };
        
        std::vector<CheckPoint> check_points;
        
        if (config_.multi_point.check_head && target.bone_valid[static_cast<int>(VisibilityBone::Head)]) {
            check_points.push_back({static_cast<int>(VisibilityBone::Head), 1.0f});
        }
        if (config_.multi_point.check_neck && target.bone_valid[static_cast<int>(VisibilityBone::Neck)]) {
            check_points.push_back({static_cast<int>(VisibilityBone::Neck), 0.9f});
        }
        if (config_.multi_point.check_chest) {
            if (target.bone_valid[static_cast<int>(VisibilityBone::Chest)]) {
                check_points.push_back({static_cast<int>(VisibilityBone::Chest), 1.0f});
            } else if (target.bone_valid[static_cast<int>(VisibilityBone::UpperChest)]) {
                check_points.push_back({static_cast<int>(VisibilityBone::UpperChest), 0.9f});
            } else if (target.bone_valid[static_cast<int>(VisibilityBone::LowerChest)]) {
                check_points.push_back({static_cast<int>(VisibilityBone::LowerChest), 0.8f});
            }
        }
        if (config_.multi_point.check_pelvis && target.bone_valid[static_cast<int>(VisibilityBone::Pelvis)]) {
            check_points.push_back({static_cast<int>(VisibilityBone::Pelvis), 0.7f});
        }
        if (config_.multi_point.check_limbs) {
            if (target.bone_valid[static_cast<int>(VisibilityBone::LeftShoulder)]) {
                check_points.push_back({static_cast<int>(VisibilityBone::LeftShoulder), 0.4f});
            }
            if (target.bone_valid[static_cast<int>(VisibilityBone::RightShoulder)]) {
                check_points.push_back({static_cast<int>(VisibilityBone::RightShoulder), 0.4f});
            }
        }
        
        // Limit check points
        if (check_points.size() > config_.multi_point.max_points) {
            check_points.resize(config_.multi_point.max_points);
        }
        
        if (check_points.empty()) {
            return VisibilityResult{};
        }
        
        int visible_count = 0;
        float total_visibility = 0.0f;
        float total_weight = 0.0f;
        auto start_time = std::chrono::steady_clock::now();
        
        for (const auto& cp : check_points) {
            const Vec3& target_pos = target.bone_positions[cp.bone_index];
            if (target_pos.IsZero()) continue;
            
            VisibilityResult ray_result = PerformRaycastCheck(observer.position, target_pos);
            
            if (ray_result.visible) {
                visible_count++;
                total_visibility += cp.weight;
            } else if (ray_result.visibility > 0) {
                total_visibility += cp.weight * ray_result.visibility;
            }
            total_weight += cp.weight;
        }
        
        result.check_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time);
        
        if (total_weight > 0) {
            result.visibility = total_visibility / total_weight;
            result.visible = (visible_count / (float)check_points.size()) >= config_.multi_point.min_visible_points;
            
            // Find first visible bone for hit_bone
            for (const auto& cp : check_points) {
                const Vec3& target_pos = target.bone_positions[cp.bone_index];
                if (!target_pos.IsZero()) {
                    VisibilityResult ray_result = PerformRaycastCheck(observer.position, target_pos);
                    if (ray_result.visible) {
                        result.hit_bone = cp.bone_index;
                        break;
                    }
                }
            }
        }
        
        return result;
    }

    VisibilityResult VisibilityManager::PerformRaycastCheck(const Vec3& start, const Vec3& end) {
        VisibilityResult result;
        result.visible = false;
        result.visibility = 0.0f;
        result.distance = start.DistTo(end);
        
        auto start_time = std::chrono::steady_clock::now();
        
        Vec3 hit_pos;
        int surface_type = 0;
        bool through_smoke = false;
        
        bool hit = Raycast(start, end, &hit_pos, &surface_type, &through_smoke);
        
        result.check_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time);
        
        if (!hit) {
            result.visible = true;
            result.visibility = 1.0f;
        } else {
            // Check if hit is the target (would need target bounds check)
            // For now, assume hit means occluded
            result.visible = false;
            result.visibility = 0.0f;
            result.hit_bone = -1;
            result.surface_type = surface_type;
            result.through_smoke = through_smoke;
        }
        
        return result;
    }

    bool VisibilityManager::IsCacheValid(const CacheEntry& entry) const {
        if (!entry.valid) return false;
        auto now = std::chrono::steady_clock::now();
        float age = std::chrono::duration<float>(now - entry.timestamp).count();
        return age < config_.cache_duration;
    }

    VisibilityResult VisibilityManager::GetFromCache(uint64_t observer, uint64_t target, int bone) {
        auto it = cache_.find(observer);
        if (it == cache_.end()) return VisibilityResult{};
        
        for (const auto& entry : it->second) {
            if (entry.target_id == target && 
                (bone == -1 || entry.bone_index == bone) &&
                IsCacheValid(entry)) {
                return entry.result;
            }
        }
        return VisibilityResult{};
    }

    void VisibilityManager::AddToCache(uint64_t observer, uint64_t target, int bone, const VisibilityResult& result) {
        auto& cache_list = cache_[observer];
        
        // Check if already exists
        for (auto& entry : cache_list) {
            if (entry.target_id == target && entry.bone_index == bone) {
                entry.result = result;
                entry.timestamp = std::chrono::steady_clock::now();
                entry.valid = true;
                return;
            }
        }
        
        // Add new entry
        CacheEntry entry;
        entry.observer_id = observer;
        entry.target_id = target;
        entry.bone_index = bone;
        entry.result = result;
        entry.timestamp = std::chrono::steady_clock::now();
        entry.valid = true;
        
        cache_list.push_back(entry);
        
        // Limit cache size
        if (cache_list.size() > config_.max_cache_entries) {
            cache_list.erase(cache_list.begin());
        }
    }

    void VisibilityManager::ClearCache() {
        cache_.clear();
    }

    void VisibilityManager::PruneCache() {
        for (auto& [obs_id, cache_list] : cache_) {
            cache_list.erase(
                std::remove_if(cache_list.begin(), cache_list.end(),
                    [this](const CacheEntry& e) { return !IsCacheValid(e); }),
                cache_list.end()
            );
        }
    }

    size_t VisibilityManager::GetCacheSize() const {
        size_t total = 0;
        for (const auto& [obs_id, cache_list] : cache_) {
            total += cache_list.size();
        }
        return total;
    }

    void VisibilityManager::ResetStats() {
        stats_ = Stats{};
    }

    void VisibilityManager::DrawDebug(ImDrawList* draw, const Vec3& camera_pos, const Matrix& view_proj) {
        if (!config_.debug_draw) return;
        
        // Draw cached visibility results
        for (const auto& [obs_id, cache_list] : cache_) {
            for (const auto& entry : cache_list) {
                if (!entry.valid) continue;
                
                auto tgt_it = entities_.find(entry.target_id);
                if (tgt_it == entities_.end()) continue;
                
                const Vec3& pos = tgt_it->second.position;
                if (pos.IsZero()) continue;
                
                // World to screen
                Vec4 clip;
                clip.x = pos.x * view_proj(0, 0) + pos.y * view_proj(1, 0) + pos.z * view_proj(2, 0) + view_proj(3, 0);
                clip.y = pos.x * view_proj(0, 1) + pos.y * view_proj(1, 1) + pos.z * view_proj(2, 1) + view_proj(3, 1);
                clip.z = pos.x * view_proj(0, 2) + pos.y * view_proj(1, 2) + pos.z * view_proj(2, 2) + view_proj(3, 2);
                clip.w = pos.x * view_proj(0, 3) + pos.y * view_proj(1, 3) + pos.z * view_proj(2, 3) + view_proj(3, 3);
                
                if (clip.w < 0.1f) continue;
                
                ImVec2 screen(clip.x / clip.w, clip.y / clip.w);
                screen.x = (screen.x + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.x;
                screen.y = (1.0f - screen.y) * 0.5f * ImGui::GetIO().DisplaySize.y;
                
                ImU32 color = entry.result.visible ? config_.debug_visible_color :
                              entry.result.visibility > 0 ? config_.debug_partial_color :
                              config_.debug_occluded_color;
                
                draw_list->AddCircle(screen, 6.0f, color, 12, 2.0f);
                
                // Draw visibility percentage
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f%%", entry.result.visibility * 100);
                draw_list->AddText(ImVec2(screen.x + 8, screen.y - 8), color, buf);
            }
        }
    }

    // Vec4 for world to screen
    struct Vec4 {
        float x, y, z, w;
    };

    // Visibility presets
    VisibilityConfig GetLegitVisibilityConfig() {
        VisibilityConfig cfg;
        cfg.method = CheckMethod::MultiPoint;
        cfg.multi_point.check_head = true;
        cfg.multi_point.check_neck = true;
        cfg.multi_point.check_chest = true;
        cfg.multi_point.check_pelvis = true;
        cfg.multi_point.check_limbs = false;
        cfg.multi_point.min_visible_points = 0.4f;
        cfg.multi_point.max_points = 6;
        
        cfg.enable_caching = true;
        cfg.cache_duration = 0.1f;
        cfg.max_checks_per_frame = 16;
        cfg.async_checks = true;
        cfg.prioritize_closest = true;
        
        cfg.raycast.ignore_smoke = true;
        cfg.raycast.ignore_glass = true;
        cfg.raycast.ignore_grates = true;
        cfg.raycast.ignore_foliage = true;
        
        cfg.check_penetration = true;
        cfg.max_penetration_depth = 20.0f;
        
        return cfg;
    }

    VisibilityConfig GetRageVisibilityConfig() {
        VisibilityConfig cfg;
        cfg.method = CheckMethod::Hybrid;
        cfg.multi_point.check_head = true;
        cfg.multi_point.check_neck = true;
        cfg.multi_point.check_chest = true;
        cfg.multi_point.check_pelvis = true;
        cfg.multi_point.check_limbs = true;
        cfg.multi_point.min_visible_points = 0.2f;
        cfg.multi_point.max_points = 10;
        
        cfg.enable_caching = true;
        cfg.cache_duration = 0.05f;
        cfg.max_checks_per_frame = 64;
        cfg.async_checks = false;
        
        cfg.raycast.ignore_smoke = true;
        cfg.raycast.ignore_glass = true;
        cfg.raycast.ignore_grates = true;
        cfg.raycast.ignore_foliage = false;
        
        cfg.check_penetration = true;
        cfg.max_penetration_depth = 50.0f;
        cfg.max_penetration_count = 3;
        
        return cfg;
    }

    VisibilityConfig GetSniperVisibilityConfig() {
        VisibilityConfig cfg;
        cfg.method = CheckMethod::MultiPoint;
        cfg.multi_point.check_head = true;
        cfg.multi_point.check_neck = true;
        cfg.multi_point.check_chest = true;
        cfg.multi_point.check_pelvis = false;
        cfg.multi_point.min_visible_points = 0.6f;
        
        cfg.enable_caching = true;
        cfg.cache_duration = 0.2f;
        cfg.max_checks_per_frame = 8;
        cfg.prioritize_aim_target = true;
        
        cfg.raycast.ignore_smoke = true;
        cfg.raycast.ignore_glass = true;
        cfg.raycast.ignore_foliage = true;
        
        cfg.check_penetration = false;
        
        return cfg;
    }

    VisibilityConfig GetCompetitiveVisibilityConfig() {
        VisibilityConfig cfg;
        cfg.method = CheckMethod::MultiPoint;
        cfg.multi_point.check_head = true;
        cfg.multi_point.check_neck = true;
        cfg.multi_point.check_chest = true;
        cfg.multi_point.check_pelvis = true;
        cfg.multi_point.min_visible_points = 0.5f;
        cfg.multi_point.max_points = 6;
        
        cfg.enable_caching = true;
        cfg.cache_duration = 0.1f;
        cfg.max_checks_per_frame = 24;
        cfg.async_checks = true;
        
        cfg.raycast.ignore_smoke = true;
        cfg.raycast.ignore_glass = true;
        cfg.raycast.ignore_grates = true;
        cfg.raycast.ignore_foliage = true;
        
        cfg.check_penetration = true;
        cfg.max_penetration_depth = 15.0f;
        
        return cfg;
    }

    std::vector<int> GetStandardVisibilityBones(const char* game_name) {
        std::vector<int> bones;
        
        // Standard bones for all games
        bones.push_back(static_cast<int>(VisibilityBone::Head));
        bones.push_back(static_cast<int>(VisibilityBone::Neck));
        bones.push_back(static_cast<int>(VisibilityBone::Chest));
        bones.push_back(static_cast<int>(VisibilityBone::Pelvis));
        bones.push_back(static_cast<int>(VisibilityBone::LeftShoulder));
        bones.push_back(static_cast<int>(VisibilityBone::RightShoulder));
        
        return bones;
    }

    // Serialization
    std::string SerializeVisibilityConfig(const VisibilityConfig& config) {
        std::ostringstream out;
        out << static_cast<int>(config.method) << '|'
            << (config.multi_point.check_head ? 1 : 0) << '|'
            << (config.multi_point.check_neck ? 1 : 0) << '|'
            << (config.multi_point.check_chest ? 1 : 0) << '|'
            << (config.multi_point.check_pelvis ? 1 : 0) << '|'
            << (config.multi_point.check_limbs ? 1 : 0) << '|'
            << config.multi_point.min_visible_points << '|'
            << config.multi_point.max_points << '|'
            << (config.raycast.ignore_smoke ? 1 : 0) << '|'
            << (config.raycast.ignore_glass ? 1 : 0) << '|'
            << (config.raycast.ignore_grates ? 1 : 0) << '|'
            << (config.raycast.ignore_foliage ? 1 : 0) << '|'
            << config.raycast.max_distance << '|'
            << config.raycast.radius << '|'
            << (config.enable_caching ? 1 : 0) << '|'
            << config.cache_duration << '|'
            << config.max_cache_entries << '|'
            << (config.cache_per_bone ? 1 : 0) << '|'
            << config.max_checks_per_frame << '|'
            << (config.async_checks ? 1 : 0) << '|'
            << (config.prioritize_closest ? 1 : 0) << '|'
            << (config.prioritize_aim_target ? 1 : 0) << '|'
            << (config.check_penetration ? 1 : 0) << '|'
            << config.max_penetration_depth << '|'
            << config.max_penetration_count << '|'
            << (config.debug_draw ? 1 : 0);
        return out.str();
    }

    bool DeserializeVisibilityConfig(const std::string& data, VisibilityConfig& config) {
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
        
        read_int(*reinterpret_cast<int*>(&config.method));
        read_bool(config.multi_point.check_head);
        read_bool(config.multi_point.check_neck);
        read_bool(config.multi_point.check_chest);
        read_bool(config.multi_point.check_pelvis);
        read_bool(config.multi_point.check_limbs);
        read_float(config.multi_point.min_visible_points);
        read_int(config.multi_point.max_points);
        read_bool(config.raycast.ignore_smoke);
        read_bool(config.raycast.ignore_glass);
        read_bool(config.raycast.ignore_grates);
        read_bool(config.raycast.ignore_foliage);
        read_float(config.raycast.max_distance);
        read_float(config.raycast.radius);
        read_bool(config.enable_caching);
        read_float(config.cache_duration);
        read_int(config.max_cache_entries);
        read_bool(config.cache_per_bone);
        read_int(config.max_checks_per_frame);
        read_bool(config.async_checks);
        read_bool(config.prioritize_closest);
        read_bool(config.prioritize_aim_target);
        read_bool(config.check_penetration);
        read_float(config.max_penetration_depth);
        read_int(config.max_penetration_count);
        read_bool(config.debug_draw);
        
        return true;
    }

} // namespace Gameplay::Visibility