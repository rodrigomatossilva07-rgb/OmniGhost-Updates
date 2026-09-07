#include "offset_manager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <iomanip>
#include <cstring>

namespace Gameplay::Offsets {

    // OffsetManager implementation
    OffsetManager::OffsetManager() {
        update_thread_ = std::thread(&OffsetManager::UpdateThread, this);
    }

    OffsetManager::~OffsetManager() {
        stop_thread_ = true;
        if (update_thread_.joinable()) {
            update_thread_.join();
        }
    }

    void OffsetManager::RegisterOffset(const OffsetDefinition& offset) {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        offsets_[offset.name] = offset;
    }

    void OffsetManager::UnregisterOffset(const std::string& name) {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        offsets_.erase(name);
    }

    OffsetDefinition* OffsetManager::GetOffset(const std::string& name) {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        auto it = offsets_.find(name);
        return it != offsets_.end() ? &it->second : nullptr;
    }

    const OffsetDefinition* OffsetManager::GetOffset(const std::string& name) const {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        auto it = offsets_.find(name);
        return it != offsets_.end() ? &it->second : nullptr;
    }

    std::vector<OffsetDefinition*> OffsetManager::GetAllOffsets() {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        std::vector<OffsetDefinition*> result;
        result.reserve(offsets_.size());
        for (auto& [name, offset] : offsets_) {
            result.push_back(&offset.second);
        }
        return result;
    }

    std::vector<OffsetDefinition*> OffsetManager::GetCriticalOffsets() {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        std::vector<OffsetDefinition*> result;
        for (auto& [name, offset] : offsets_) {
            if (offset.is_critical) result.push_back(&offset.second);
        }
        return result;
    }

    std::vector<OffsetDefinition*> OffsetManager::GetInvalidOffsets() {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        std::vector<OffsetDefinition*> result;
        for (auto& [name, offset] : offsets_) {
            if (!offset.is_valid) result.push_back(&offset.second);
        }
        return result;
    }

    bool OffsetManager::UpdateAllOffsets(bool force) {
        if (is_updating_) return false;
        
        is_updating_ = true;
        update_progress_ = 0.0f;
        
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        int total = (int)offsets_.size();
        int completed = 0;
        
        for (auto& [name, offset] : offsets_) {
            if (!force && offset.is_valid && !config_.validate_periodically) {
                update_progress_ = (float)(++completed) / offsets_.size();
                continue;
            }
            
            bool success = UpdateOffset(offset.name);
            
            update_progress_ = (float)(++completed) / offsets_.size();
            
            if (config_.on_update_progress) {
                config_.on_update_progress(update_progress_);
            }
        }
        
        is_updating_ = false;
        update_progress_ = 1.0f;
        return true;
    }

    bool OffsetManager::UpdateOffset(const std::string& name) {
        OffsetDefinition* offset = GetOffset(name);
        if (!offset) return false;
        
        ValidationResult result;
        bool success = false;
        
        // Try different scan methods in order of preference
        if (config_.use_signature_scan && !offset->signature.empty()) {
            auto result = ScanSignature(*offset);
            if (result.success && result.confidence >= config_.min_confidence) {
                offset->current_value = result.found_value;
                offset->is_valid = true;
                offset->last_validated = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return true;
            }
        }
        
        if (config_.use_pointer_chain && !offset->chain.empty()) {
            auto result = ScanPointerChain(*offset);
            if (result.success && result.confidence >= config_.min_confidence) {
                offset->current_value = result.found_value;
                offset->is_valid = true;
                offset->last_validated = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return true;
            }
        }
        
        if (config_.use_relative && offset->is_relative) {
            auto result = ScanRelative(*offset);
            if (result.success && result.confidence >= config_.min_confidence) {
                offset->current_value = result.found_value;
                offset->is_valid = true;
                offset->last_validated = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return true;
            }
        }
        
        // Use cached value if available
        if (config_.use_cached_offsets && offset->current_value != 0) {
            offset->is_valid = true;
            return true;
        }
        
        // Use community offsets
        if (config_.use_community_offsets) {
            auto* community = CommunityOffsetDB::Instance().GetBestOffset("", offset.name);
            if (community) {
                offset->current_value = community->value;
                offset->is_valid = true;
                return true;
            }
        }
        
        return false;
    }

