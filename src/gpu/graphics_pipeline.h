#pragma once

#include "../core/types.h"
#include "gpu.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace PS5Emu {

// Complete Graphics Pipeline Implementation
class GraphicsPipeline {
public:
    // Vertex Input Assembly
    struct VertexInputState {
        struct VertexBinding {
            uint32_t binding;
            uint32_t stride;
            bool per_instance;
        };
        
        struct VertexAttribute {
            uint32_t location;
            uint32_t binding;
            uint32_t format;
            uint32_t offset;
        };
        
        std::vector<VertexBinding> bindings;
        std::vector<VertexAttribute> attributes;
    };
    
    // Tessellation State
    struct TessellationState {
        bool enabled;
        uint32_t patch_control_points;
        float tessellation_levels[6]; // outer[4] + inner[2]
    };
    
    // Geometry Shader State
    struct GeometryState {
        bool enabled;
        uint32_t input_primitive;
        uint32_t output_primitive;
        uint32_t max_output_vertices;
    };
    
    // Rasterization State
    struct RasterizationState {
        bool depth_clamp_enable;
        bool rasterizer_discard_enable;
        uint32_t polygon_mode; // FILL, LINE, POINT
        uint32_t cull_mode;
        uint32_t front_face;
        bool depth_bias_enable;
        float depth_bias_constant;
        float depth_bias_clamp;
        float depth_bias_slope;
        float line_width;
    };
    
    // Multisample State
    struct MultisampleState {
        uint32_t sample_count;
        bool sample_shading_enable;
        float min_sample_shading;
        uint32_t sample_mask;
        bool alpha_to_coverage_enable;
        bool alpha_to_one_enable;
    };
    
    // Depth/Stencil State
    struct DepthStencilState {
        bool depth_test_enable;
        bool depth_write_enable;
        uint32_t depth_compare_op;
        bool depth_bounds_test_enable;
        bool stencil_test_enable;
        
        struct StencilOpState {
            uint32_t fail_op;
            uint32_t pass_op;
            uint32_t depth_fail_op;
            uint32_t compare_op;
            uint32_t compare_mask;
            uint32_t write_mask;
            uint32_t reference;
        } front_stencil, back_stencil;
        
        float min_depth_bounds;
        float max_depth_bounds;
    };
    
    // Color Blend State
    struct ColorBlendState {
        bool logic_op_enable;
        uint32_t logic_op;
        
        struct ColorBlendAttachment {
            bool blend_enable;
            uint32_t src_color_blend_factor;
            uint32_t dst_color_blend_factor;
            uint32_t color_blend_op;
            uint32_t src_alpha_blend_factor;
            uint32_t dst_alpha_blend_factor;
            uint32_t alpha_blend_op;
            uint32_t color_write_mask;
        };
        
        std::vector<ColorBlendAttachment> attachments;
        float blend_constants[4];
    };
    
    // Multi-render target support
    struct MultiRenderTargetState {
        std::vector<uint32_t> color_attachments;
        uint32_t depth_attachment;
        bool independent_blend_enable;
        std::vector<ColorBlendAttachment> per_target_blend;
    };
    
    // Pipeline State Object
    struct PipelineState {
        uint32_t pipeline_id;
        
        // Shader stages
        uint32_t vertex_shader;
        uint32_t tessellation_control_shader;
        uint32_t tessellation_evaluation_shader;
        uint32_t geometry_shader;
        uint32_t fragment_shader;
        
        // Fixed function state
        VertexInputState vertex_input;
        TessellationState tessellation;
        GeometryState geometry;
        RasterizationState rasterization;
        MultisampleState multisample;
        DepthStencilState depth_stencil;
        ColorBlendState color_blend;
        MultiRenderTargetState multi_render_target;
        
        // Render pass compatibility
        uint32_t render_pass;
        uint32_t subpass;
    };
    
    // Render Pass
    struct RenderPass {
        struct Attachment {
            uint32_t format;
            uint32_t samples;
            uint32_t load_op;
            uint32_t store_op;
            uint32_t stencil_load_op;
            uint32_t stencil_store_op;
            uint32_t initial_layout;
            uint32_t final_layout;
        };
        
