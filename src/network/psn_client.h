#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace PS5Emu {

enum class PSNRegion {
    NORTH_AMERICA,
    EUROPE,
    ASIA,
    JAPAN,
    OCEANIA
};

enum class PSNUserStatus {
    OFFLINE,
    ONLINE,
    AWAY,
    BUSY,
    INVISIBLE
};

struct PSNUser {
    std::string user_id;
    std::string online_id;
    std::string display_name;
    std::string avatar_url;
    PSNUserStatus status;
    std::string status_message;
    uint32_t trophy_level;
    uint64_t trophy_points;
    bool is_plus_member;
    std::chrono::system_clock::time_point last_seen;
    PSNRegion region;
};

struct PSNFriend {
    PSNUser user;
    bool is_favorite;
    bool can_message;
    bool can_invite;
    std::chrono::system_clock::time_point friendship_date;
};

struct PSNMessage {
    std::string message_id;
    std::string sender_id;
    std::string recipient_id;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    bool is_read;
    std::vector<std::string> attachments;
};

struct PSNGameSession {
    std::string session_id;
    std::string game_title_id;
    std::string host_user_id;
    std::vector<std::string> participants;
    uint32_t max_players;
    bool is_private;
    std::string join_code;
    std::unordered_map<std::string, std::string> session_data;
    std::chrono::system_clock::time_point created_time;
};

struct PSNStoreItem {
    std::string content_id;
    std::string title;
    std::string description;
    std::string publisher;
    std::string category;
    double price;
    std::string currency;
    std::vector<std::string> screenshots;
    std::string trailer_url;
    uint64_t file_size;
    bool is_owned;
    bool is_downloadable;
};

class PSNClient {
private:
    std::string auth_token_;
    std::string refresh_token_;
    std::chrono::system_clock::time_point token_expiry_;
    PSNUser current_user_;
    PSNRegion region_;
    
    // Network configuration
    std::string api_base_url_;
    std::string auth_base_url_;
    std::string store_base_url_;
    
    // Connection state
    bool is_connected_;
    bool is_authenticated_;
    std::thread heartbeat_thread_;
    std::mutex connection_mutex_;
    std::condition_variable connection_cv_;
    
    // Friends and social
    std::unordered_map<std::string, PSNFriend> friends_;
    std::vector<PSNMessage> messages_;
    std::mutex social_mutex_;
    
    // Game sessions
    std::unordered_map<std::string, PSNGameSession> active_sessions_;
    std::string current_session_id_;
    std::mutex session_mutex_;
    
    // Store and content
    std::unordered_map<std::string, PSNStoreItem> owned_content_;
    std::vector<PSNStoreItem> store_cache_;
    std::mutex store_mutex_;
    
    // Callbacks
    std::vector<std::function<void(const PSNFriend&)>> friend_online_callbacks_;
    std::vector<std::function<void(const PSNMessage&)>> message_callbacks_;
    std::vector<std::function<void(const PSNGameSession&)>> session_invite_callbacks_;
    
    // Internal methods
    bool refresh_auth_token();
    std::string make_http_request(const std::string& url, const std::string& method, 
                                 const std::string& data = "", 
                                 const std::unordered_map<std::string, std::string>& headers = {});
    void heartbeat_loop();
    void process_notifications();
    bool validate_auth_token();
    
public:
    PSNClient(PSNRegion region = PSNRegion::NORTH_AMERICA);
    ~PSNClient();
    
    // Authentication
    bool authenticate(const std::string& username, const std::string& password);
    bool authenticate_with_token(const std::string& auth_token, const std::string& refresh_token);
    void logout();
    bool is_authenticated() const { return is_authenticated_; }
    const PSNUser& get_current_user() const { return current_user_; }
    
    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const { return is_connected_; }
    
    // User profile
    bool update_profile(const std::string& display_name, const std::string& status_message);
    bool set_status(PSNUserStatus status);
    bool upload_avatar(const std::vector<uint8_t>& image_data);
    PSNUser get_user_profile(const std::string& user_id);
    
    // Friends management
    std::vector<PSNFriend> get_friends_list();
    bool send_friend_request(const std::string& user_id);
    bool accept_friend_request(const std::string& user_id);
    bool remove_friend(const std::string& user_id);
    bool block_user(const std::string& user_id);
    std::vector<PSNUser> search_users(const std::string& query);
    
