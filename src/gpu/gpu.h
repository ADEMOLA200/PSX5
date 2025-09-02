#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <array>

// RDNA2 GPU Architecture Emulation for PS5
// Implements AMD RDNA2 compute units, graphics pipeline, and command processing

struct RDNA2ComputeUnit {
    // Each CU has 64 stream processors (ALUs)
    std::array<uint32_t, 64> alu_units;
    std::array<float, 64> vector_registers[256]; // 256 VGPR per wavefront
    std::array<uint32_t, 128> scalar_registers;  // 128 SGPR
    
    // Local Data Share (64KB per CU)
    std::array<uint8_t, 65536> lds;
    
    // Wavefront execution state
    struct Wavefront {
        uint64_t pc;
        uint64_t exec_mask;
        bool active;
        uint32_t wave_id;
    };
    std::array<Wavefront, 32> wavefronts; // 32 wavefronts per CU
    
    bool busy;
    uint32_t cu_id;
};

struct RDNA2ShaderEngine {
    static constexpr uint32_t CUS_PER_SE = 10; // PS5 has 10 CUs per SE
    std::array<RDNA2ComputeUnit, CUS_PER_SE> compute_units;
    
    // Geometry pipeline
    struct GeometryProcessor {
        bool primitive_assembly_active;
        uint32_t vertex_count;
        uint32_t primitive_count;
    } geometry_processor;
    
    // Rasterizer
    struct Rasterizer {
        uint32_t tile_width = 64;
        uint32_t tile_height = 64;
        bool hierarchical_z_enabled;
        struct TileBuffer {
            std::vector<uint32_t> primitive_ids;
            std::vector<float> z_buffer_hierarchy[4]; // 4 levels of hierarchical Z
            bool early_z_reject_enabled;
            uint32_t visible_primitive_count;
        };
        std::unordered_map<uint64_t, TileBuffer> tile_buffers; // tile_id -> buffer
    } rasterizer;
    
    uint32_t se_id;
};

struct RDNA2RenderBackend {
    // Color/Depth render targets
    struct RenderTarget {
        uint64_t base_address;
        uint32_t width, height;
        uint32_t format;
        uint32_t pitch;
        bool compression_enabled;
    };
    
    std::array<RenderTarget, 8> color_targets;
    RenderTarget depth_target;
    
    // ROPs (Render Output Units)
    static constexpr uint32_t ROP_COUNT = 64; // PS5 has 64 ROPs
    struct ROP {
        bool blend_enabled;
        uint32_t blend_equation;
        bool depth_test_enabled;
        uint32_t depth_func;
    };
    std::array<ROP, ROP_COUNT> rops;
};

// Forward declaration for graphics pipeline integration
class GraphicsPipeline;
class ComputePipeline;

class GPU {
public:
    struct Command { 
        uint32_t opcode; 
        uint64_t arg0; 
        uint64_t arg1; 
        uint64_t arg2;
        uint64_t arg3;
    };
    
    // RDNA2 Command Processor opcodes
    enum CommandType : uint32_t {
        // Graphics commands
        DRAW_INDEX_AUTO = 0x2D,
        DRAW_INDEX_2 = 0x2E,
        DRAW_INDIRECT = 0x2F,
        DISPATCH_DIRECT = 0x15,
        DISPATCH_INDIRECT = 0x16,
        
        // State setting
        SET_CONTEXT_REG = 0x69,
        SET_CONFIG_REG = 0x68,
        SET_SH_REG = 0x76,
        
        // Memory operations
        DMA_DATA = 0x50,
        COPY_DATA = 0x40,
        WRITE_DATA = 0x37,
        
        // Synchronization
        EVENT_WRITE = 0x46,
        EVENT_WRITE_EOP = 0x47,
        WAIT_REG_MEM = 0x3C,
        
        // Compute
        SET_COMPUTE_SHADER = 0x80,
        DISPATCH_COMPUTE = 0x81,
        
        SET_DESCRIPTOR_SET = 0x90,
        BIND_VERTEX_BUFFER = 0x91,
        BIND_INDEX_BUFFER = 0x92,
        BIND_TEXTURE = 0x93,
        BIND_SAMPLER = 0x94,
        UPDATE_CONSTANTS = 0x95,
    };
    
