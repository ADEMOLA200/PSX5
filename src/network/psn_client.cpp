#include "psn_client.h"
#include "../core/logger.h"
#include <curl/curl.h>
#include <json/json.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <base64.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace PS5Emu {

// Callback for libcurl to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

PSNClient::PSNClient(PSNRegion region) 
    : region_(region), is_connected_(false), is_authenticated_(false) {
    
    // Set API endpoints based on region
    switch (region) {
        case PSNRegion::NORTH_AMERICA:
            api_base_url_ = "https://us-prof.np.community.playstation.net";
            auth_base_url_ = "https://auth.api.sonyentertainmentnetwork.com";
            store_base_url_ = "https://store.playstation.com/store/api";
            break;
        case PSNRegion::EUROPE:
            api_base_url_ = "https://eu-prof.np.community.playstation.net";
            auth_base_url_ = "https://auth.api.sonyentertainmentnetwork.com";
            store_base_url_ = "https://store.playstation.com/store/api";
            break;
        case PSNRegion::ASIA:
            api_base_url_ = "https://asia-prof.np.community.playstation.net";
            auth_base_url_ = "https://auth.api.sonyentertainmentnetwork.com";
            store_base_url_ = "https://store.playstation.com/store/api";
            break;
        case PSNRegion::JAPAN:
            api_base_url_ = "https://jp-prof.np.community.playstation.net";
            auth_base_url_ = "https://auth.api.sonyentertainmentnetwork.com";
            store_base_url_ = "https://store.playstation.com/store/api";
            break;
        case PSNRegion::OCEANIA:
            api_base_url_ = "https://oc-prof.np.community.playstation.net";
            auth_base_url_ = "https://auth.api.sonyentertainmentnetwork.com";
            store_base_url_ = "https://store.playstation.com/store/api";
            break;
    }
    
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    log::info("PSN Client initialized for region: " + std::to_string(static_cast<int>(region)));
}

PSNClient::~PSNClient() {
    disconnect();
    curl_global_cleanup();
}

bool PSNClient::authenticate(const std::string& username, const std::string& password) {
    // Step 1: Get authentication challenge
    std::string challenge_url = auth_base_url_ + "/2.0/oauth/authorize";
    std::string challenge_data = "client_id=b7cbf451-6bb6-4a5a-8913-71e61f462787&"
                                "response_type=code&"
                                "scope=psn:mobile.v1 psn:clientapp&"
                                "redirect_uri=com.playstation.PlayStationApp://redirect";
    
    std::string challenge_response = make_http_request(challenge_url, "POST", challenge_data);
    if (challenge_response.empty()) {
        log::error("Failed to get authentication challenge");
        return false;
    }
    
    // Step 2: Submit credentials
    std::string login_url = auth_base_url_ + "/2.0/oauth/token";
    std::string login_data = "grant_type=password&"
                            "client_id=b7cbf451-6bb6-4a5a-8913-71e61f462787&"
                            "client_secret=zsISsjmCx85zgzqG&"
                            "username=" + username + "&"
                            "password=" + password + "&"
                            "scope=psn:mobile.v1 psn:clientapp";
    
    std::unordered_map<std::string, std::string> headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    headers["User-Agent"] = "PlayStation/21090100 CFNetwork/1240.0.4 Darwin/20.6.0";
    
    std::string auth_response = make_http_request(login_url, "POST", login_data, headers);
    if (auth_response.empty()) {
        log::error("Authentication failed - invalid credentials");
        return false;
    }
    
    // Parse authentication response
    Json::Value auth_json;
    Json::Reader reader;
    if (!reader.parse(auth_response, auth_json)) {
        log::error("Failed to parse authentication response");
        return false;
    }
    
    if (auth_json.isMember("error")) {
        log::error("Authentication error: " + auth_json["error_description"].asString());
        return false;
    }
    
    auth_token_ = auth_json["access_token"].asString();
    refresh_token_ = auth_json["refresh_token"].asString();
    
    // Calculate token expiry
    int expires_in = auth_json["expires_in"].asInt();
    token_expiry_ = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    
    // Step 3: Get user profile
    std::string profile_url = api_base_url_ + "/userProfile/v1/users/me/profile2";
    headers.clear();
    headers["Authorization"] = "Bearer " + auth_token_;
    headers["User-Agent"] = "PlayStation/21090100 CFNetwork/1240.0.4 Darwin/20.6.0";
    
    std::string profile_response = make_http_request(profile_url, "GET", "", headers);
    if (profile_response.empty()) {
        log::error("Failed to get user profile");
        return false;
    }
    
    Json::Value profile_json;
    if (!reader.parse(profile_response, profile_json)) {
        log::error("Failed to parse user profile");
        return false;
    }
    
    // Populate user data
    current_user_.user_id = profile_json["profile"]["accountId"].asString();
    current_user_.online_id = profile_json["profile"]["onlineId"].asString();
    current_user_.display_name = profile_json["profile"]["personalDetail"]["firstName"].asString() + 
                                 " " + profile_json["profile"]["personalDetail"]["lastName"].asString();
    current_user_.avatar_url = profile_json["profile"]["avatarUrls"][0]["avatarUrl"].asString();
    current_user_.status = PSNUserStatus::ONLINE;
    current_user_.trophy_level = profile_json["profile"]["trophySummary"]["level"].asUInt();
    current_user_.trophy_points = profile_json["profile"]["trophySummary"]["earnedTrophies"]["platinum"].asUInt64() * 300 +
                                  profile_json["profile"]["trophySummary"]["earnedTrophies"]["gold"].asUInt64() * 90 +
                                  profile_json["profile"]["trophySummary"]["earnedTrophies"]["silver"].asUInt64() * 30 +
                                  profile_json["profile"]["trophySummary"]["earnedTrophies"]["bronze"].asUInt64() * 15;
    current_user_.is_plus_member = profile_json["profile"]["plus"].asBool();
    current_user_.region = region_;
    
    is_authenticated_ = true;
    
    log::info("Successfully authenticated as: " + current_user_.online_id);
    return true;
}