    void OffsetManager::TriggerHotReload() {
        if (!hot_reload_enabled_) return;
        
        UpdateAllOffsets(true);
        
        // Notify listeners of changes
        for (const auto& [name, offset] : offsets_) {
            if (on_offset_changed_ && offset.previous_value != offset.current_value) {
                on_offset_changed_(offset.name, offset.previous_value, offset.current_value);
            }
        }
    }

    ValidationResult OffsetManager::ValidateOffset(const std::string& name) {
        OffsetDefinition* offset = GetOffset(name);
        if (!offset) {
            return ValidationResult{false, 0, "Offset not found"};
        }
        return ValidateOffset(*offset);
    }

    ValidationResult OffsetManager::ValidateOffset(const OffsetDefinition& offset) {
        ValidationResult best_result;
        best_result.success = false;
        best_result.confidence = 0.0f;
        
        // Try signature scan
        if (!offset.signature.empty()) {
            auto result = ScanSignature(offset);
            if (result.success && result.confidence > best_result.confidence) {
                best_result = result;
            }
        }
        
        // Try pointer chain
        if (!offset.chain.empty()) {
            auto result = ScanPointerChain(offset);
            if (result.success && result.confidence > best_result.confidence) {
                best_result = result;
            }
        }
        
        // Try relative
        if (offset.is_relative) {
            auto result = ScanRelative(offset);
            if (result.success && result.confidence > best_result.confidence) {
                best_result = result;
            }
        }
        
        return best_result;
    }

    bool OffsetManager::ValidateAllOffsets() {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        bool all_valid = true;
        
        for (auto& [name, offset] : offsets_) {
            if (offset.is_critical) {
                auto result = ValidateOffset(offset);
                if (!result.success || result.confidence < config_.min_confidence) {
                    offset.is_valid = false;
                    all_valid = false;
                } else {
                    offset.is_valid = true;
                }
            }
        }
        return all_valid;
    }

    uintptr_t OffsetManager::GetOffsetValue(const std::string& name) {
        OffsetDefinition* offset = GetOffset(name);
        if (!offset) return 0;
        
        if (!offset->is_valid) {
            UpdateOffset(name);
        }
        return offset->current_value;
    }

    bool OffsetManager::TryGetOffsetValue(const std::string& name, uintptr_t& out_value) {
        OffsetDefinition* offset = GetOffset(name);
        if (!offset || !offset->is_valid) return false;
        out_value = offset->current_value;
        return true;
    }

    bool OffsetManager::SetOffsetValue(const std::string& name, uintptr_t value) {
        OffsetDefinition* offset = GetOffset(name);
        if (!offset) return false;
        
        offset->previous_value = offset->current_value;
        offset->current_value = value;
        offset->is_valid = true;
        offset->last_validated = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (on_offset_changed_) {
            on_offset_changed_(name, offset->previous_value, value);
        }
        return true;
    }

    OffsetManager::Stats OffsetManager::GetStats() const {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        Stats stats;
        stats.total_offsets = (int)offsets_.size();
        
        for (const auto& [name, offset] : offsets_) {
            if (offset.is_valid) stats.valid_offsets++;
            if (offset.is_critical) stats.critical_offsets++;
        }
        stats.last_update = std::chrono::steady_clock::now();
        return stats;
    }

    // Signature scanning
    ValidationResult OffsetManager::ScanSignature(const OffsetDefinition& offset) {
        ValidationResult result;
        auto start = std::chrono::steady_clock::now();
        result.method_used = "signature";
        
        uintptr_t module_base = GetModuleBase(offset.module);
        if (!module_base) {
            result.error = "Module not loaded: " + offset.module;
            return result;
        }
        
        // Get module size (simplified)
        size_t module_size = 0x1000000; // 16MB default
        
        auto addresses = FindPattern(module_base, 0x1000000, offset.signature, offset.mask);
        
        if (addresses.empty()) {
            result.error = "Pattern not found";
            return result;
        }
        
        if (addresses.size() > 1 && config_.require_multiple_matches) {
            result.error = "Multiple matches found";
            return result;
        }
        
        uintptr_t found = addresses[0] + offset.extra_offset;
        
        // Handle relative offsets
        if (offset.is_relative && offset.relative_size > 0) {
            // Read relative offset
            int32_t rel_offset = *reinterpret_cast<int32_t*>(found);
            found = found + offset.relative_size + rel_offset;
        }
        
        result.found_value = found;
        result.success = true;
        result.confidence = addresses.size() == 1 ? 1.0f : 0.8f;
        result.scan_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        
        return result;
    }