        struct SubpassDescription {
            std::vector<uint32_t> input_attachments;
            std::vector<uint32_t> color_attachments;
            std::vector<uint32_t> resolve_attachments;
            uint32_t depth_stencil_attachment;
            std::vector<uint32_t> preserve_attachments;
            bool variable_rate_shading_enabled;
            uint32_t shading_rate;
        };
        
        struct SubpassDependency {
            uint32_t src_subpass;
            uint32_t dst_subpass;
            uint32_t src_stage_mask;
            uint32_t dst_stage_mask;
            uint32_t src_access_mask;
            uint32_t dst_access_mask;
            bool memory_barrier;
            bool buffer_barrier;
            bool image_barrier;
        };
        
        uint32_t render_pass_id;
        std::vector<Attachment> attachments;
        std::vector<SubpassDescription> subpasses;
        std::vector<SubpassDependency> dependencies;
        bool tile_based_optimization;
        bool early_z_optimization;
        bool conservative_rasterization;
    };
    
    // Framebuffer
    struct Framebuffer {
        uint32_t framebuffer_id;
        uint32_t render_pass;
        std::vector<uint64_t> attachment_addresses;
        uint32_t width;
        uint32_t height;
        uint32_t layers;
    };

private:
    GPU* gpu;
    std::unordered_map<uint32_t, PipelineState> pipelines;
    std::unordered_map<uint32_t, RenderPass> render_passes;
    std::unordered_map<uint32_t, Framebuffer> framebuffers;
    
    uint32_t next_pipeline_id = 1;
    uint32_t next_render_pass_id = 1;
    uint32_t next_framebuffer_id = 1;
    
    // Current rendering state
    uint32_t current_pipeline = 0;
    uint32_t current_render_pass = 0;
    uint32_t current_framebuffer = 0;
    uint32_t current_subpass = 0;
    
public:
    GraphicsPipeline(GPU* gpu_instance);
    ~GraphicsPipeline();
    
    // Pipeline management
    uint32_t CreatePipeline(const PipelineState& state);
    void DestroyPipeline(uint32_t pipeline_id);
    void BindPipeline(uint32_t pipeline_id);
    
    // Render pass management
    uint32_t CreateRenderPass(const RenderPass& render_pass);
    void DestroyRenderPass(uint32_t render_pass_id);
    
    // Framebuffer management
    uint32_t CreateFramebuffer(const Framebuffer& framebuffer);
    void DestroyFramebuffer(uint32_t framebuffer_id);
    
    // Rendering commands
    void BeginRenderPass(uint32_t render_pass_id, uint32_t framebuffer_id);
    void NextSubpass();
    void EndRenderPass();
    
    // Advanced rendering methods
    void BeginRenderPassMultiTarget(uint32_t render_pass_id, uint32_t framebuffer_id, 
                                   const std::vector<float>& clear_colors, float clear_depth);
    void NextSubpassWithBarriers(const std::vector<uint32_t>& memory_barriers);
    void EndRenderPassWithResolve(const std::vector<uint32_t>& resolve_targets);
    
    // Draw commands
    void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
    void DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
    void DrawIndirect(uint64_t buffer_address, uint32_t draw_count, uint32_t stride);
    void DrawIndexedIndirect(uint64_t buffer_address, uint32_t draw_count, uint32_t stride);
    
    // Advanced draw commands with resource binding
    void DrawWithDescriptorSets(uint32_t vertex_count, uint32_t instance_count, 
                               const std::vector<uint32_t>& descriptor_sets);
    void DrawIndexedWithResources(uint32_t index_count, uint32_t instance_count,
                                 const std::vector<uint32_t>& vertex_buffers,
                                 uint32_t index_buffer, const std::vector<uint32_t>& descriptor_sets);
    
    // Resource binding
    void BindVertexBuffers(uint32_t first_binding, const std::vector<uint64_t>& buffer_addresses, const std::vector<uint64_t>& offsets);
    void BindIndexBuffer(uint64_t buffer_address, uint64_t offset, uint32_t index_type);
    void BindDescriptorSets(uint32_t first_set, const std::vector<uint32_t>& descriptor_sets);
    
    // Dynamic state
    void SetViewport(float x, float y, float width, float height, float min_depth, float max_depth);
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void SetLineWidth(float line_width);
    void SetDepthBias(float constant_factor, float clamp, float slope_factor);
    void SetBlendConstants(const float blend_constants[4]);
    void SetDepthBounds(float min_depth_bounds, float max_depth_bounds);
    