    GPU();
    ~GPU();
    
    void submit(const std::vector<Command>& commands);
    void present();
    void reset();
    
    // RDNA2 specific functions
    void initialize_shader_engines();
    void execute_graphics_command(const Command& cmd);
    void execute_compute_command(const Command& cmd);
    void process_draw_call(uint32_t vertex_count, uint32_t instance_count);
    void dispatch_compute_shader(uint32_t group_x, uint32_t group_y, uint32_t group_z);
    
    // Memory management
    void allocate_gpu_memory(uint64_t address, size_t size);
    void free_gpu_memory(uint64_t address);
    uint8_t* get_gpu_memory_ptr(uint64_t address);
    
    struct GPUResource {
        uint64_t address;
        size_t size;
        uint32_t format;
        uint32_t width, height, depth;
        uint32_t mip_levels;
        uint32_t resource_type; // 0=buffer, 1=texture1D, 2=texture2D, 3=texture3D
        bool compressed;
    };
    
    uint32_t create_buffer(size_t size, uint32_t usage_flags);
    uint32_t create_texture(uint32_t width, uint32_t height, uint32_t format, uint32_t mip_levels);
    void destroy_resource(uint32_t resource_id);
    GPUResource* get_resource(uint32_t resource_id);
    
    struct DescriptorSet {
        uint32_t set_id;
        std::unordered_map<uint32_t, uint32_t> texture_bindings; // binding -> resource_id
        std::unordered_map<uint32_t, uint32_t> sampler_bindings; // binding -> sampler_id
        std::unordered_map<uint32_t, uint32_t> buffer_bindings;  // binding -> resource_id
        std::unordered_map<uint32_t, uint64_t> constant_data;    // binding -> data
    };
    
    uint32_t create_descriptor_set();
    void update_descriptor_set(uint32_t set_id, uint32_t binding, uint32_t resource_id, uint32_t resource_type);
    void bind_descriptor_set(uint32_t set_id, uint32_t shader_stage);
    
    // Shader compilation and caching
    struct CompiledShader {
        std::vector<uint32_t> bytecode;
        uint32_t shader_type; // VS, PS, CS, etc.
        uint32_t resource_usage;
        std::vector<uint32_t> texture_bindings;
        std::vector<uint32_t> sampler_bindings;
        std::vector<uint32_t> buffer_bindings;
        uint32_t constant_buffer_size;
    };
    
    uint32_t compile_shader(const std::vector<uint8_t>& shader_source, uint32_t shader_type);
    CompiledShader* get_compiled_shader(uint32_t shader_id);
    
    // Performance counters
    struct PerformanceCounters {
        uint64_t triangles_rendered;
        uint64_t pixels_shaded;
        uint64_t compute_dispatches;
        uint64_t memory_bandwidth_used;
        uint64_t gpu_utilization;
        uint64_t tiles_processed;
        uint64_t primitives_culled;
        uint64_t hierarchical_z_rejects;
    } perf_counters;
    
    // Graphics pipeline interface methods
    GraphicsPipeline* GetGraphicsPipeline() { return graphics_pipeline.get(); }
    ComputePipeline* GetComputePipeline() { return compute_pipeline.get(); }
    
    // Enhanced rendering capabilities
    void BeginFrame();
    void EndFrame();
    void SetRenderTarget(uint32_t index, uint64_t address, uint32_t width, uint32_t height, uint32_t format);
    void SetDepthTarget(uint64_t address, uint32_t width, uint32_t height, uint32_t format);
    
    // Advanced GPU features
    void EnableVariableRateShading(bool enable);
    void SetShadingRate(uint32_t rate);
    void EnableMeshShaders(bool enable);
    
    void enable_tile_based_rendering(bool enable);
    void configure_hierarchical_z(bool enable, uint32_t levels);
    void begin_render_pass_advanced(uint32_t render_pass_id, const std::vector<uint32_t>& color_targets, 
                                   uint32_t depth_target, bool clear_color, bool clear_depth);
    void end_render_pass_advanced();
    
private:
    // RDNA2 Hardware Components
    static constexpr uint32_t SHADER_ENGINE_COUNT = 4; // PS5 has 4 shader engines
    std::array<RDNA2ShaderEngine, SHADER_ENGINE_COUNT> shader_engines;
    