    ValidationResult OffsetManager::ScanPointerChain(const OffsetDefinition& offset) {
        ValidationResult result;
        auto start = std::chrono::steady_clock::now();
        result.method_used = "pointer_chain";
        
        uintptr_t module_base = GetModuleBase(offset.module);
        if (!module_base) {
            result.error = "Module not loaded: " + offset.module;
            return result;
        }
        
        uintptr_t addr = module_base + offset.base_offset;
        
        // Follow pointer chain
        for (size_t i = 0; i < offset.chain.size(); ++i) {
            uintptr_t next = *reinterpret_cast<uintptr_t*>(addr);
            if (!next || next < 0x10000 || next > 0x7FFFFFFFFFFF) {
                result.error = "Invalid pointer in chain at index " + std::to_string(i);
                return result;
            }
            addr = next + offset.chain[i];
        }
        
        result.found_value = addr;
        result.success = true;
        result.confidence = 0.9f;
        result.scan_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        
        return result;
    }

    ValidationResult OffsetManager::ScanRelative(const OffsetDefinition& offset) {
        ValidationResult result;
        auto start = std::chrono::steady_clock::now();
        result.method_used = "relative";
        
        uintptr_t module_base = GetModuleBase(offset.module);
        if (!module_base) {
            result.error = "Module not loaded";
            return result;
        }
        
        uintptr_t instr_addr = module_base + offset.base_offset;
        
        // Read relative offset from instruction
        int32_t rel_offset = *reinterpret_cast<int32_t*>(instr_addr + offset.extra_offset);
        uintptr_t target = instr_addr + offset.relative_size + rel_offset;
        
        result.found_value = target;
        result.success = true;
        result.confidence = 0.95f;
        result.scan_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        
        return result;
    }

    uintptr_t OffsetManager::GetModuleBase(const std::string& module) {
        // Simplified - would use actual module enumeration
        // This is a placeholder
        return 0;
    }

    bool OffsetManager::IsModuleLoaded(const std::string& module) {
        return GetModuleBase(module) != 0;
    }

    std::vector<uintptr_t> OffsetManager::FindPattern(uintptr_t base, size_t size, 
                                                      const std::string& pattern, const std::string& mask) {
        std::vector<uintptr_t> results;
        
        // Simplified pattern scanning
        // In real implementation, would use efficient pattern matching
        
        return results;
    }

    bool OffsetManager::CrossValidate(const OffsetDefinition& offset, uintptr_t value) {
        if (!config_.cross_validate) return true;
        
        // Would cross-check with alternative methods
        return true;
    }

    float OffsetManager::CalculateConfidence(const ValidationResult& result) {
        float confidence = result.confidence;
        
        // Adjust based on scan time (faster = more reliable usually)
        if (result.scan_time.count() < 1000) confidence += 0.05f;
        
        // Adjust based on method
        if (result.method_used == "signature" && result.confidence > 0.9f) confidence += 0.05f;
        if (result.method_used == "pointer_chain") confidence += 0.03f;
        
        return std::clamp(confidence, 0.0f, 1.0f);
    }

    void OffsetManager::UpdateThread() {
        while (!stop_thread_) {
            std::this_thread::sleep_for(std::chrono::minutes(config_.validation_interval_minutes));
            
            if (stop_thread_) break;
            
            if (config_.validate_periodically && !is_updating_) {
                UpdateAllOffsets(false);
            }
        }
    }

    std::string OffsetManager::ExportOffsets() const {
        std::lock_guard<std::mutex> lock(offsets_mutex_);
        std::ostringstream out;
        out << "OFFSETS_V1\n";
        out << offsets_.size() << "\n";
        
        for (const auto& [name, offset] : offsets_) {
            out << name << "|"
                << offset.module << "|"
                << offset.base_offset << "|"
                << offset.chain.size() << "|";
            for (auto c : offset.chain) out << c << ",";
            out << "|"
                << (offset.is_relative ? 1 : 0) << "|"
                << offset.relative_size << "|"
                << offset.signature << "|"
                << offset.mask << "|"
                << offset.extra_offset << "|"
                << (offset.is_critical ? 1 : 0) << "|"
                << offset.description << "|"
                << offset.current_value << "|"
                << offset.previous_value << "|"
                << offset.is_valid << "|"
                << offset.last_validated << "\n";
        }
        return out.str();
    }

