#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <functional>

namespace PS5Emu {

enum class TrophyType : uint32_t {
    BRONZE = 1,
    SILVER = 2,
    GOLD = 3,
    PLATINUM = 4
};

enum class TrophyGrade : uint32_t {
    UNKNOWN = 0,
    BRONZE = 1,
    SILVER = 2,
    GOLD = 3,
    PLATINUM = 4
};

struct TrophyDefinition {
    uint32_t trophy_id;
    std::string name;
    std::string description;
    std::string detail;
    TrophyType type;
    TrophyGrade grade;
    bool hidden;
    std::string icon_path;
    std::vector<uint32_t> prerequisites; // Other trophy IDs required
    
    // Unlock conditions
    struct UnlockCondition {
        enum Type {
            PROGRESS_VALUE,    // Reach specific progress value
            GAME_EVENT,        // Specific game event triggered
            COMBINATION,       // Multiple conditions met
            TIME_BASED,        // Play for specific duration
            COLLECTIBLE,       // Collect specific items
            ACHIEVEMENT        // Complete specific achievement
        } type;
        
        std::string event_name;
        uint64_t target_value;
        std::vector<std::string> required_events;
    } unlock_condition;
};

struct TrophyProgress {
    uint32_t trophy_id;
    bool unlocked;
    std::chrono::system_clock::time_point unlock_time;
    uint64_t current_progress;
    uint64_t max_progress;
    std::vector<std::string> completed_events;
};

struct TrophySet {
    std::string npcommid;        // PlayStation Network Communication ID
    std::string title_id;        // Game title ID
    std::string title_name;      // Game title
    std::string trophy_set_version;
    uint32_t trophy_count;
    uint32_t platinum_count;
    uint32_t gold_count;
    uint32_t silver_count;
    uint32_t bronze_count;
    
    std::vector<TrophyDefinition> trophies;
    std::unordered_map<uint32_t, TrophyProgress> progress;
};

struct UserTrophyData {
    std::string user_id;
    std::string psn_id;
    uint32_t trophy_level;
    uint64_t trophy_points;
    uint32_t platinum_trophies;
    uint32_t gold_trophies;
    uint32_t silver_trophies;
    uint32_t bronze_trophies;
    
    std::unordered_map<std::string, TrophySet> trophy_sets; // Key: npcommid
};

class TrophySystem {
private:
    std::string save_directory_;
    std::unordered_map<std::string, UserTrophyData> user_data_; // Key: user_id
    std::string current_user_id_;
    std::string current_game_id_;
    
    // Event tracking for trophy conditions
    std::unordered_map<std::string, uint64_t> game_events_;
    std::unordered_map<std::string, uint64_t> progress_counters_;
    std::chrono::system_clock::time_point session_start_time_;
    
    // Trophy unlock callbacks
    std::vector<std::function<void(const TrophyDefinition&, const std::string&)>> unlock_callbacks_;
    
    bool load_user_data(const std::string& user_id);
    bool save_user_data(const std::string& user_id);
    bool load_trophy_set(const std::string& npcommid, TrophySet& trophy_set);
    void calculate_trophy_level(UserTrophyData& user_data);
    void check_platinum_unlock(const std::string& npcommid);
    bool evaluate_unlock_condition(const TrophyDefinition& trophy);
    void trigger_trophy_unlock(uint32_t trophy_id, const std::string& npcommid);
    
public:
    TrophySystem(const std::string& save_directory);
    ~TrophySystem();
    
    // User management
    bool initialize_user(const std::string& user_id, const std::string& psn_id);
    bool set_current_user(const std::string& user_id);
    UserTrophyData* get_current_user_data();
    
    // Game management
    bool load_game_trophies(const std::string& title_id, const std::string& npcommid);
    bool set_current_game(const std::string& title_id);
    TrophySet* get_current_trophy_set();
    
    // Trophy operations
    bool unlock_trophy(uint32_t trophy_id);
    bool is_trophy_unlocked(uint32_t trophy_id);
    TrophyProgress* get_trophy_progress(uint32_t trophy_id);
    std::vector<TrophyDefinition> get_unlocked_trophies();
    std::vector<TrophyDefinition> get_locked_trophies();
    
    // Progress tracking
    void update_progress(uint32_t trophy_id, uint64_t progress);
    void increment_progress(uint32_t trophy_id, uint64_t amount = 1);
    void trigger_event(const std::string& event_name, uint64_t value = 1);
    void set_progress_counter(const std::string& counter_name, uint64_t value);
    void increment_counter(const std::string& counter_name, uint64_t amount = 1);
    
    // Trophy checking and auto-unlock
    void check_all_trophy_conditions();
    void check_trophy_condition(uint32_t trophy_id);
    
    // Statistics
    uint32_t get_trophy_count(TrophyType type = static_cast<TrophyType>(0));
    double get_completion_percentage();
    uint32_t get_trophy_points();
    uint32_t get_trophy_level();
    
    // Callbacks
    void register_unlock_callback(std::function<void(const TrophyDefinition&, const std::string&)> callback);
    
    // System integration
    void on_game_start();
    void on_game_end();
    void on_save_game();
    void on_load_game();
    
    // Import/Export
    bool export_trophy_data(const std::string& filepath);
    bool import_trophy_data(const std::string& filepath);
    
    // Network sync (for PSN integration)
    bool sync_with_psn();
    bool upload_trophy_data();
    bool download_trophy_data();
};

// Trophy system integration with PS5 BIOS
class TrophyBIOSInterface {
private:
    std::shared_ptr<TrophySystem> trophy_system_;
    
public:
    TrophyBIOSInterface(std::shared_ptr<TrophySystem> trophy_system);
    
    // System call handlers
    void handle_trophy_unlock(uint64_t* registers);
    void handle_trophy_progress(uint64_t* registers);
    void handle_trophy_query(uint64_t* registers);
    void handle_trophy_list(uint64_t* registers);
    void handle_trophy_icon(uint64_t* registers);
    
    // Game integration
    void notify_game_start(const std::string& title_id, const std::string& npcommid);
    void notify_game_event(const std::string& event_name, uint64_t value);
    void notify_progress_update(const std::string& progress_name, uint64_t value);
};

} // namespace PS5Emu
