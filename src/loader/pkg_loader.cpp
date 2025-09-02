#include "pkg_loader.h"
#include "../core/logger.h"
#include <fstream>
#include <cstring>
#include <openssl/aes.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace PS5Emu {

// PS5 PKG encryption keys (these would be extracted from actual PS5 console)
const uint8_t PKGLoader::PKG_AES_KEY[32] = {
    0x47, 0x29, 0xB0, 0x73, 0xA2, 0xA6, 0x27, 0x7C, 0x92, 0x8D, 0x39, 0x8A, 0x5E, 0x2C, 0x89, 0x7B,
    0x35, 0x91, 0x6A, 0x8F, 0x46, 0x73, 0xC2, 0x89, 0x12, 0x27, 0x46, 0x8C, 0x7A, 0x87, 0x8A, 0x92
};

const uint8_t PKGLoader::PKG_HMAC_KEY[64] = {
    0x0C, 0xFE, 0x60, 0x9A, 0x48, 0xBD, 0x81, 0x37, 0x99, 0x32, 0x8F, 0x46, 0x75, 0xA0, 0x49, 0x47,
    0xF2, 0xA0, 0x97, 0x2A, 0x4D, 0x8E, 0x76, 0xAA, 0x0E, 0x69, 0x0D, 0x37, 0x8C, 0x25, 0x4C, 0x29,
    0xA0, 0x84, 0x49, 0x9A, 0xDB, 0x81, 0x9D, 0x64, 0xF3, 0x8A, 0x61, 0x9E, 0xD4, 0x91, 0x55, 0x70,
    0x38, 0x0C, 0x7F, 0x2C, 0x4E, 0x6A, 0x73, 0x8A, 0x06, 0x09, 0xA4, 0x4F, 0x9F, 0x52, 0x6A, 0x2E
};

bool PKGLoader::verify_header(const PKGHeader& header) {
    // Verify PKG magic number
    if (header.magic != 0x7F504B47) {
        log::error("Invalid PKG magic number");
        return false;
    }
    
    // Verify supported revision
    if (header.revision < 0x1000 || header.revision > 0x1FFF) {
        log::error("Unsupported PKG revision: " + std::to_string(header.revision));
        return false;
    }
    
    // Verify metadata bounds
    if (header.metadata_offset + header.metadata_size > header.body_offset) {
        log::error("Invalid PKG metadata bounds");
        return false;
    }
    
    return true;
}

bool PKGLoader::decrypt_body(const std::vector<uint8_t>& encrypted_data, 
                            const uint8_t* key, const uint8_t* iv,
                            std::vector<uint8_t>& decrypted_data) {
    if (encrypted_data.empty()) return false;
    
    decrypted_data.resize(encrypted_data.size());
    
    AES_KEY aes_key;
    if (AES_set_decrypt_key(key, 256, &aes_key) != 0) {
        log::error("Failed to set AES decryption key");
        return false;
    }
    
    // Decrypt in CBC mode with custom IV
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    
    size_t blocks = encrypted_data.size() / 16;
    for (size_t i = 0; i < blocks; i++) {
        const uint8_t* input = encrypted_data.data() + (i * 16);
        uint8_t* output = decrypted_data.data() + (i * 16);
        
        AES_decrypt(input, output, &aes_key);
        
        // XOR with IV for CBC mode
        for (int j = 0; j < 16; j++) {
            output[j] ^= iv_copy[j];
        }
        
        // Update IV for next block
        memcpy(iv_copy, input, 16);
    }
    
    // Handle remaining bytes
    size_t remaining = encrypted_data.size() % 16;
    if (remaining > 0) {
        uint8_t padded_block[16] = {0};
        memcpy(padded_block, encrypted_data.data() + (blocks * 16), remaining);
        
        uint8_t decrypted_block[16];
        AES_decrypt(padded_block, decrypted_block, &aes_key);
        
        for (size_t j = 0; j < remaining; j++) {
            decrypted_data[blocks * 16 + j] = decrypted_block[j] ^ iv_copy[j];
        }
    }
    
    return true;
}

bool PKGLoader::parse_metadata(const std::vector<uint8_t>& data, 
                              uint32_t offset, uint32_t count,
                              std::vector<PKGMetadata>& metadata) {
    metadata.clear();
    metadata.reserve(count);
    
    size_t pos = offset;
    for (uint32_t i = 0; i < count; i++) {
        if (pos + 8 > data.size()) return false;
        
        PKGMetadata entry;
        entry.id = *reinterpret_cast<const uint32_t*>(data.data() + pos);
        entry.size = *reinterpret_cast<const uint32_t*>(data.data() + pos + 4);
        pos += 8;
        
        if (pos + entry.size > data.size()) return false;
        
        entry.data.resize(entry.size);
        memcpy(entry.data.data(), data.data() + pos, entry.size);
        pos += entry.size;
        
        metadata.push_back(std::move(entry));
    }
    
    return true;
}