    static constexpr uint32_t RB_COUNT = 4; // 4 render backends
    std::array<RDNA2RenderBackend, RB_COUNT> render_backends;
    
    // Command processor
    struct CommandProcessor {
        std::vector<Command> command_queue;
        uint32_t current_command_index;
        bool processing;
    } command_processor;
    
    // GPU Memory (16GB GDDR6 in PS5)
    static constexpr size_t GPU_MEMORY_SIZE = 16ULL * 1024 * 1024 * 1024;
    std::unique_ptr<uint8_t[]> gpu_memory;
    std::unordered_map<uint64_t, size_t> memory_allocations;
    
    std::unordered_map<uint32_t, GPUResource> gpu_resources;
    std::unordered_map<uint32_t, DescriptorSet> descriptor_sets;
    uint32_t next_resource_id = 1;
    uint32_t next_descriptor_set_id = 1;
    
    // Shader cache
    std::unordered_map<uint32_t, CompiledShader> shader_cache;
    uint32_t next_shader_id = 1;
    
    // Graphics state
    struct GraphicsState {
        uint64_t vertex_buffer_address;
        uint64_t index_buffer_address;
        uint32_t vertex_shader_id;
        uint32_t pixel_shader_id;
        uint32_t primitive_topology;
        bool depth_test_enabled;
        bool blend_enabled;
        std::array<uint32_t, 8> bound_descriptor_sets;
        std::array<uint32_t, 16> bound_vertex_buffers;
        uint32_t bound_index_buffer;
    } graphics_state;
    
    // Compute state
    struct ComputeState {
        uint32_t compute_shader_id;
        uint32_t thread_group_size[3];
        uint64_t constant_buffer_address;
        std::array<uint32_t, 8> bound_descriptor_sets;
    } compute_state;
    
    // Graphics and compute pipeline instances
    std::unique_ptr<GraphicsPipeline> graphics_pipeline;
    std::unique_ptr<ComputePipeline> compute_pipeline;
    
    // Frame state tracking
    struct FrameState {
        bool in_frame;
        uint64_t frame_number;
        uint32_t active_render_targets;
        bool depth_target_bound;
        bool in_render_pass;
        uint32_t current_render_pass_id;
        std::vector<uint32_t> active_color_targets;
        uint32_t active_depth_target;
    } frame_state;
    
    // GPU features state
    struct AdvancedFeatures {
        bool variable_rate_shading_enabled;
        uint32_t current_shading_rate;
        bool mesh_shaders_enabled;
        bool primitive_shaders_enabled;
        bool tile_based_rendering_enabled;
        bool hierarchical_z_enabled;
        uint32_t hierarchical_z_levels;
    } advanced_features;
    
    // Internal processing functions
    void process_command_queue();
    void execute_shader_on_cu(RDNA2ComputeUnit& cu, const CompiledShader& shader);
    void rasterize_triangle(const float vertices[9]); // 3 vertices * 3 components
    void shade_pixel(uint32_t x, uint32_t y, const CompiledShader& pixel_shader);
    
    void bin_primitives_to_tiles(const std::vector<float>& vertices, uint32_t primitive_count);
    void process_tile_advanced(uint32_t tile_x, uint32_t tile_y, RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer);
    bool hierarchical_z_test(uint32_t tile_x, uint32_t tile_y, float z_min, float z_max, uint32_t level);
    void update_hierarchical_z(uint32_t tile_x, uint32_t tile_y, float depth);
    void execute_render_pass_subpass(uint32_t subpass_index);
    
    // Vulkan backend integration
    class VulkanBackend* vulkan_backend;
    void sync_with_vulkan();
    
#ifdef PSX5_ENABLE_VULKAN
    std::unordered_map<uint32_t, uint32_t> vulkan_buffer_mapping_;  // resource_id -> vulkan_buffer_id
    std::unordered_map<uint32_t, uint32_t> vulkan_image_mapping_;   // resource_id -> vulkan_image_id
#endif
};