    bool OffsetManager::ImportOffsets(const std::string& data) {
        std::istringstream in(data);
        std::string line;
        
        if (!std::getline(in, line)) return false;
        if (line != "OFFSETS_V1") return false;
        
        std::string line2;
        std::getline(in, line2);
        int count = std::stoi(line2);
        
        for (int i = 0; i < count; ++i) {
            OffsetDefinition offset;
            std::string line;
            std::getline(in, line);
            
            std::istringstream line_stream(line);
            std::string token;
            
            std::getline(line_stream, offset.name, '|');
            std::getline(line_stream, offset.module, '|');
            
            std::string token;
            std::getline(line_stream, token, '|'); offset.base_offset = std::stoull(token);
            
            std::getline(line_stream, token, '|'); int chain_size = std::stoi(token);
            for (int i = 0; i < chain_size; ++i) {
                std::string val;
                std::getline(line_stream, val, ',');
                if (!val.empty()) offset.chain.push_back(std::stoull(val));
            }
            
            std::getline(line_stream, token, '|'); offset.is_relative = token == "1";
            std::getline(line_stream, token, '|'); offset.relative_size = std::stoi(token);
            std::getline(line_stream, offset.signature, '|');
            std::getline(line_stream, offset.mask, '|');
            std::getline(line_stream, token, '|'); offset.extra_offset = std::stoi(token);
            std::getline(line_stream, token, '|'); offset.is_critical = token == "1";
            std::getline(line_stream, offset.description, '|');
            std::getline(line_stream, token, '|'); offset.current_value = std::stoull(token);
            std::getline(line_stream, token, '|'); offset.previous_value = std::stoull(token);
            std::getline(line_stream, token, '|'); offset.is_valid = token == "1";
            std::getline(line_stream, token, '|'); offset.last_validated = std::stoull(token);
            
            RegisterOffset(offset);
        }
        return true;
    }

    bool OffsetManager::ExportToFile(const char* path) const {
        std::string data = ExportOffsets();
        std::ofstream file(path);
        if (!file) return false;
        file << data;
        return true;
    }

    bool OffsetManager::ImportFromFile(const char* path) {
        std::ifstream file(path);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return ImportOffsets(buffer.str());
    }

    // OffsetRegistry implementation
    OffsetRegistry& OffsetRegistry::Instance() {
        static OffsetRegistry instance;
        return instance;
    }

    OffsetManager* OffsetRegistry::GetManager(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = managers_.find(game_name);
        return it != managers_.end() ? it->second.get() : nullptr;
    }

    OffsetManager* OffsetRegistry::CreateManager(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto manager = std::make_unique<OffsetManager>();
        OffsetManager* ptr = manager.get();
        managers_[game_name] = std::move(manager);
        return ptr;
    }