bool PKGLoader::extract_content_entries(const std::vector<uint8_t>& decrypted_body,
                                       const std::vector<PKGMetadata>& metadata,
                                       std::vector<PKGContentEntry>& entries) {
    entries.clear();
    
    // Find content table in metadata
    const PKGMetadata* content_table = nullptr;
    for (const auto& meta : metadata) {
        if (meta.id == 0x0001) { // Content table ID
            content_table = &meta;
            break;
        }
    }
    
    if (!content_table) {
        log::error("No content table found in PKG metadata");
        return false;
    }
    
    // Parse content table entries
    size_t pos = 0;
    while (pos + 32 <= content_table->data.size()) {
        PKGContentEntry entry;
        
        entry.id = *reinterpret_cast<const uint32_t*>(content_table->data.data() + pos);
        entry.filename_offset = *reinterpret_cast<const uint32_t*>(content_table->data.data() + pos + 4);
        entry.flags1 = *reinterpret_cast<const uint32_t*>(content_table->data.data() + pos + 8);
        entry.flags2 = *reinterpret_cast<const uint32_t*>(content_table->data.data() + pos + 12);
        entry.offset = *reinterpret_cast<const uint64_t*>(content_table->data.data() + pos + 16);
        entry.size = *reinterpret_cast<const uint64_t*>(content_table->data.data() + pos + 24);
        
        pos += 32;
        
        // Extract filename
        if (entry.filename_offset < content_table->data.size()) {
            const char* filename_ptr = reinterpret_cast<const char*>(
                content_table->data.data() + entry.filename_offset);
            entry.filename = std::string(filename_ptr);
        }
        
        // Extract file data from decrypted body
        if (entry.offset + entry.size <= decrypted_body.size()) {
            entry.data.resize(entry.size);
            memcpy(entry.data.data(), decrypted_body.data() + entry.offset, entry.size);
        }
        
        entries.push_back(std::move(entry));
    }
    
    return true;
}

std::string PKGLoader::get_metadata_string(const std::vector<PKGMetadata>& metadata, uint32_t id) {
    for (const auto& meta : metadata) {
        if (meta.id == id && !meta.data.empty()) {
            return std::string(reinterpret_cast<const char*>(meta.data.data()), meta.data.size());
        }
    }
    return "";
}

std::optional<PKGFile> PKGLoader::load_from_bytes(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < sizeof(PKGHeader)) {
        log::error("PKG file too small");
        return std::nullopt;
    }
    
    PKGFile pkg;
    
    // Parse header
    memcpy(&pkg.header, bytes.data(), sizeof(PKGHeader));
    if (!verify_header(pkg.header)) {
        return std::nullopt;
    }
    
    log::info("Loading PKG file, revision: " + std::to_string(pkg.header.revision));
    
    // Parse metadata
    if (!parse_metadata(bytes, pkg.header.metadata_offset, 
                       pkg.header.metadata_count, pkg.metadata)) {
        log::error("Failed to parse PKG metadata");
        return std::nullopt;
    }
    
    // Extract metadata strings
    pkg.title_id = get_metadata_string(pkg.metadata, 0x0002);
    pkg.content_id = get_metadata_string(pkg.metadata, 0x0003);
    pkg.version = get_metadata_string(pkg.metadata, 0x0004);
    
    // Decrypt body
    std::vector<uint8_t> encrypted_body(bytes.begin() + pkg.header.body_offset, 
                                       bytes.begin() + pkg.header.body_offset + pkg.header.body_size);
    std::vector<uint8_t> decrypted_body;
    
    if (!decrypt_body(encrypted_body, PKG_AES_KEY, pkg.header.iv, decrypted_body)) {
        log::error("Failed to decrypt PKG body");
        return std::nullopt;
    }
    
    // Extract content entries
    if (!extract_content_entries(decrypted_body, pkg.metadata, pkg.entries)) {
        log::error("Failed to extract PKG content entries");
        return std::nullopt;
    }
    
    log::info("Successfully loaded PKG: " + pkg.title_id + " v" + pkg.version);
    log::info("Extracted " + std::to_string(pkg.entries.size()) + " content entries");
    
    return pkg;
}

std::optional<PKGFile> PKGLoader::load_from_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        log::error("Failed to open PKG file: " + filepath);
        return std::nullopt;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    
    return load_from_bytes(bytes);
}

bool PKGLoader::extract_executable(const PKGFile& pkg, std::vector<uint8_t>& executable) {
    // Look for main executable (usually eboot.bin or similar)
    for (const auto& entry : pkg.entries) {
        if (entry.filename == "eboot.bin" || 
            entry.filename.find(".elf") != std::string::npos ||
            (entry.flags1 & 0x1) != 0) { // Executable flag
            
            executable = entry.data;
            log::info("Extracted executable: " + entry.filename + 
                     " (" + std::to_string(entry.size) + " bytes)");
            return true;
        }
    }
    
    log::error("No executable found in PKG file");
    return false;
}

} // namespace PS5Emu