    // Advanced rendering features
    void EnableConservativeRasterization(bool enable);
    void SetSampleLocations(const std::vector<float>& sample_locations);
    void EnableVariableRateShading(bool enable, uint32_t shading_rate);
    
    // Mesh shaders (RDNA2 feature)
    void DrawMeshTasks(uint32_t task_count, uint32_t first_task);
    void DrawMeshTasksIndirect(uint64_t buffer_address, uint32_t draw_count, uint32_t stride);
    
    // Tile-based rendering optimization
    void EnableTileBasedRendering(bool enable);
    void SetTileSize(uint32_t width, uint32_t height);
    void OptimizeForTileRendering(bool conservative_raster, bool early_z);
    
private:
    // Internal pipeline execution
    void ExecuteVertexStage(const PipelineState& pipeline, uint32_t vertex_count);
    void ExecuteTessellationStage(const PipelineState& pipeline);
    void ExecuteGeometryStage(const PipelineState& pipeline);
    void ExecuteRasterizationStage(const PipelineState& pipeline);
    void ExecuteFragmentStage(const PipelineState& pipeline);
    
    // Primitive processing
    void ProcessPrimitives(uint32_t primitive_type, const void* vertex_data, uint32_t vertex_count);
    void ClipPrimitives();
    void CullPrimitives();
    
    // Rasterization
    void RasterizeTriangle(const float v0[4], const float v1[4], const float v2[4]);
    void RasterizeLine(const float v0[4], const float v1[4]);
    void RasterizePoint(const float v[4]);
    
    // Fragment processing
    void ProcessFragment(uint32_t x, uint32_t y, float depth, const float* interpolated_attributes);
    bool DepthTest(uint32_t x, uint32_t y, float depth);
    bool StencilTest(uint32_t x, uint32_t y, uint32_t stencil_value);
    void BlendFragment(uint32_t x, uint32_t y, const float color[4]);
    
    // Tile-based rendering (RDNA2 optimization)
    struct Tile {
        uint32_t x, y;
        uint32_t width, height;
        std::vector<uint32_t> primitive_list;
    };
    
    struct AdvancedTile {
        uint32_t x, y;
        uint32_t width, height;
        std::vector<uint32_t> primitive_list;
        std::vector<uint32_t> visible_primitives;
        float hierarchical_z[4][16]; // 4 levels, up to 16 samples per level
        bool early_z_reject;
        uint32_t shading_rate; // Variable rate shading
    };
    
    void TileRasterization(const std::vector<Tile>& tiles);
    void ProcessTile(const Tile& tile);
    void ProcessAdvancedTile(const AdvancedTile& tile);
    void ExecuteSubpassDependencies(const std::vector<SubpassDependency>& dependencies, uint32_t current_subpass);
    void ResolveMultisampleAttachments(const std::vector<uint32_t>& resolve_targets);
    
    // Resource binding state
    struct ResourceBindingState {
        std::array<uint32_t, 8> bound_descriptor_sets;
        std::array<uint32_t, 16> bound_vertex_buffers;
        uint32_t bound_index_buffer;
        bool descriptor_sets_dirty;
        bool vertex_buffers_dirty;
        bool index_buffer_dirty;
    } resource_binding_state;
    
    void UpdateResourceBindings();
    void ValidateResourceBindings();
};

// Compute Pipeline
class ComputePipeline {
public:
    struct ComputePipelineState {
        uint32_t pipeline_id;
        uint32_t compute_shader;
        uint32_t local_size_x;
        uint32_t local_size_y;
        uint32_t local_size_z;
    };

private:
    GPU* gpu;
    std::unordered_map<uint32_t, ComputePipelineState> compute_pipelines;
    uint32_t next_pipeline_id = 1;
    uint32_t current_pipeline = 0;
    
public:
    ComputePipeline(GPU* gpu_instance);
    
    uint32_t CreateComputePipeline(const ComputePipelineState& state);
    void DestroyComputePipeline(uint32_t pipeline_id);
    void BindComputePipeline(uint32_t pipeline_id);
    
    void Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
    void DispatchIndirect(uint64_t buffer_address);
    
    void BindComputeDescriptorSets(uint32_t first_set, const std::vector<uint32_t>& descriptor_sets);
};

} // namespace PS5Emu
