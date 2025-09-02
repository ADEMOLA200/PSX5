#include "trophy_system.h"
#include "../core/logger.h"
#include <fstream>
#include <filesystem>
#include <json/json.h>
#include <algorithm>
#include <cmath>

namespace PS5Emu {

TrophySystem::TrophySystem(const std::string& save_directory) 
    : save_directory_(save_directory) {
    
    // Create save directory if it doesn't exist
    std::filesystem::create_directories(save_directory_);
    std::filesystem::create_directories(save_directory_ + "/trophies");
    std::filesystem::create_directories(save_directory_ + "/trophy_sets");
    
    session_start_time_ = std::chrono::system_clock::now();
    
    log::info("Trophy system initialized with save directory: " + save_directory_);
}

TrophySystem::~TrophySystem() {
    // Save all user data on shutdown
    for (const auto& [user_id, user_data] : user_data_) {
        save_user_data(user_id);
    }
}

bool TrophySystem::initialize_user(const std::string& user_id, const std::string& psn_id) {
    if (user_data_.find(user_id) != user_data_.end()) {
        return true; // User already exists
    }
    
    UserTrophyData user_data;
    user_data.user_id = user_id;
    user_data.psn_id = psn_id;
    user_data.trophy_level = 1;
    user_data.trophy_points = 0;
    user_data.platinum_trophies = 0;
    user_data.gold_trophies = 0;
    user_data.silver_trophies = 0;
    user_data.bronze_trophies = 0;
    
    if (!load_user_data(user_id)) {
        user_data_[user_id] = user_data;
        save_user_data(user_id);
    }
    
    log::info("Initialized user: " + user_id + " (PSN: " + psn_id + ")");
    return true;
}

bool TrophySystem::set_current_user(const std::string& user_id) {
    if (user_data_.find(user_id) == user_data_.end()) {
        log::error("User not found: " + user_id);
        return false;
    }
    
    current_user_id_ = user_id;
    log::info("Set current user: " + user_id);
    return true;
}

bool TrophySystem::load_game_trophies(const std::string& title_id, const std::string& npcommid) {
    if (current_user_id_.empty()) {
        log::error("No current user set");
        return false;
    }
    
    auto& user_data = user_data_[current_user_id_];
    
    if (user_data.trophy_sets.find(npcommid) != user_data.trophy_sets.end()) {
        current_game_id_ = npcommid;
        return true;
    }
    
    TrophySet trophy_set;
    if (!load_trophy_set(npcommid, trophy_set)) {
        log::error("Failed to load trophy set: " + npcommid);
        return false;
    }
    
    trophy_set.title_id = title_id;
    
    // Initialize progress for all trophies
    for (const auto& trophy : trophy_set.trophies) {
        if (trophy_set.progress.find(trophy.trophy_id) == trophy_set.progress.end()) {
            TrophyProgress progress;
            progress.trophy_id = trophy.trophy_id;
            progress.unlocked = false;
            progress.current_progress = 0;
            progress.max_progress = trophy.unlock_condition.target_value;
            trophy_set.progress[trophy.trophy_id] = progress;
        }
    }
    
    user_data.trophy_sets[npcommid] = trophy_set;
    current_game_id_ = npcommid;
    
    log::info("Loaded trophy set: " + npcommid + " (" + std::to_string(trophy_set.trophy_count) + " trophies)");
    return true;
}

bool TrophySystem::load_trophy_set(const std::string& npcommid, TrophySet& trophy_set) {
    std::string filepath = save_directory_ + "/trophy_sets/" + npcommid + ".json";
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Create default trophy set for testing
        trophy_set.npcommid = npcommid;
        trophy_set.title_name = "Test Game";
        trophy_set.trophy_set_version = "1.00";
        
        // Create sample trophies
        TrophyDefinition welcome_trophy;
        welcome_trophy.trophy_id = 1;
        welcome_trophy.name = "Welcome";
        welcome_trophy.description = "Start the game";
        welcome_trophy.type = TrophyType::BRONZE;
        welcome_trophy.grade = TrophyGrade::BRONZE;
        welcome_trophy.hidden = false;
        welcome_trophy.unlock_condition.type = TrophyDefinition::UnlockCondition::GAME_EVENT;
        welcome_trophy.unlock_condition.event_name = "game_start";
        welcome_trophy.unlock_condition.target_value = 1;
        
        TrophyDefinition progress_trophy;
        progress_trophy.trophy_id = 2;
        progress_trophy.name = "Progress Master";
        progress_trophy.description = "Reach 50% completion";
        progress_trophy.type = TrophyType::SILVER;
        progress_trophy.grade = TrophyGrade::SILVER;
        progress_trophy.hidden = false;
        progress_trophy.unlock_condition.type = TrophyDefinition::UnlockCondition::PROGRESS_VALUE;
        progress_trophy.unlock_condition.event_name = "completion_progress";
        progress_trophy.unlock_condition.target_value = 50;
        
        TrophyDefinition platinum_trophy;
        platinum_trophy.trophy_id = 3;
        platinum_trophy.name = "Platinum Trophy";
        platinum_trophy.description = "Unlock all other trophies";
        platinum_trophy.type = TrophyType::PLATINUM;
        platinum_trophy.grade = TrophyGrade::PLATINUM;
        platinum_trophy.hidden = false;
        platinum_trophy.prerequisites = {1, 2};
        
        trophy_set.trophies = {welcome_trophy, progress_trophy, platinum_trophy};
        trophy_set.trophy_count = 3;
        trophy_set.bronze_count = 1;
        trophy_set.silver_count = 1;
        trophy_set.gold_count = 0;
        trophy_set.platinum_count = 1;
        
        return true;
    }
    