bool PSNClient::connect() {
    if (!is_authenticated_) {
        log::error("Cannot connect - not authenticated");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(connection_mutex_);
    
    if (is_connected_) {
        return true;
    }
    
    // Test connection with a simple API call
    std::string test_url = api_base_url_ + "/userProfile/v1/users/me/profile2";
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    
    std::string response = make_http_request(test_url, "GET", "", headers);
    if (response.empty()) {
        log::error("Failed to connect to PSN");
        return false;
    }
    
    is_connected_ = true;
    
    // Start heartbeat thread
    heartbeat_thread_ = std::thread(&PSNClient::heartbeat_loop, this);
    
    log::info("Connected to PlayStation Network");
    return true;
}

void PSNClient::disconnect() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    
    if (!is_connected_) {
        return;
    }
    
    is_connected_ = false;
    
    // Stop heartbeat thread
    if (heartbeat_thread_.joinable()) {
        connection_cv_.notify_all();
        heartbeat_thread_.join();
    }
    
    log::info("Disconnected from PlayStation Network");
}

std::string PSNClient::make_http_request(const std::string& url, const std::string& method, 
                                        const std::string& data,
                                        const std::unordered_map<std::string, std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }
    
    std::string response_data;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Set method
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!data.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        }
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (!data.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Set headers
    struct curl_slist* header_list = nullptr;
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Check response code
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    // Cleanup
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || response_code >= 400) {
        log::error("HTTP request failed: " + std::string(curl_easy_strerror(res)) + 
                  " (Code: " + std::to_string(response_code) + ")");
        return "";
    }
    
    return response_data;
}

void PSNClient::heartbeat_loop() {
    while (is_connected_) {
        std::unique_lock<std::mutex> lock(connection_mutex_);
        
        // Wait for 30 seconds or until disconnection
        if (connection_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !is_connected_; })) {
            break; // Disconnected
        }
        
        // Refresh token if needed
        if (std::chrono::system_clock::now() >= token_expiry_ - std::chrono::minutes(5)) {
            if (!refresh_auth_token()) {
                log::error("Failed to refresh auth token - disconnecting");
                is_connected_ = false;
                break;
            }
        }
        
        // Process notifications
        process_notifications();
    }
}

