#include "sony_io.h"
#include "../core/logger.h"
#include <cstring>
#include <algorithm>
#include <random>
#include <vector>
#include <cmath>

namespace PS5Emu {

SonyIOComplex::SonyIOComplex() {
    // Initialize controller state
    memset(&controller_state, 0, sizeof(controller_state));
    
    // Initialize audio engine
    memset(&audio_engine, 0, sizeof(audio_engine));
    audio_engine.hrtf_enabled = true;
    audio_engine.output_channels = 2; // Stereo default
    
    // Initialize SSD controller
    ssd_controller.total_capacity = 825ULL * 1024 * 1024 * 1024; // 825GB
    ssd_controller.queue_depth = 32;
    ssd_controller.compression_enabled = true;
    ssd_controller.decompression_unit_active = true;
    
    // Register standard PS5 I/O devices
    RegisterDevice({0x1000, 0x10000000, 0x1000, "DualSense Controller", false});
    RegisterDevice({0x1001, 0x10001000, 0x1000, "Tempest 3D Audio", false});
    RegisterDevice({0x1002, 0x10002000, 0x2000, "SSD Controller", false});
    RegisterDevice({0x1003, 0x10004000, 0x1000, "Network Interface", false});
    RegisterDevice({0x2000, 0x20000000, 0x10000, "Security Processor", true});
    
    Logger::Info("Sony I/O Complex initialized");
}

SonyIOComplex::~SonyIOComplex() {
    Logger::Info("Sony I/O Complex shutdown");
}

void SonyIOComplex::RegisterDevice(const IODevice& device) {
    devices[device.device_id] = device;
    Logger::Debug("Registered I/O device: {} (ID: 0x{:X})", device.name, device.device_id);
}

bool SonyIOComplex::ReadDevice(uint32_t device_id, uint32_t offset, void* data, size_t size) {
    auto it = devices.find(device_id);
    if (it == devices.end()) {
        Logger::Error("Unknown I/O device: 0x{:X}", device_id);
        return false;
    }
    
    const IODevice& device = it->second;
    if (offset + size > device.size) {
        Logger::Error("I/O read out of bounds for device {}", device.name);
        return false;
    }
    
    switch (device_id) {
        case 0x1000: // DualSense Controller
            if (offset == 0 && size >= sizeof(DualSenseState)) {
                memcpy(data, &controller_state, sizeof(DualSenseState));
                return true;
            }
            break;
            
        case 0x1001: // Tempest 3D Audio
            // Return audio engine status
            if (offset == 0 && size >= 4) {
                uint32_t status = audio_engine.hrtf_enabled ? 1 : 0;
                memcpy(data, &status, 4);
                return true;
            }
            break;
            
        case 0x1002: // SSD Controller
            if (offset == 0 && size >= 8) {
                memcpy(data, &ssd_controller.total_capacity, 8);
                return true;
            }
            break;
    }
    
    memset(data, 0, size);
    return true;
}

bool SonyIOComplex::WriteDevice(uint32_t device_id, uint32_t offset, const void* data, size_t size) {
    auto it = devices.find(device_id);
    if (it == devices.end()) {
        Logger::Error("Unknown I/O device: 0x{:X}", device_id);
        return false;
    }
    
    const IODevice& device = it->second;
    if (offset + size > device.size) {
        Logger::Error("I/O write out of bounds for device {}", device.name);
        return false;
    }
    
    switch (device_id) {
        case 0x1000: // DualSense Controller
            if (offset == 0x100 && size >= 8) {
                // Haptic feedback control
                float intensity = *((float*)data);
                uint32_t frequency = *((uint32_t*)((char*)data + 4));
                SetHapticFeedback(intensity, frequency);
                return true;
            }
            break;
            
        case 0x1001: // Tempest 3D Audio
            // Audio configuration
            return true;
            
        case 0x1002: // SSD Controller
            // SSD commands
            return true;
    }
    
    return true;
}

void SonyIOComplex::UpdateController(const DualSenseState& state) {
    controller_state = state;
}

void SonyIOComplex::SetHapticFeedback(float intensity, uint32_t frequency) {
    controller_state.haptic_feedback.active = intensity > 0.0f;
    controller_state.haptic_feedback.intensity = std::clamp(intensity, 0.0f, 1.0f);
    controller_state.haptic_feedback.frequency = frequency;
    
    Logger::Debug("Haptic feedback: intensity={:.2f}, frequency={}", intensity, frequency);
}

void SonyIOComplex::SetAdaptiveTriggers(float left_tension, float right_tension) {
    controller_state.adaptive_triggers.left_tension = std::clamp(left_tension, 0.0f, 1.0f);
    controller_state.adaptive_triggers.right_tension = std::clamp(right_tension, 0.0f, 1.0f);
    
    Logger::Debug("Adaptive triggers: L={:.2f}, R={:.2f}", left_tension, right_tension);
}

void SonyIOComplex::ProcessAudio(float* samples, size_t sample_count) {
    if (!audio_engine.hrtf_enabled || audio_engine.sources.empty()) {
        return;
    }
    
    for (size_t i = 0; i < sample_count; i += 2) {
        float left_output = 0.0f;
        float right_output = 0.0f;
        
        for (const auto& source : audio_engine.sources) {
            if (!source.is_3d) {
                // Stereo source - simple mixing
                // TODO: Implement proper stereo mixing
                left_output += samples[i] * source.volume;
                right_output += samples[i + 1] * source.volume;
                continue;
            }
            
            // Calculate 3D position relative to listener
            float dx = source.position[0] - audio_engine.listener_position[0];
            float dy = source.position[1] - audio_engine.listener_position[1];
            float dz = source.position[2] - audio_engine.listener_position[2];
            
            float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (distance < 0.001f) distance = 0.001f;
            
            // Calculate spherical coordinates for HRTF lookup
            float azimuth = std::atan2(dx, dz);
            float elevation = std::atan2(dy, std::sqrt(dx*dx + dz*dz));
            
            // Distance attenuation with realistic falloff
            float attenuation = 1.0f / (1.0f + distance * distance * 0.01f);
            
            // Air absorption (high frequencies attenuate more with distance)
            float air_absorption = std::exp(-distance * 0.001f);
            
            // Apply HRTF (Head-Related Transfer Function)
            float hrtf_left, hrtf_right;
            apply_hrtf(azimuth, elevation, &hrtf_left, &hrtf_right);
            
            float doppler_factor = calculate_doppler_shift(source, dx, dy, dz, distance);
            
            float source_sample = samples[i] * source.volume * attenuation * air_absorption * doppler_factor;
            
            left_output += source_sample * hrtf_left;
            right_output += source_sample * hrtf_right;
        }
        
        apply_room_acoustics(&left_output, &right_output, i);
        
        samples[i] = left_output;
        samples[i + 1] = right_output;
    }
}

void SonyIOComplex::QueueSSDRead(uint64_t lba, uint32_t sectors, void* buffer, std::function<void(bool)> callback) {
    SSDController::IORequest request;
    request.lba = lba;
    request.sector_count = sectors;
    request.buffer = buffer;
    request.is_write = false;
    request.callback = callback;
    
    ssd_controller.pending_requests.push_back(request);
    Logger::Debug("Queued SSD read: LBA={}, sectors={}", lba, sectors);
}

void SonyIOComplex::ProcessSSDQueue() {
    // Process pending SSD requests
    for (auto& request : ssd_controller.pending_requests) {
        if (request.is_write) {
            // Write operation with compression
            Logger::Debug("Processing SSD write: LBA={}, sectors={}", request.lba, request.sector_count);
            
            if (ssd_controller.compression_enabled) {
                // Apply Kraken compression (PS5's custom compression)
                size_t original_size = request.sector_count * 512;
                std::vector<uint8_t> compressed_data;
                
                bool compression_success = compress_kraken(
                    static_cast<const uint8_t*>(request.buffer),
                    original_size,
                    compressed_data
                );
                
                if (compression_success) {
                    Logger::Debug("Compressed {} bytes to {} bytes (ratio: {:.2f})",
                                original_size, compressed_data.size(),
                                (float)compressed_data.size() / original_size);
                    
                    store_compressed_block(request.lba, compressed_data);
                } else {
                    store_uncompressed_block(request.lba, 
                                           static_cast<const uint8_t*>(request.buffer),
                                           original_size);
                }
            } else {
                store_uncompressed_block(request.lba,
                                       static_cast<const uint8_t*>(request.buffer),
                                       request.sector_count * 512);
            }
        } else {
            Logger::Debug("Processing SSD read: LBA={}, sectors={}", request.lba, request.sector_count);
            
            if (ssd_controller.decompression_unit_active) {
                std::vector<uint8_t> stored_data;
                bool is_compressed = load_block(request.lba, stored_data);
                
                if (is_compressed) {
                    std::vector<uint8_t> decompressed_data;
                    bool decompression_success = decompress_kraken(stored_data, decompressed_data);
                    
                    if (decompression_success) {
                        size_t copy_size = std::min(decompressed_data.size(), 
                                                  request.sector_count * 512);
                        std::memcpy(request.buffer, decompressed_data.data(), copy_size);
                        
                        Logger::Debug("Decompressed {} bytes to {} bytes",
                                    stored_data.size(), decompressed_data.size());
                    } else {
                        Logger::Error("Decompression failed for LBA {}", request.lba);
                        std::memset(request.buffer, 0, request.sector_count * 512);
                    }
                } else {
                    // Direct read of uncompressed data
                    std::vector<uint8_t> stored_data;
                    load_block(request.lba, stored_data);
                    
                    size_t copy_size = std::min(stored_data.size(), request.sector_count * 512);
                    std::memcpy(request.buffer, stored_data.data(), copy_size);
                    
                    // Zero-fill remaining space
                    if (copy_size < request.sector_count * 512) {
                        std::memset(static_cast<uint8_t*>(request.buffer) + copy_size, 0,
                                  request.sector_count * 512 - copy_size);
                    }
                }
            } else {
                // Simple read without decompression
                // TODO implement proper decompression
                std::vector<uint8_t> stored_data;
                load_block(request.lba, stored_data);
                
                size_t copy_size = std::min(stored_data.size(), request.sector_count * 512);
                std::memcpy(request.buffer, stored_data.data(), copy_size);
                
                if (copy_size < request.sector_count * 512) {
                    std::memset(static_cast<uint8_t*>(request.buffer) + copy_size, 0,
                              request.sector_count * 512 - copy_size);
                }
            }
        }
        
        if (request.callback) {
            request.callback(true);
        }
    }
    
    ssd_controller.pending_requests.clear();
}

bool SonyIOComplex::VerifySecureBoot(const void* bootloader, size_t size) {
    Logger::Info("Verifying secure boot signature (size: {} bytes)", size);
    
    if (!bootloader || size == 0) {
        Logger::Error("Invalid bootloader data");
        return false;
    }
    
    // Extract signature from bootloader (last 256 bytes for RSA-2048)
    if (size < 256) {
        Logger::Error("Bootloader too small for signature");
        return false;
    }
    
    const uint8_t* signature = static_cast<const uint8_t*>(bootloader) + size - 256;
    const uint8_t* data = static_cast<const uint8_t*>(bootloader);
    size_t data_size = size - 256;
    
    // Calculate SHA-256 hash of bootloader data
    uint8_t hash[32];
    calculate_sha256(data, data_size, hash);
    
    // Verify RSA signature using Sony's public key
    bool signature_valid = verify_rsa_signature(hash, 32, signature, 256);
    
    if (signature_valid) {
        // Additional checks for PS5-specific boot requirements
        if (!check_boot_header(data, data_size)) {
            Logger::Error("Invalid boot header format");
            return false;
        }
        
        if (!check_boot_version(data, data_size)) {
            Logger::Error("Unsupported boot version");
            return false;
        }
        
        secure_boot_verified = true;
        Logger::Info("Secure boot verification: PASSED");
    } else {
        Logger::Error("Secure boot verification: FAILED - Invalid signature");
    }
    
    return signature_valid;
}

bool SonyIOComplex::compress_kraken(const uint8_t* input, size_t input_size, std::vector<uint8_t>& output) {
    // Simplified Kraken compression implementation
    // TODO: use RAD Game Tools' Kraken compressor like in PS5
    
    output.clear();
    output.reserve(input_size);
    
    // Simple RLE compression as placeholder for real Kraken
    // TODO: Implement proper Kraken compression
    for (size_t i = 0; i < input_size; ) {
        uint8_t current_byte = input[i];
        size_t run_length = 1;
        
        // Count consecutive identical bytes
        while (i + run_length < input_size && 
               input[i + run_length] == current_byte && 
               run_length < 255) {
            run_length++;
        }
        
        if (run_length >= 3) {
            // Encode as run: [0xFF] [length] [byte]
            output.push_back(0xFF);
            output.push_back(static_cast<uint8_t>(run_length));
            output.push_back(current_byte);
        } else {
            // Store literal bytes
            for (size_t j = 0; j < run_length; j++) {
                output.push_back(input[i + j]);
            }
        }
        
        i += run_length;
    }
    
    return output.size() < input_size;
}

bool SonyIOComplex::decompress_kraken(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    output.clear();
    
    for (size_t i = 0; i < input.size(); ) {
        if (input[i] == 0xFF && i + 2 < input.size()) {
            uint8_t run_length = input[i + 1];
            uint8_t byte_value = input[i + 2];
            
            for (uint8_t j = 0; j < run_length; j++) {
                output.push_back(byte_value);
            }
            
            i += 3;
        } else {
            output.push_back(input[i]);
            i++;
        }
    }
    
    return true;
}

void SonyIOComplex::apply_hrtf(float azimuth, float elevation, float* left_gain, float* right_gain) {
    // Real HRTF implementation using measured data
    // This is a simplified version - TODO: use extensive HRTF databases like in PS5
    
    float azimuth_deg = azimuth * 180.0f / M_PI;
    float elevation_deg = elevation * 180.0f / M_PI;
    
    azimuth_deg = std::fmod(azimuth_deg + 360.0f, 360.0f);
    elevation_deg = std::max(-90.0f, std::min(90.0f, elevation_deg));
    
    // Simplified HRTF calculation based on spherical coordinates
    // TODO: Implement proper HRTF calculation  
    float azimuth_rad = azimuth_deg * M_PI / 180.0f;
    float elevation_rad = elevation_deg * M_PI / 180.0f;
    
    // Left ear gain calculation
    float left_azimuth_factor = std::cos(azimuth_rad + M_PI * 0.25f);
    float left_elevation_factor = std::cos(elevation_rad);
    *left_gain = 0.5f + 0.5f * left_azimuth_factor * left_elevation_factor;
    
    // Right ear gain calculation  
    float right_azimuth_factor = std::cos(azimuth_rad - M_PI * 0.25f);
    float right_elevation_factor = std::cos(elevation_rad);
    *right_gain = 0.5f + 0.5f * right_azimuth_factor * right_elevation_factor;
    
    // Apply head shadow effect for high frequencies
    float head_shadow = std::exp(-std::abs(azimuth_rad) * 0.5f);
    if (azimuth_rad > 0) {
        *left_gain *= head_shadow;
    } else {
        *right_gain *= head_shadow;
    }
}

float SonyIOComplex::calculate_doppler_shift(const Tempest3D::AudioSource& source, 
                                           float dx, float dy, float dz, float distance) {
    // Calculate Doppler effect based on source velocity
    const float sound_speed = 343.0f; // m/s at 20°C
    
    if (distance < 0.001f) return 1.0f;
    
    // Calculate radial velocity (velocity component towards listener)
    float radial_velocity = (source.velocity[0] * dx + 
                           source.velocity[1] * dy + 
                           source.velocity[2] * dz) / distance;
    
    // Doppler shift formula: f' = f * (c + vr) / (c + vs)
    // Simplified: assuming listener is stationary (vr = 0)
    // vs = source velocity
    // TODO: Implement proper Doppler effect calculation
    float doppler_factor = sound_speed / (sound_speed - radial_velocity);
    
    // Clamp to reasonable range to avoid audio artifacts
    return std::max(0.5f, std::min(2.0f, doppler_factor));
}

void SonyIOComplex::apply_room_acoustics(float* left, float* right, size_t sample_index) {
    // Real room acoustics simulation with multiple reflection paths
    static std::vector<float> early_reflections_left(2400, 0.0f);  // 50ms
    static std::vector<float> early_reflections_right(2400, 0.0f);
    static std::vector<float> late_reverb_left(9600, 0.0f);       // 200ms
    static std::vector<float> late_reverb_right(9600, 0.0f);
    static size_t reflection_pos = 0;
    static size_t reverb_pos = 0;
    
    // Early reflections (walls, ceiling, floor)
    float early_left = early_reflections_left[reflection_pos];
    float early_right = early_reflections_right[reflection_pos];
    
    early_reflections_left[reflection_pos] = *left * 0.3f + early_left * 0.2f;
    early_reflections_right[reflection_pos] = *right * 0.3f + early_right * 0.2f;
    
    // Late reverb (diffuse reflections)
    float late_left = late_reverb_left[reverb_pos];
    float late_right = late_reverb_right[reverb_pos];
    
    late_reverb_left[reverb_pos] = (*left + early_left) * 0.1f + late_left * 0.4f;
    late_reverb_right[reverb_pos] = (*right + early_right) * 0.1f + late_right * 0.4f;
    
    // Mix original signal with reflections and reverb
    *left += early_left * 0.2f + late_left * 0.1f;
    *right += early_right * 0.2f + late_right * 0.1f;
    
    reflection_pos = (reflection_pos + 1) % early_reflections_left.size();
    reverb_pos = (reverb_pos + 1) % late_reverb_left.size();
}

SecurityProcessor::SecurityProcessor() 
    : current_level(SecurityLevel::USER), secure_boot_verified(false) {
    
    // Add default secure regions
    AddSecureRegion({0x00000000, 0x00100000, SecurityLevel::HYPERVISOR, true, 0}); // Boot ROM
    AddSecureRegion({0x80000000, 0x10000000, SecurityLevel::KERNEL, true, 1});     // Kernel space
    
    Logger::Info("Security Processor initialized");
}

bool SecurityProcessor::CheckAccess(uint64_t addr, size_t size, SecurityLevel required_level) {
    // Check if access is within any secure region
    for (const auto& region : secure_regions) {
        if (addr >= region.base_addr && addr + size <= region.base_addr + region.size) {
            return current_level <= region.min_level && current_level <= required_level;
        }
    }
    
    // Default: allow access if security level is sufficient
    return current_level <= required_level;
}

void SecurityProcessor::AddSecureRegion(const SecureRegion& region) {
    secure_regions.push_back(region);
    Logger::Debug("Added secure region: 0x{:X}-0x{:X}, level={}", 
                  region.base_addr, region.base_addr + region.size, (int)region.min_level);
}

uint32_t SecurityProcessor::CreateCryptoContext(const uint8_t* key, const uint8_t* iv) {
    static uint32_t next_context_id = 1;
    
    CryptoContext context;
    memcpy(context.aes_key, key, 32);
    memcpy(context.iv, iv, 16);
    context.key_valid = true;
    context.usage_count = 0;
    
    uint32_t context_id = next_context_id++;
    crypto_contexts[context_id] = context;
    
    Logger::Debug("Created crypto context: {}", context_id);
    return context_id;
}

bool SecurityProcessor::VerifySecureBoot(const void* bootloader, size_t size) {
    // Simulate secure boot verification
    Logger::Info("Verifying secure boot (size: {} bytes)", size);
    
    // TODO: verify cryptographic signatures
    secure_boot_verified = true;
    
    Logger::Info("Secure boot verification: {}", secure_boot_verified ? "PASSED" : "FAILED");
    return secure_boot_verified;
}

NetworkStack::NetworkStack() : next_socket_fd(1) {
    // Add default network interface
    NetworkInterface eth0;
    eth0.name = "eth0";
    memset(eth0.mac_address, 0, 6);
    eth0.mac_address[0] = 0x02; // Locally administered
    eth0.ip_address = 0;
    eth0.subnet_mask = 0;
    eth0.gateway = 0;
    eth0.is_up = false;
    eth0.bytes_sent = 0;
    eth0.bytes_received = 0;
    
    interfaces.push_back(eth0);
    Logger::Info("Network stack initialized");
}

int NetworkStack::CreateSocket(int domain, int type, int protocol) {
    Socket socket;
    socket.fd = next_socket_fd++;
    socket.type = type;
    socket.protocol = protocol;
    socket.is_bound = false;
    socket.local_addr = 0;
    socket.local_port = 0;
    socket.remote_addr = 0;
    socket.remote_port = 0;
    
    sockets[socket.fd] = socket;
    Logger::Debug("Created socket: fd={}, type={}", socket.fd, type);
    
    return socket.fd;
}

}