    Json::Value root;
    file >> root;
    
    trophy_set.npcommid = root["npcommid"].asString();
    trophy_set.title_name = root["title_name"].asString();
    trophy_set.trophy_set_version = root["version"].asString();
    trophy_set.trophy_count = root["trophy_count"].asUInt();
    trophy_set.bronze_count = root["bronze_count"].asUInt();
    trophy_set.silver_count = root["silver_count"].asUInt();
    trophy_set.gold_count = root["gold_count"].asUInt();
    trophy_set.platinum_count = root["platinum_count"].asUInt();
    
    // Load trophy definitions
    const Json::Value& trophies = root["trophies"];
    for (const auto& trophy_json : trophies) {
        TrophyDefinition trophy;
        trophy.trophy_id = trophy_json["id"].asUInt();
        trophy.name = trophy_json["name"].asString();
        trophy.description = trophy_json["description"].asString();
        trophy.type = static_cast<TrophyType>(trophy_json["type"].asUInt());
        trophy.grade = static_cast<TrophyGrade>(trophy_json["grade"].asUInt());
        trophy.hidden = trophy_json["hidden"].asBool();
        
        // Load unlock condition
        const Json::Value& condition = trophy_json["unlock_condition"];
        trophy.unlock_condition.type = static_cast<TrophyDefinition::UnlockCondition::Type>(condition["type"].asInt());
        trophy.unlock_condition.event_name = condition["event_name"].asString();
        trophy.unlock_condition.target_value = condition["target_value"].asUInt64();
        
        trophy_set.trophies.push_back(trophy);
    }
    
    return true;
}

bool TrophySystem::unlock_trophy(uint32_t trophy_id) {
    if (current_user_id_.empty() || current_game_id_.empty()) {
        return false;
    }
    
    auto& user_data = user_data_[current_user_id_];
    auto& trophy_set = user_data.trophy_sets[current_game_id_];
    
    auto progress_it = trophy_set.progress.find(trophy_id);
    if (progress_it == trophy_set.progress.end()) {
        return false;
    }
    
    auto& progress = progress_it->second;
    if (progress.unlocked) {
        return true; // Already unlocked
    }
    
    // Find trophy definition
    const TrophyDefinition* trophy_def = nullptr;
    for (const auto& trophy : trophy_set.trophies) {
        if (trophy.trophy_id == trophy_id) {
            trophy_def = &trophy;
            break;
        }
    }
    
    if (!trophy_def) {
        return false;
    }
    
    // Check prerequisites
    for (uint32_t prereq_id : trophy_def->prerequisites) {
        auto prereq_it = trophy_set.progress.find(prereq_id);
        if (prereq_it == trophy_set.progress.end() || !prereq_it->second.unlocked) {
            log::info("Trophy " + std::to_string(trophy_id) + " prerequisites not met");
            return false;
        }
    }
    
    // Unlock the trophy
    progress.unlocked = true;
    progress.unlock_time = std::chrono::system_clock::now();
    progress.current_progress = progress.max_progress;
    
    // Update user statistics
    switch (trophy_def->type) {
        case TrophyType::BRONZE:
            user_data.bronze_trophies++;
            user_data.trophy_points += 15;
            break;
        case TrophyType::SILVER:
            user_data.silver_trophies++;
            user_data.trophy_points += 30;
            break;
        case TrophyType::GOLD:
            user_data.gold_trophies++;
            user_data.trophy_points += 90;
            break;
        case TrophyType::PLATINUM:
            user_data.platinum_trophies++;
            user_data.trophy_points += 300;
            break;
    }
    
    calculate_trophy_level(user_data);
    
    // Trigger callbacks
    for (const auto& callback : unlock_callbacks_) {
        callback(*trophy_def, current_user_id_);
    }
    
    log::info("Trophy unlocked: " + trophy_def->name + " (" + trophy_def->description + ")");
    
    // Check for platinum unlock
    if (trophy_def->type != TrophyType::PLATINUM) {
        check_platinum_unlock(current_game_id_);
    }
    
    // Save progress
    save_user_data(current_user_id_);
    
    return true;
}