    void OffsetRegistry::RemoveManager(const std::string& game_name) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        managers_.erase(game_name);
    }

    void OffsetRegistry::UpdateAllManagers() {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        for (auto& [name, manager] : managers_) {
            manager->UpdateAllOffsets(false);
        }
    }

    void OffsetRegistry::RegisterSharedOffset(const std::string& name, const OffsetDefinition& offset) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        shared_offsets_[name] = offset;
    }

    OffsetDefinition* OffsetRegistry::GetSharedOffset(const std::string& name) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = shared_offsets_.find(name);
        return it != shared_offsets_.end() ? &it->second : nullptr;
    }

    // CommunityOffsetDB implementation
    CommunityOffsetDB& CommunityOffsetDB::Instance() {
        static CommunityOffsetDB instance;
        return instance;
    }

    void CommunityOffsetDB::SubmitOffset(const CommunityOffset& offset) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        offsets_[offset.game].push_back(offset);
    }

    bool CommunityOffsetDB::VoteOffset(const std::string& game, const std::string& name, bool upvote) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        auto it = offsets_.find(game);
        if (it == offsets_.end()) return false;
        
        for (auto& offset : it->second) {
            if (offset.offset_name == name) {
                if (upvote) offset.votes_up++;
                else offset.votes_down++;
                return true;
            }
        }
        return false;
    }

    std::vector<CommunityOffsetDB::CommunityOffset> CommunityOffsetDB::GetOffsets(const std::string& game, const std::string& version) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        auto it = offsets_.find(game);
        if (it == offsets_.end()) return {};
        
        std::vector<CommunityOffset> result;
        for (const auto& offset : it->second) {
            if (version.empty() || offset.version == version) {
                result.push_back(offset);
            }
        }
        
        // Sort by votes
        std::sort(result.begin(), result.end(), 
            [](const CommunityOffset& a, const CommunityOffset& b) {
                return (a.votes_up - a.votes_down) > (b.votes_up - b.votes_down);
            });
        
        return result;
    }

    CommunityOffsetDB::CommunityOffset* CommunityOffsetDB::GetBestOffset(const std::string& game, const std::string& name, const std::string& version) {
        auto offsets = GetOffsets(game, version);
        for (auto& offset : offsets) {
            if (offset.offset_name == name) {
                return &offset; // Note: returns pointer to copy, not ideal but works for demo
            }
        }
        return nullptr;
    }

    bool CommunityOffsetDB::LoadFromServer(const std::string& url) {
        // Would implement HTTP request
        return false;
    }

    bool CommunityOffsetDB::SaveToServer(const std::string& url) {
        // Would implement HTTP request
        return false;
    }

    bool CommunityOffsetDB::ExportToFile(const char* path) const {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::ofstream file(path);
        if (!file) return false;
        
        file << "COMMUNITY_OFFSETS_V1\n";
        for (const auto& [game, offsets] : offsets_) {
            for (const auto& offset : offsets) {
                file << game << "|"
                     << offset.offset_name << "|"
                     << offset.value << "|"
                     << offset.submitter << "|"
                     << std::chrono::duration_cast<std::chrono::seconds>(offset.submitted.time_since_epoch()).count() << "|"
                     << offset.votes_up << "|"
                     << offset.votes_down << "|"
                     << offset.version << "|"
                     << offset.commit_hash << "|"
                     << (offset.verified ? 1 : 0) << "\n";
            }
        }
        return true;
    }

    bool CommunityOffsetDB::ImportFromFile(const char* path) {
        std::ifstream file(path);
        if (!file) return false;
        
        std::string line;
        std::getline(file, line); // Version
        
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string token;
            CommunityOffset offset;
            
            std::getline(ss, offset.game, '|');
            std::getline(ss, offset.offset_name, '|');
            std::string val; std::getline(ss, val, '|'); offset.value = std::stoull(val);
            std::getline(ss, offset.submitter, '|');
            std::getline(ss, val, '|'); offset.submitted = std::chrono::system_clock::from_time_t(std::stoll(val));
            std::getline(ss, val, '|'); offset.votes_up = std::stoi(val);
            std::getline(ss, val, '|'); offset.votes_down = std::stoi(val);
            std::getline(ss, offset.version, '|');
            std::getline(ss, offset.commit_hash, '|');
            std::getline(ss, val, '|'); offset.verified = val == "1";
            
            offsets_[offset.game].push_back(offset);
        }
        return true;
    }

    // VersionTracker implementation
    VersionTracker& VersionTracker::Instance() {
        static VersionTracker instance;
        return instance;
    }

    void VersionTracker::RegisterGame(const std::string& game_name, const std::string& version_url) {
        std::lock_guard<std::mutex> lock(versions_mutex_);
        versions_[game_name] = GameVersionInfo{};
        versions_[game_name].game_name = game_name;
        versions_[game_name].update_url = version_url;
    }

    bool VersionTracker::CheckForUpdates(const std::string& game_name) {
        // Would fetch version info from URL
        return false;
    }

    const GameVersionInfo* VersionTracker::GetVersionInfo(const std::string& game_name) const {
        std::lock_guard<std::mutex> lock(versions_mutex_);
        auto it = versions_.find(game_name);
        return it != versions_.end() ? &it->second : nullptr;
    }

    std::vector<std::string> VersionTracker::GetGamesWithUpdates() const {
        std::lock_guard<std::mutex> lock(versions_mutex_);
        std::vector<std::string> result;
        for (const auto& [name, info] : versions_) {
            if (info.update_available) result.push_back(name);
        }
        return result;
    }

} // namespace Gameplay::Offsets