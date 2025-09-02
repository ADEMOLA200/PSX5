#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <map>

namespace PS5Emu {

struct PKGHeader {
    uint32_t magic;           // 0x7F504B47 (".PKG")
    uint32_t revision;        // Package revision
    uint16_t type;            // Package type (game, patch, dlc)
    uint16_t metadata_offset; // Offset to metadata
    uint32_t metadata_count;  // Number of metadata entries
    uint32_t metadata_size;   // Size of metadata section
    uint64_t body_offset;     // Offset to encrypted body
    uint64_t body_size;       // Size of encrypted body
    uint64_t content_id;      // Unique content identifier
    uint8_t digest[32];       // SHA-256 digest
    uint8_t iv[16];           // AES initialization vector
    uint8_t reserved[16];     // Reserved space
};

struct PKGMetadata {
    uint32_t id;              // Metadata entry ID
    uint32_t size;            // Size of metadata value
    std::vector<uint8_t> data; // Metadata value
};

struct PKGContentEntry {
    uint32_t id;              // Content entry ID
    uint32_t filename_offset; // Offset to filename
    uint32_t flags1;          // Entry flags
    uint32_t flags2;          // Additional flags
    uint64_t offset;          // Offset in decrypted data
    uint64_t size;            // Size of entry
    uint64_t encrypted_size;  // Size when encrypted
    std::string filename;     // Entry filename
    std::vector<uint8_t> data; // Decrypted entry data
};

struct PKGFile {
    PKGHeader header;
    std::vector<PKGMetadata> metadata;
    std::vector<PKGContentEntry> entries;
    std::string title_id;
    std::string content_id;
    std::string version;
    uint32_t app_type;
};

class PKGLoader {
private:
    // PS5 PKG encryption keys (would be extracted from console)
    static const uint8_t PKG_AES_KEY[32];
    static const uint8_t PKG_HMAC_KEY[64];
    
    bool verify_header(const PKGHeader& header);
    bool decrypt_body(const std::vector<uint8_t>& encrypted_data, 
                     const uint8_t* key, const uint8_t* iv,
                     std::vector<uint8_t>& decrypted_data);
    bool parse_metadata(const std::vector<uint8_t>& data, 
                       uint32_t offset, uint32_t count,
                       std::vector<PKGMetadata>& metadata);
    bool extract_content_entries(const std::vector<uint8_t>& decrypted_body,
                                const std::vector<PKGMetadata>& metadata,
                                std::vector<PKGContentEntry>& entries);
    std::string get_metadata_string(const std::vector<PKGMetadata>& metadata, uint32_t id);
    
public:
    std::optional<PKGFile> load_from_bytes(const std::vector<uint8_t>& bytes);
    std::optional<PKGFile> load_from_file(const std::string& filepath);
    bool extract_executable(const PKGFile& pkg, std::vector<uint8_t>& executable);
};

} // namespace PS5Emu