void TrophySystem::calculate_trophy_level(UserTrophyData& user_data) {
    // PlayStation trophy level calculation
    uint64_t points = user_data.trophy_points;
    
    if (points < 200) {
        user_data.trophy_level = 1;
    } else if (points < 600) {
        user_data.trophy_level = 2;
    } else if (points < 1200) {
        user_data.trophy_level = 3;
    } else if (points < 2400) {
        user_data.trophy_level = 4;
    } else if (points < 4000) {
        user_data.trophy_level = 5;
    } else {
        // Level 6+ uses exponential scaling
        user_data.trophy_level = 6 + static_cast<uint32_t>(std::log2((points - 4000) / 2000.0));
    }
    
    // Cap at level 999
    if (user_data.trophy_level > 999) {
        user_data.trophy_level = 999;
    }
}

void TrophySystem::check_platinum_unlock(const std::string& npcommid) {
    auto& user_data = user_data_[current_user_id_];
    auto& trophy_set = user_data.trophy_sets[npcommid];
    
    // Find platinum trophy
    uint32_t platinum_id = 0;
    for (const auto& trophy : trophy_set.trophies) {
        if (trophy.type == TrophyType::PLATINUM) {
            platinum_id = trophy.trophy_id;
            break;
        }
    }
    
    if (platinum_id == 0) return; // No platinum trophy
    
    // Check if already unlocked
    auto progress_it = trophy_set.progress.find(platinum_id);
    if (progress_it != trophy_set.progress.end() && progress_it->second.unlocked) {
        return;
    }
    
    // Check if all other trophies are unlocked
    bool all_unlocked = true;
    for (const auto& trophy : trophy_set.trophies) {
        if (trophy.type != TrophyType::PLATINUM) {
            auto prog_it = trophy_set.progress.find(trophy.trophy_id);
            if (prog_it == trophy_set.progress.end() || !prog_it->second.unlocked) {
                all_unlocked = false;
                break;
            }
        }
    }
    
    if (all_unlocked) {
        unlock_trophy(platinum_id);
    }
}

void TrophySystem::trigger_event(const std::string& event_name, uint64_t value) {
    game_events_[event_name] += value;
    
    // Check all trophy conditions that depend on this event
    if (!current_game_id_.empty()) {
        check_all_trophy_conditions();
    }
}

void TrophySystem::check_all_trophy_conditions() {
    if (current_user_id_.empty() || current_game_id_.empty()) {
        return;
    }
    
    auto& user_data = user_data_[current_user_id_];
    auto& trophy_set = user_data.trophy_sets[current_game_id_];
    
    for (const auto& trophy : trophy_set.trophies) {
        if (!is_trophy_unlocked(trophy.trophy_id)) {
            if (evaluate_unlock_condition(trophy)) {
                unlock_trophy(trophy.trophy_id);
            }
        }
    }
}

bool TrophySystem::evaluate_unlock_condition(const TrophyDefinition& trophy) {
    const auto& condition = trophy.unlock_condition;
    
    switch (condition.type) {
        case TrophyDefinition::UnlockCondition::GAME_EVENT: {
            auto it = game_events_.find(condition.event_name);
            return it != game_events_.end() && it->second >= condition.target_value;
        }
        
        case TrophyDefinition::UnlockCondition::PROGRESS_VALUE: {
            auto it = progress_counters_.find(condition.event_name);
            return it != progress_counters_.end() && it->second >= condition.target_value;
        }
        
        case TrophyDefinition::UnlockCondition::TIME_BASED: {
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - session_start_time_);
            return duration.count() >= static_cast<int64_t>(condition.target_value);
        }
        
        default:
            return false;
    }
}

