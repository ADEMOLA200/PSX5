#pragma once

#include "../core/types.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace PS5Emu {

class SonyIOComplex {
public:
    struct IODevice {
        uint32_t device_id;
        uint32_t base_addr;
        uint32_t size;
        std::string name;
        bool is_secure;
    };

    struct DualSenseState {
        // Digital buttons
        bool square, triangle, circle, cross;
        bool l1, l2, r1, r2;
        bool share, options, ps, touchpad;
        bool dpad_up, dpad_down, dpad_left, dpad_right;
        
        // Analog inputs
        float left_stick_x, left_stick_y;
        float right_stick_x, right_stick_y;
        float l2_trigger, r2_trigger;
        
        // Advanced features
        struct {
            float x, y, z;
        } accelerometer, gyroscope;
        
        struct {
            bool active;
            float intensity;
            uint32_t frequency;
        } haptic_feedback;
        
        struct {
            float left_tension, right_tension;
        } adaptive_triggers;
        
        // Touchpad
        struct TouchPoint {
            bool active;
            float x, y;
        } touchpad_points[2];
        
        // Audio
        bool mic_muted;
        float speaker_volume;
    };

    // Tempest 3D AudioTech
    struct Tempest3D {
        struct AudioSource {
            float position[3];
            float velocity[3];
            float volume;
            uint32_t sample_rate;
            bool is_3d;
        };
        
        std::vector<AudioSource> sources;
        float listener_position[3];
        float listener_orientation[4]; // quaternion
        bool hrtf_enabled;
        uint32_t output_channels;
    };

    // SSD I/O Complex
    struct SSDController {
        struct IORequest {
            uint64_t lba;
            uint32_t sector_count;
            void* buffer;
            bool is_write;
            std::function<void(bool)> callback;
        };
        
        std::vector<IORequest> pending_requests;
        uint64_t total_capacity;
        uint32_t queue_depth;
        bool compression_enabled;
        bool decompression_unit_active;
    };

private:
    std::unordered_map<uint32_t, IODevice> devices;
    DualSenseState controller_state;
    Tempest3D audio_engine;
    SSDController ssd_controller;
    
public:
    SonyIOComplex();
    ~SonyIOComplex();
    
    // Device management
    void RegisterDevice(const IODevice& device);
    bool ReadDevice(uint32_t device_id, uint32_t offset, void* data, size_t size);
    bool WriteDevice(uint32_t device_id, uint32_t offset, const void* data, size_t size);
    
    // Controller interface
    void UpdateController(const DualSenseState& state);
    const DualSenseState& GetControllerState() const { return controller_state; }
    void SetHapticFeedback(float intensity, uint32_t frequency);
    void SetAdaptiveTriggers(float left_tension, float right_tension);
    
    // Audio processing
    void ProcessAudio(float* samples, size_t sample_count);
    void Add3DAudioSource(uint32_t source_id, const Tempest3D::AudioSource& source);
    void Remove3DAudioSource(uint32_t source_id);
    void SetListenerTransform(const float position[3], const float orientation[4]);
    
    // SSD operations
    void QueueSSDRead(uint64_t lba, uint32_t sectors, void* buffer, std::function<void(bool)> callback);
    void QueueSSDWrite(uint64_t lba, uint32_t sectors, const void* buffer, std::function<void(bool)> callback);
    void ProcessSSDQueue();
    
    // Security interface
    bool ValidateSecureAccess(uint32_t device_id, uint32_t access_level);
    void UpdateSecurityKeys();
};

// Security Processor
class SecurityProcessor {
public:
    enum class SecurityLevel {
        HYPERVISOR = 0,
        KERNEL = 1,
        USER = 2,
        GUEST = 3
    };
    
    struct SecureRegion {
        uint64_t base_addr;
        uint64_t size;
        SecurityLevel min_level;
        bool encrypted;
        uint32_t key_id;
    };
    
    struct CryptoContext {
        uint8_t aes_key[32];
        uint8_t iv[16];
        bool key_valid;
        uint32_t usage_count;
    };

private:
    std::vector<SecureRegion> secure_regions;
    std::unordered_map<uint32_t, CryptoContext> crypto_contexts;
    SecurityLevel current_level;
    bool secure_boot_verified;
    
public:
    SecurityProcessor();
    
    // Access control
    bool CheckAccess(uint64_t addr, size_t size, SecurityLevel required_level);
    void SetSecurityLevel(SecurityLevel level);
    SecurityLevel GetSecurityLevel() const { return current_level; }
    
    // Secure regions
    void AddSecureRegion(const SecureRegion& region);
    void RemoveSecureRegion(uint64_t base_addr);
    
    // Cryptography
    uint32_t CreateCryptoContext(const uint8_t* key, const uint8_t* iv);
    bool EncryptData(uint32_t context_id, const void* input, void* output, size_t size);
    bool DecryptData(uint32_t context_id, const void* input, void* output, size_t size);
    void DestroyCryptoContext(uint32_t context_id);
    
    // Secure boot
    bool VerifySecureBoot(const void* bootloader, size_t size);
    bool IsSecureBootVerified() const { return secure_boot_verified; }
};

// Network Stack
class NetworkStack {
public:
    struct NetworkInterface {
        std::string name;
        uint8_t mac_address[6];
        uint32_t ip_address;
        uint32_t subnet_mask;
        uint32_t gateway;
        bool is_up;
        uint64_t bytes_sent;
        uint64_t bytes_received;
    };
    
    struct Socket {
        int fd;
        int type; // SOCK_STREAM, SOCK_DGRAM
        int protocol;
        bool is_bound;
        uint32_t local_addr;
        uint16_t local_port;
        uint32_t remote_addr;
        uint16_t remote_port;
    };

private:
    std::vector<NetworkInterface> interfaces;
    std::unordered_map<int, Socket> sockets;
    int next_socket_fd;
    
public:
    NetworkStack();
    
    // Interface management
    void AddInterface(const NetworkInterface& iface);
    void SetInterfaceState(const std::string& name, bool up);
    const std::vector<NetworkInterface>& GetInterfaces() const { return interfaces; }
    
    // Socket operations
    int CreateSocket(int domain, int type, int protocol);
    bool BindSocket(int fd, uint32_t addr, uint16_t port);
    bool ConnectSocket(int fd, uint32_t addr, uint16_t port);
    int SendData(int fd, const void* data, size_t size);
    int ReceiveData(int fd, void* buffer, size_t size);
    void CloseSocket(int fd);
};

} // namespace PS5Emu