bool PSNClient::refresh_auth_token() {
    std::string refresh_url = auth_base_url_ + "/2.0/oauth/token";
    std::string refresh_data = "grant_type=refresh_token&"
                              "client_id=b7cbf451-6bb6-4a5a-8913-71e61f462787&"
                              "client_secret=zsISsjmCx85zgzqG&"
                              "refresh_token=" + refresh_token_;
    
    std::unordered_map<std::string, std::string> headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    std::string response = make_http_request(refresh_url, "POST", refresh_data, headers);
    if (response.empty()) {
        return false;
    }
    
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(response, json) || json.isMember("error")) {
        return false;
    }
    
    auth_token_ = json["access_token"].asString();
    if (json.isMember("refresh_token")) {
        refresh_token_ = json["refresh_token"].asString();
    }
    
    int expires_in = json["expires_in"].asInt();
    token_expiry_ = std::chrono::system_clock::now() + std::chrono::seconds(expires_in);
    
    return true;
}

void PSNClient::process_notifications() {
    // Get notifications from PSN
    std::string notifications_url = api_base_url_ + "/notification/v1/users/me/notifications";
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    
    std::string response = make_http_request(notifications_url, "GET", "", headers);
    if (response.empty()) {
        return;
    }
    
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(response, json)) {
        return;
    }
    
    // Process each notification
    const Json::Value& notifications = json["notifications"];
    for (const auto& notification : notifications) {
        std::string type = notification["type"].asString();
        
        if (type == "FRIEND_ONLINE") {
            std::string friend_id = notification["sourceUser"]["accountId"].asString();
            auto it = friends_.find(friend_id);
            if (it != friends_.end()) {
                it->second.user.status = PSNUserStatus::ONLINE;
                
                for (const auto& callback : friend_online_callbacks_) {
                    callback(it->second);
                }
            }
        } else if (type == "MESSAGE_RECEIVED") {
            PSNMessage message;
            message.message_id = notification["messageId"].asString();
            message.sender_id = notification["sourceUser"]["accountId"].asString();
            message.recipient_id = current_user_.user_id;
            message.content = notification["message"]["body"].asString();
            message.timestamp = std::chrono::system_clock::now();
            message.is_read = false;
            
            std::lock_guard<std::mutex> lock(social_mutex_);
            messages_.push_back(message);
            
            for (const auto& callback : message_callbacks_) {
                callback(message);
            }
        } else if (type == "SESSION_INVITE") {
            // Game session invite
            PSNGameSession session;
            session.session_id = notification["sessionId"].asString();
            session.game_title_id = notification["titleId"].asString();
            session.host_user_id = notification["sourceUser"]["accountId"].asString();
            
            for (const auto& callback : session_invite_callbacks_) {
                callback(session);
            }
        }
    }
}

std::vector<PSNFriend> PSNClient::get_friends_list() {
    if (!is_authenticated_ || !is_connected_) {
        return {};
    }
    
    std::string friends_url = api_base_url_ + "/userProfile/v1/users/me/friends/profiles2";
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    
    std::string response = make_http_request(friends_url, "GET", "", headers);
    if (response.empty()) {
        return {};
    }
    
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(response, json)) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(social_mutex_);
    friends_.clear();
    
    const Json::Value& profiles = json["profiles"];
    for (const auto& profile : profiles) {
        PSNFriend friend_data;
        friend_data.user.user_id = profile["accountId"].asString();
        friend_data.user.online_id = profile["onlineId"].asString();
        friend_data.user.display_name = profile["personalDetail"]["firstName"].asString() + 
                                       " " + profile["personalDetail"]["lastName"].asString();
        friend_data.user.avatar_url = profile["avatarUrls"][0]["avatarUrl"].asString();
        
        // Parse online status
        std::string presence = profile["presence"]["primaryPlatformInfo"]["onlineStatus"].asString();
        if (presence == "online") {
            friend_data.user.status = PSNUserStatus::ONLINE;
        } else if (presence == "away") {
            friend_data.user.status = PSNUserStatus::AWAY;
        } else {
            friend_data.user.status = PSNUserStatus::OFFLINE;
        }
        
        friend_data.user.trophy_level = profile["trophySummary"]["level"].asUInt();
        friend_data.user.is_plus_member = profile["plus"].asBool();
        friend_data.is_favorite = profile["relation"]["isFavorite"].asBool();
        friend_data.can_message = profile["relation"]["canMessage"].asBool();
        friend_data.can_invite = profile["relation"]["canInvite"].asBool();
        
        friends_[friend_data.user.user_id] = friend_data;
    }
    
    std::vector<PSNFriend> result;
    for (const auto& [id, friend_data] : friends_) {
        result.push_back(friend_data);
    }
    
    return result;
}