bool TrophySystem::save_user_data(const std::string& user_id) {
    auto it = user_data_.find(user_id);
    if (it == user_data_.end()) {
        return false;
    }
    
    const auto& user_data = it->second;
    std::string filepath = save_directory_ + "/trophies/" + user_id + ".json";
    
    Json::Value root;
    root["user_id"] = user_data.user_id;
    root["psn_id"] = user_data.psn_id;
    root["trophy_level"] = user_data.trophy_level;
    root["trophy_points"] = static_cast<Json::UInt64>(user_data.trophy_points);
    root["platinum_trophies"] = user_data.platinum_trophies;
    root["gold_trophies"] = user_data.gold_trophies;
    root["silver_trophies"] = user_data.silver_trophies;
    root["bronze_trophies"] = user_data.bronze_trophies;
    
    // Save trophy sets
    Json::Value trophy_sets;
    for (const auto& [npcommid, trophy_set] : user_data.trophy_sets) {
        Json::Value set_json;
        set_json["npcommid"] = trophy_set.npcommid;
        set_json["title_id"] = trophy_set.title_id;
        
        Json::Value progress_json;
        for (const auto& [trophy_id, progress] : trophy_set.progress) {
            Json::Value prog;
            prog["trophy_id"] = trophy_id;
            prog["unlocked"] = progress.unlocked;
            prog["current_progress"] = static_cast<Json::UInt64>(progress.current_progress);
            prog["max_progress"] = static_cast<Json::UInt64>(progress.max_progress);
            
            if (progress.unlocked) {
                auto time_t = std::chrono::system_clock::to_time_t(progress.unlock_time);
                prog["unlock_time"] = static_cast<Json::UInt64>(time_t);
            }
            
            progress_json[std::to_string(trophy_id)] = prog;
        }
        set_json["progress"] = progress_json;
        
        trophy_sets[npcommid] = set_json;
    }
    root["trophy_sets"] = trophy_sets;
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << root;
    return true;
}

bool TrophySystem::load_user_data(const std::string& user_id) {
    std::string filepath = save_directory_ + "/trophies/" + user_id + ".json";
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    Json::Value root;
    file >> root;
    
    UserTrophyData user_data;
    user_data.user_id = root["user_id"].asString();
    user_data.psn_id = root["psn_id"].asString();
    user_data.trophy_level = root["trophy_level"].asUInt();
    user_data.trophy_points = root["trophy_points"].asUInt64();
    user_data.platinum_trophies = root["platinum_trophies"].asUInt();
    user_data.gold_trophies = root["gold_trophies"].asUInt();
    user_data.silver_trophies = root["silver_trophies"].asUInt();
    user_data.bronze_trophies = root["bronze_trophies"].asUInt();
    
    // Load trophy sets
    const Json::Value& trophy_sets = root["trophy_sets"];
    for (const auto& npcommid : trophy_sets.getMemberNames()) {
        const Json::Value& set_json = trophy_sets[npcommid];
        
        TrophySet trophy_set;
        if (load_trophy_set(npcommid, trophy_set)) {
            trophy_set.title_id = set_json["title_id"].asString();
            
            // Load progress
            const Json::Value& progress_json = set_json["progress"];
            for (const auto& trophy_id_str : progress_json.getMemberNames()) {
                uint32_t trophy_id = std::stoul(trophy_id_str);
                const Json::Value& prog = progress_json[trophy_id_str];
                
                TrophyProgress progress;
                progress.trophy_id = trophy_id;
                progress.unlocked = prog["unlocked"].asBool();
                progress.current_progress = prog["current_progress"].asUInt64();
                progress.max_progress = prog["max_progress"].asUInt64();
                
                if (progress.unlocked && prog.isMember("unlock_time")) {
                    auto time_t = static_cast<std::time_t>(prog["unlock_time"].asUInt64());
                    progress.unlock_time = std::chrono::system_clock::from_time_t(time_t);
                }
                
                trophy_set.progress[trophy_id] = progress;
            }
            
            user_data.trophy_sets[npcommid] = trophy_set;
        }
    }
    
    user_data_[user_id] = user_data;
    return true;
}

void TrophySystem::register_unlock_callback(std::function<void(const TrophyDefinition&, const std::string&)> callback) {
    unlock_callbacks_.push_back(callback);
}

bool TrophySystem::is_trophy_unlocked(uint32_t trophy_id) {
    if (current_user_id_.empty() || current_game_id_.empty()) {
        return false;
    }
    
    auto& user_data = user_data_[current_user_id_];
    auto& trophy_set = user_data.trophy_sets[current_game_id_];
    
    auto it = trophy_set.progress.find(trophy_id);
    return it != trophy_set.progress.end() && it->second.unlocked;
}

void TrophySystem::on_game_start() {
    session_start_time_ = std::chrono::system_clock::now();
    trigger_event("game_start", 1);
    
    log::info("Trophy system: Game session started");
}

TrophyBIOSInterface::TrophyBIOSInterface(std::shared_ptr<TrophySystem> trophy_system)
    : trophy_system_(trophy_system) {
}

void TrophyBIOSInterface::handle_trophy_unlock(uint64_t* registers) {
    uint32_t trophy_id = static_cast<uint32_t>(registers[0]);
    bool success = trophy_system_->unlock_trophy(trophy_id);
    registers[0] = success ? 0 : -1;
}

void TrophyBIOSInterface::notify_game_event(const std::string& event_name, uint64_t value) {
    trophy_system_->trigger_event(event_name, value);
}

} // namespace PS5Emu