    // Messaging
    bool send_message(const std::string& recipient_id, const std::string& content);
    std::vector<PSNMessage> get_messages(const std::string& user_id, uint32_t limit = 50);
    bool mark_message_read(const std::string& message_id);
    bool delete_message(const std::string& message_id);
    
    // Game sessions and multiplayer
    std::string create_game_session(const std::string& game_title_id, uint32_t max_players, bool is_private);
    bool join_game_session(const std::string& session_id, const std::string& join_code = "");
    bool leave_game_session();
    bool invite_to_session(const std::string& user_id, const std::string& session_id);
    std::vector<PSNGameSession> get_joinable_sessions(const std::string& game_title_id);
    bool update_session_data(const std::string& key, const std::string& value);
    
    // Store integration
    std::vector<PSNStoreItem> browse_store(const std::string& category = "", uint32_t limit = 20);
    PSNStoreItem get_store_item(const std::string& content_id);
    bool purchase_content(const std::string& content_id);
    std::vector<PSNStoreItem> get_owned_content();
    bool download_content(const std::string& content_id, const std::string& download_path);
    
    // Trophy synchronization
    bool sync_trophies(const std::string& title_id);
    bool upload_trophy_data(const std::string& title_id, const std::vector<uint8_t>& trophy_data);
    std::vector<uint8_t> download_trophy_data(const std::string& title_id);
    
    // Cloud saves
    bool upload_save_data(const std::string& title_id, const std::string& save_name, 
                         const std::vector<uint8_t>& save_data);
    std::vector<uint8_t> download_save_data(const std::string& title_id, const std::string& save_name);
    std::vector<std::string> list_cloud_saves(const std::string& title_id);
    bool delete_cloud_save(const std::string& title_id, const std::string& save_name);
    
    // PlayStation Plus
    bool is_plus_member() const { return current_user_.is_plus_member; }
    std::vector<PSNStoreItem> get_plus_games();
    bool claim_plus_game(const std::string& content_id);
    
    // Events and callbacks
    void register_friend_online_callback(std::function<void(const PSNFriend&)> callback);
    void register_message_callback(std::function<void(const PSNMessage&)> callback);
    void register_session_invite_callback(std::function<void(const PSNGameSession&)> callback);
    
    // Statistics and achievements
    struct GameStats {
        std::string title_id;
        uint64_t play_time_seconds;
        uint32_t sessions_played;
        std::chrono::system_clock::time_point last_played;
        std::unordered_map<std::string, uint64_t> custom_stats;
    };
    
    bool update_game_stats(const GameStats& stats);
    GameStats get_game_stats(const std::string& title_id);
    std::vector<GameStats> get_all_game_stats();
    
    // Parental controls and safety
    struct ParentalSettings {
        bool chat_restricted;
        bool friend_requests_restricted;
        bool store_purchases_restricted;
        uint32_t max_play_time_minutes;
        std::vector<std::string> blocked_content_ratings;
    };
    
    ParentalSettings get_parental_settings();
    bool update_parental_settings(const ParentalSettings& settings);
    
    // Network testing
    struct NetworkTest {
        uint32_t download_speed_mbps;
        uint32_t upload_speed_mbps;
        uint32_t ping_ms;
        bool nat_type_open;
        std::string connection_type;
    };
    
    NetworkTest run_network_test();
};

// PSN Integration with PS5 BIOS
class PSNBIOSInterface {
private:
    std::shared_ptr<PSNClient> psn_client_;
    
public:
    PSNBIOSInterface(std::shared_ptr<PSNClient> psn_client);
    
    // System call handlers
    void handle_psn_authenticate(uint64_t* registers);
    void handle_psn_get_friends(uint64_t* registers);
    void handle_psn_send_message(uint64_t* registers);
    void handle_psn_create_session(uint64_t* registers);
    void handle_psn_join_session(uint64_t* registers);
    void handle_psn_sync_trophies(uint64_t* registers);
    void handle_psn_upload_save(uint64_t* registers);
    void handle_psn_download_save(uint64_t* registers);
    
    // Game integration
    void notify_game_start(const std::string& title_id);
    void notify_game_end(const std::string& title_id, uint64_t play_time_seconds);
    void update_rich_presence(const std::string& status);
};

} // namespace PS5Emu