bool PSNClient::send_message(const std::string& recipient_id, const std::string& content) {
    if (!is_authenticated_ || !is_connected_) {
        return false;
    }
    
    std::string message_url = api_base_url_ + "/messaging/v1/users/me/conversations/" + recipient_id + "/messages";
    
    Json::Value message_json;
    message_json["messageKind"] = 1; // Text message
    message_json["body"] = content;
    
    Json::StreamWriterBuilder builder;
    std::string message_data = Json::writeString(builder, message_json);
    
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    headers["Content-Type"] = "application/json";
    
    std::string response = make_http_request(message_url, "POST", message_data, headers);
    
    return !response.empty();
}

std::string PSNClient::create_game_session(const std::string& game_title_id, uint32_t max_players, bool is_private) {
    if (!is_authenticated_ || !is_connected_) {
        return "";
    }
    
    std::string session_url = api_base_url_ + "/gaming/v1/users/me/sessions";
    
    Json::Value session_json;
    session_json["titleId"] = game_title_id;
    session_json["maxPlayers"] = max_players;
    session_json["isPrivate"] = is_private;
    session_json["sessionType"] = "GAME_SESSION";
    
    Json::StreamWriterBuilder builder;
    std::string session_data = Json::writeString(builder, session_json);
    
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    headers["Content-Type"] = "application/json";
    
    std::string response = make_http_request(session_url, "POST", session_data, headers);
    if (response.empty()) {
        return "";
    }
    
    Json::Value response_json;
    Json::Reader reader;
    if (!reader.parse(response, response_json)) {
        return "";
    }
    
    std::string session_id = response_json["sessionId"].asString();
    
    // Store session locally
    std::lock_guard<std::mutex> lock(session_mutex_);
    PSNGameSession session;
    session.session_id = session_id;
    session.game_title_id = game_title_id;
    session.host_user_id = current_user_.user_id;
    session.max_players = max_players;
    session.is_private = is_private;
    session.created_time = std::chrono::system_clock::now();
    session.participants.push_back(current_user_.user_id);
    
    active_sessions_[session_id] = session;
    current_session_id_ = session_id;
    
    log::info("Created game session: " + session_id);
    return session_id;
}

bool PSNClient::sync_trophies(const std::string& title_id) {
    if (!is_authenticated_ || !is_connected_) {
        return false;
    }
    
    std::string sync_url = api_base_url_ + "/trophy/v1/users/me/npCommunicationIds/" + title_id + "/trophyGroups/all/trophies";
    
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + auth_token_;
    
    std::string response = make_http_request(sync_url, "GET", "", headers);
    
    return !response.empty();
}

PSNBIOSInterface::PSNBIOSInterface(std::shared_ptr<PSNClient> psn_client)
    : psn_client_(psn_client) {
}

void PSNBIOSInterface::handle_psn_authenticate(uint64_t* registers) {
    // registers[0] = username pointer
    // registers[1] = password pointer
    // registers[2] = result pointer
    
    char* username = reinterpret_cast<char*>(registers[0]);
    char* password = reinterpret_cast<char*>(registers[1]);
    
    bool success = psn_client_->authenticate(std::string(username), std::string(password));
    if (success) {
        success = psn_client_->connect();
    }
    
    registers[0] = success ? 0 : -1;
}

void PSNBIOSInterface::handle_psn_sync_trophies(uint64_t* registers) {
    // registers[0] = title_id pointer
    
    char* title_id = reinterpret_cast<char*>(registers[0]);
    bool success = psn_client_->sync_trophies(std::string(title_id));
    
    registers[0] = success ? 0 : -1;
}

void PSNBIOSInterface::notify_game_start(const std::string& title_id) {
    if (psn_client_->is_connected()) {
        // Update rich presence
        PSNClient::GameStats stats;
        stats.title_id = title_id;
        stats.sessions_played = 1;
        stats.last_played = std::chrono::system_clock::now();
        
        psn_client_->update_game_stats(stats);
        
        log::info("Notified PSN of game start: " + title_id);
    }
}

} // namespace PS5Emu
