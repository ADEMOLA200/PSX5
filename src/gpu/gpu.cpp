#include "gpu.h"
#ifdef PSX5_ENABLE_GLFW
#include "gpu/vulkan_glfw.h"
#endif
#ifdef PSX5_ENABLE_VULKAN
#include "gpu/vulkan_swapchain.h"
#endif
#include "graphics_pipeline.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <cmath>
#include <random>

GPU::GPU() {
    gpu_memory = std::make_unique<uint8_t[]>(GPU_MEMORY_SIZE);
    memset(gpu_memory.get(), 0, GPU_MEMORY_SIZE);
    
    initialize_shader_engines();
    
    // Initialize performance counters
    memset(&perf_counters, 0, sizeof(perf_counters));
    
    // Initialize graphics and compute state
    memset(&graphics_state, 0, sizeof(graphics_state));
    memset(&compute_state, 0, sizeof(compute_state));
    
#ifdef PSX5_ENABLE_VULKAN
    vulkan_backend = new VulkanBackend();
    if (!vulkan_backend->init()) {
        std::cerr << "GPU: Failed to initialize Vulkan backend, falling back to software rendering" << std::endl;
        delete vulkan_backend;
        vulkan_backend = nullptr;
    } else {
        std::cout << "GPU: Vulkan backend initialized successfully" << std::endl;
    }
#else
    vulkan_backend = nullptr;
    std::cout << "GPU: Vulkan support not compiled in, using software rendering" << std::endl;
#endif
    
    graphics_pipeline = std::make_unique<GraphicsPipeline>(this);
    compute_pipeline = std::make_unique<ComputePipeline>(this);
    
    memset(&frame_state, 0, sizeof(frame_state));
    frame_state.frame_number = 0;
    
    memset(&advanced_features, 0, sizeof(advanced_features));
    advanced_features.variable_rate_shading_enabled = false;
    advanced_features.current_shading_rate = 1; // 1x1 default
    advanced_features.mesh_shaders_enabled = true; // RDNA2 supports mesh shaders
    advanced_features.primitive_shaders_enabled = true;
    advanced_features.tile_based_rendering_enabled = true;
    advanced_features.hierarchical_z_enabled = true;
    advanced_features.hierarchical_z_levels = 4;
    
    std::cout << "RDNA2 GPU initialized with " << SHADER_ENGINE_COUNT 
              << " shader engines, " << (SHADER_ENGINE_COUNT * RDNA2ShaderEngine::CUS_PER_SE) 
              << " compute units total" << std::endl;
    std::cout << "Advanced features: VRS=" << (advanced_features.variable_rate_shading_enabled ? "ON" : "OFF")
              << ", Mesh Shaders=" << (advanced_features.mesh_shaders_enabled ? "ON" : "OFF")
              << ", Tile-Based Rendering=" << (advanced_features.tile_based_rendering_enabled ? "ON" : "OFF")
              << ", Hierarchical Z=" << (advanced_features.hierarchical_z_enabled ? "ON" : "OFF")
              << std::endl;
}

GPU::~GPU() {
#ifdef PSX5_ENABLE_VULKAN
    if (vulkan_backend) {
        vulkan_backend->shutdown();
        delete vulkan_backend;
        vulkan_backend = nullptr;
    }
#endif
}

void GPU::initialize_shader_engines() {
    for (uint32_t se = 0; se < SHADER_ENGINE_COUNT; ++se) {
        auto& shader_engine = shader_engines[se];
        shader_engine.se_id = se;
        
        // Initialize compute units
        for (uint32_t cu = 0; cu < RDNA2ShaderEngine::CUS_PER_SE; ++cu) {
            auto& compute_unit = shader_engine.compute_units[cu];
            compute_unit.cu_id = se * RDNA2ShaderEngine::CUS_PER_SE + cu;
            compute_unit.busy = false;
            
            // Initialize wavefronts
            for (uint32_t wf = 0; wf < 32; ++wf) {
                compute_unit.wavefronts[wf].active = false;
                compute_unit.wavefronts[wf].wave_id = wf;
                compute_unit.wavefronts[wf].pc = 0;
                compute_unit.wavefronts[wf].exec_mask = 0xFFFFFFFFFFFFFFFFULL;
            }
            
            // Clear ALU units and registers
            memset(compute_unit.alu_units.data(), 0, sizeof(compute_unit.alu_units));
            memset(compute_unit.vector_registers, 0, sizeof(compute_unit.vector_registers));
            memset(compute_unit.scalar_registers.data(), 0, sizeof(compute_unit.scalar_registers));
            memset(compute_unit.lds.data(), 0, sizeof(compute_unit.lds));
        }
        
        // Initialize geometry processor
        shader_engine.geometry_processor.primitive_assembly_active = false;
        shader_engine.geometry_processor.vertex_count = 0;
        shader_engine.geometry_processor.primitive_count = 0;
        
        // Initialize rasterizer
        shader_engine.rasterizer.hierarchical_z_enabled = true;
        shader_engine.rasterizer.tile_buffers.clear();
    }
}

void GPU::submit(const std::vector<Command>& commands) {
    std::cout << "GPU: Processing " << commands.size() << " RDNA2 commands" << std::endl;
    
    for (const auto& cmd : commands) {
        switch (cmd.opcode) {
            case DRAW_INDEX_AUTO:
            case DRAW_INDEX_2:
            case DRAW_INDIRECT:
                if (graphics_pipeline) {
                    execute_graphics_command(cmd);
                }
                break;
                
            case DISPATCH_DIRECT:
            case DISPATCH_INDIRECT:
            case DISPATCH_COMPUTE:
                if (compute_pipeline) {
                    execute_compute_command(cmd);
                }
                break;
                
            case SET_CONTEXT_REG:
            case SET_CONFIG_REG:
            case SET_SH_REG:
                // Handle register writes
                std::cout << "GPU: Setting register 0x" << std::hex << cmd.arg0 
                         << " = 0x" << std::hex << cmd.arg1 << std::dec << std::endl;
                break;
                
            case DMA_DATA:
            case COPY_DATA:
                // Handle memory operations
                std::cout << "GPU: Memory operation from 0x" << std::hex << cmd.arg0 
                         << " to 0x" << std::hex << cmd.arg1 << " size " << std::dec << cmd.arg2 << std::endl;
                break;
                
            case EVENT_WRITE:
            case EVENT_WRITE_EOP:
                // Handle synchronization events
                std::cout << "GPU: Event write " << cmd.arg0 << std::endl;
                break;
                
            default:
                std::cout << "GPU: Unknown command opcode 0x" << std::hex << cmd.opcode << std::dec << std::endl;
                break;
        }
    }
    
    // Update performance counters
    perf_counters.compute_dispatches += commands.size();
}

void GPU::execute_graphics_command(const Command& cmd) {
    if (!frame_state.in_frame) {
        std::cout << "GPU: Warning - Graphics command outside of frame" << std::endl;
        BeginFrame();
    }
    
    switch (cmd.opcode) {
        case DRAW_INDEX_AUTO: {
            uint32_t vertex_count = static_cast<uint32_t>(cmd.arg0);
            uint32_t instance_count = static_cast<uint32_t>(cmd.arg1);
            
            if (graphics_pipeline) {
                graphics_pipeline->Draw(vertex_count, instance_count, 0, 0);
            } else {
                process_draw_call(vertex_count, instance_count);
            }
            break;
        }
        
        case DRAW_INDEX_2: {
            uint32_t index_count = static_cast<uint32_t>(cmd.arg0);
            uint32_t instance_count = static_cast<uint32_t>(cmd.arg1);
            uint64_t index_buffer_addr = cmd.arg2;
            
            graphics_state.index_buffer_address = index_buffer_addr;
            
            if (graphics_pipeline) {
                graphics_pipeline->DrawIndexed(index_count, instance_count, 0, 0, 0);
            } else {
                process_draw_call(index_count, instance_count);
            }
            break;
        }
        
        case DRAW_INDIRECT: {
            uint64_t indirect_buffer = cmd.arg0;
            uint32_t draw_count = static_cast<uint32_t>(cmd.arg1);
            uint32_t stride = static_cast<uint32_t>(cmd.arg2);
            
            if (graphics_pipeline) {
                graphics_pipeline->DrawIndirect(indirect_buffer, draw_count, stride);
            } else {
                uint32_t* params = reinterpret_cast<uint32_t*>(get_gpu_memory_ptr(indirect_buffer));
                if (params) {
                    process_draw_call(params[0], params[1]);
                }
            }
            break;
        }
    }
}

void GPU::execute_compute_command(const Command& cmd) {
    switch (cmd.opcode) {
        case DISPATCH_DIRECT: {
            uint32_t group_x = static_cast<uint32_t>(cmd.arg0);
            uint32_t group_y = static_cast<uint32_t>(cmd.arg1);
            uint32_t group_z = static_cast<uint32_t>(cmd.arg2);
            
            if (compute_pipeline) {
                compute_pipeline->Dispatch(group_x, group_y, group_z);
            } else {
                dispatch_compute_shader(group_x, group_y, group_z);
            }
            break;
        }
        
        case SET_COMPUTE_SHADER: {
            compute_state.compute_shader_id = static_cast<uint32_t>(cmd.arg0);
            compute_state.thread_group_size[0] = static_cast<uint32_t>(cmd.arg1 & 0xFFFF);
            compute_state.thread_group_size[1] = static_cast<uint32_t>((cmd.arg1 >> 16) & 0xFFFF);
            compute_state.thread_group_size[2] = static_cast<uint32_t>(cmd.arg2 & 0xFFFF);
            break;
        }
    }
}

void GPU::process_draw_call(uint32_t vertex_count, uint32_t instance_count) {
    std::cout << "GPU: Processing production-quality draw call - " << vertex_count << " vertices, " 
              << instance_count << " instances" << std::endl;
    
    // Process vertex data through vertex shader pipeline
    std::vector<ProcessedVertex> processed_vertices;
    processed_vertices.reserve(vertex_count);
    
    // Execute vertex shader for each vertex
    for (uint32_t v = 0; v < vertex_count; ++v) {
        ProcessedVertex vertex = execute_vertex_shader(v);
        processed_vertices.push_back(vertex);
    }
    
    // Perform primitive assembly
    std::vector<AssembledPrimitive> primitives = assemble_primitives(processed_vertices);
    
    // Perform clipping and culling
    std::vector<AssembledPrimitive> visible_primitives = clip_and_cull_primitives(primitives);
    
    // Bin primitives to tiles for tile-based rendering
    bin_primitives_to_tiles_production(visible_primitives);
    
    // Process tiles across shader engines with full pipeline
    const uint32_t tile_size = 64;
    const uint32_t screen_width = 1920;
    const uint32_t screen_height = 1080;
    uint32_t tiles_x = (screen_width + tile_size - 1) / tile_size;
    uint32_t tiles_y = (screen_height + tile_size - 1) / tile_size;
    
    // Parallel tile processing across shader engines
    std::vector<std::thread> tile_threads;
    
    for (uint32_t se = 0; se < SHADER_ENGINE_COUNT; ++se) {
        tile_threads.emplace_back([this, se, tiles_x, tiles_y, tile_size]() {
            for (uint32_t ty = se; ty < tiles_y; ty += SHADER_ENGINE_COUNT) {
                for (uint32_t tx = 0; tx < tiles_x; ++tx) {
                    uint64_t tile_id = (static_cast<uint64_t>(ty) << 32) | tx;
                    
                    auto& tile_buffers = shader_engines[se].rasterizer.tile_buffers;
                    auto it = tile_buffers.find(tile_id);
                    if (it != tile_buffers.end()) {
                        process_tile_production(tx, ty, it->second, se);
                    }
                }
            }
        });
    }
    
    // Wait for all tile processing to complete
    for (auto& thread : tile_threads) {
        thread.join();
    }
    
    perf_counters.triangles_rendered += visible_primitives.size();
    
    std::cout << "GPU: Production draw call complete - " << visible_primitives.size() << " visible primitives, "
              << perf_counters.tiles_processed << " tiles processed" << std::endl;
}

GPU::ProcessedVertex GPU::execute_vertex_shader(uint32_t vertex_index) {
    ProcessedVertex vertex;
    
    auto* vertex_shader = get_compiled_shader(graphics_state.vertex_shader_id);
    if (!vertex_shader) {
        // Default vertex processing
        float angle = (static_cast<float>(vertex_index) / 3.0f) * 2.0f * 3.14159f;
        vertex.position[0] = std::cos(angle) * 0.5f;
        vertex.position[1] = std::sin(angle) * 0.5f;
        vertex.position[2] = 0.0f;
        vertex.position[3] = 1.0f;
        
        vertex.attributes[0] = vertex.position[0] * 0.5f + 0.5f; // UV.x
        vertex.attributes[1] = vertex.position[1] * 0.5f + 0.5f; // UV.y
        vertex.attributes[2] = 1.0f; // Color.r
        vertex.attributes[3] = 1.0f; // Color.g
        vertex.attributes[4] = 1.0f; // Color.b
        vertex.attributes[5] = 1.0f; // Color.a
        
        return vertex;
    }
    
    // Execute vertex shader on available compute unit
    for (auto& se : shader_engines) {
        for (auto& cu : se.compute_units) {
            if (!cu.busy) {
                cu.busy = true;
                
                // Find available wavefront
                for (auto& wf : cu.wavefronts) {
                    if (!wf.active) {
                        wf.active = true;
                        
                        // Execute vertex shader bytecode
                        execute_shader_wavefront(cu, wf, *vertex_shader, vertex_index);
                        
                        // Extract results from registers
                        vertex.position[0] = cu.vector_registers[0][0];
                        vertex.position[1] = cu.vector_registers[0][1];
                        vertex.position[2] = cu.vector_registers[0][2];
                        vertex.position[3] = cu.vector_registers[0][3];
                        
                        for (int i = 0; i < 16; ++i) {
                            vertex.attributes[i] = cu.vector_registers[1 + i / 4][i % 4];
                        }
                        
                        wf.active = false;
                        cu.busy = false;
                        return vertex;
                    }
                }
                
                cu.busy = false;
                break;
            }
        }
    }
    
    return vertex;
}

void GPU::execute_shader_wavefront(RDNA2ComputeUnit& cu, RDNA2ComputeUnit::Wavefront& wf, 
                                  const CompiledShader& shader, uint32_t thread_id) {
    wf.pc = 0;
    
    // Set up input registers
    cu.scalar_registers[0] = thread_id;
    cu.scalar_registers[1] = graphics_state.vertex_buffer_address;
    cu.scalar_registers[2] = graphics_state.constant_buffer_address;
    
    // Execute shader instructions
    while (wf.pc < shader.bytecode.size() * 4 && wf.pc < 1000) { // Safety limit
        uint32_t instruction = 0;
        if (wf.pc / 4 < shader.bytecode.size()) {
            instruction = shader.bytecode[wf.pc / 4];
        }
        
        // Decode and execute instruction
        uint32_t opcode = (instruction >> 26) & 0x3F;
        uint32_t dst = (instruction >> 21) & 0x1F;
        uint32_t src0 = (instruction >> 16) & 0x1F;
        uint32_t src1 = (instruction >> 11) & 0x1F;
        uint32_t immediate = instruction & 0x7FF;
        
        switch (opcode) {
            case 0x01: // V_ADD_F32
                for (int lane = 0; lane < 64; ++lane) {
                    if (wf.exec_mask & (1ULL << lane)) {
                        cu.vector_registers[dst][lane] = cu.vector_registers[src0][lane] + cu.vector_registers[src1][lane];
                    }
                }
                break;
                
            case 0x02: // V_MUL_F32
                for (int lane = 0; lane < 64; ++lane) {
                    if (wf.exec_mask & (1ULL << lane)) {
                        cu.vector_registers[dst][lane] = cu.vector_registers[src0][lane] * cu.vector_registers[src1][lane];
                    }
                }
                break;
                
            case 0x03: // V_MAD_F32 (Multiply-Add)
                for (int lane = 0; lane < 64; ++lane) {
                    if (wf.exec_mask & (1ULL << lane)) {
                        cu.vector_registers[dst][lane] = cu.vector_registers[src0][lane] * cu.vector_registers[src1][lane] + cu.vector_registers[dst][lane];
                    }
                }
                break;
                
            case 0x04: // S_LOAD_DWORD (Scalar load)
                if (src0 < 256) {
                    uint64_t address = cu.scalar_registers[src0] + immediate * 4;
                    uint32_t* data = reinterpret_cast<uint32_t*>(get_gpu_memory_ptr(address));
                    if (data && dst < 256) {
                        cu.scalar_registers[dst] = *data;
                    }
                }
                break;
                
            case 0x05: // V_MOV_B32
                for (int lane = 0; lane < 64; ++lane) {
                    if (wf.exec_mask & (1ULL << lane)) {
                        cu.vector_registers[dst][lane] = cu.vector_registers[src0][lane];
                    }
                }
                break;
                
            case 0x10: // BUFFER_LOAD_FORMAT_XYZW
                {
                    uint64_t buffer_addr = cu.scalar_registers[src0];
                    uint32_t offset = cu.scalar_registers[src1];
                    float* buffer_data = reinterpret_cast<float*>(get_gpu_memory_ptr(buffer_addr + offset * 16));
                    
                    if (buffer_data) {
                        for (int lane = 0; lane < 64; ++lane) {
                            if (wf.exec_mask & (1ULL << lane)) {
                                cu.vector_registers[dst][lane] = buffer_data[0];     // X
                                cu.vector_registers[dst + 1][lane] = buffer_data[1]; // Y
                                cu.vector_registers[dst + 2][lane] = buffer_data[2]; // Z
                                cu.vector_registers[dst + 3][lane] = buffer_data[3]; // W
                            }
                        }
                    }
                }
                break;
                
            case 0x3F: // S_ENDPGM (End program)
                return;
                
            default:
                // Unknown instruction, skip
                break;
        }
        
        wf.pc += 4;
    }
}

std::vector<GPU::AssembledPrimitive> GPU::assemble_primitives(const std::vector<ProcessedVertex>& vertices) {
    std::vector<AssembledPrimitive> primitives;
    
    uint32_t primitive_topology = graphics_state.primitive_topology;
    
    switch (primitive_topology) {
        case 0: // Triangle list
            for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                AssembledPrimitive prim;
                prim.vertices[0] = vertices[i];
                prim.vertices[1] = vertices[i + 1];
                prim.vertices[2] = vertices[i + 2];
                prim.primitive_type = 0; // Triangle
                primitives.push_back(prim);
            }
            break;
            
        case 1: // Triangle strip
            for (size_t i = 0; i + 2 < vertices.size(); ++i) {
                AssembledPrimitive prim;
                if (i % 2 == 0) {
                    prim.vertices[0] = vertices[i];
                    prim.vertices[1] = vertices[i + 1];
                    prim.vertices[2] = vertices[i + 2];
                } else {
                    prim.vertices[0] = vertices[i];
                    prim.vertices[1] = vertices[i + 2];
                    prim.vertices[2] = vertices[i + 1];
                }
                prim.primitive_type = 0; // Triangle
                primitives.push_back(prim);
            }
            break;
            
        case 2: // Triangle fan
            if (vertices.size() >= 3) {
                for (size_t i = 1; i + 1 < vertices.size(); ++i) {
                    AssembledPrimitive prim;
                    prim.vertices[0] = vertices[0];
                    prim.vertices[1] = vertices[i];
                    prim.vertices[2] = vertices[i + 1];
                    prim.primitive_type = 0; // Triangle
                    primitives.push_back(prim);
                }
            }
            break;
    }
    
    return primitives;
}

std::vector<GPU::AssembledPrimitive> GPU::clip_and_cull_primitives(const std::vector<AssembledPrimitive>& primitives) {
    std::vector<AssembledPrimitive> visible_primitives;
    
    for (const auto& prim : primitives) {
        // Frustum culling
        bool outside_frustum = false;
        for (int v = 0; v < 3; ++v) {
            const auto& vertex = prim.vertices[v];
            float w = vertex.position[3];
            
            // Check if vertex is outside view frustum
            if (vertex.position[0] < -w || vertex.position[0] > w ||
                vertex.position[1] < -w || vertex.position[1] > w ||
                vertex.position[2] < 0.0f || vertex.position[2] > w) {
                // Vertex is outside, but we still need to check if triangle intersects
                // For simplicity, we'll do conservative culling
                // TODO: Implement proper frustum intersection test
            }
        }
        
        // Back-face culling
        if (graphics_state.cull_mode != 0) {
            // Calculate triangle normal in screen space
            float v0[2] = {prim.vertices[0].position[0] / prim.vertices[0].position[3],
                          prim.vertices[0].position[1] / prim.vertices[0].position[3]};
            float v1[2] = {prim.vertices[1].position[0] / prim.vertices[1].position[3],
                          prim.vertices[1].position[1] / prim.vertices[1].position[3]};
            float v2[2] = {prim.vertices[2].position[0] / prim.vertices[2].position[3],
                          prim.vertices[2].position[1] / prim.vertices[2].position[3]};
            
            // Calculate signed area (cross product)
            float signed_area = (v1[0] - v0[0]) * (v2[1] - v0[1]) - (v1[1] - v0[1]) * (v2[0] - v0[0]);
            
            if (graphics_state.cull_mode == 1 && signed_area > 0) { // Cull front faces
                perf_counters.primitives_culled++;
                continue;
            }
            if (graphics_state.cull_mode == 2 && signed_area < 0) { // Cull back faces
                perf_counters.primitives_culled++;
                continue;
            }
        }
        
        // Degenerate triangle culling
        float area_threshold = 1e-6f;
        float v0[2] = {prim.vertices[0].position[0] / prim.vertices[0].position[3],
                      prim.vertices[0].position[1] / prim.vertices[0].position[3]};
        float v1[2] = {prim.vertices[1].position[0] / prim.vertices[1].position[3],
                      prim.vertices[1].position[1] / prim.vertices[1].position[3]};
        float v2[2] = {prim.vertices[2].position[0] / prim.vertices[2].position[3],
                      prim.vertices[2].position[1] / prim.vertices[2].position[3]};
        
        float area = std::abs((v1[0] - v0[0]) * (v2[1] - v0[1]) - (v1[1] - v0[1]) * (v2[0] - v0[0]));
        if (area < area_threshold) {
            perf_counters.primitives_culled++;
            continue;
        }
        
        visible_primitives.push_back(prim);
    }
    
    return visible_primitives;
}

void GPU::bin_primitives_to_tiles_production(const std::vector<AssembledPrimitive>& primitives) {
    if (!advanced_features.tile_based_rendering_enabled) {
        return;
    }
    
    const uint32_t tile_size = 64;
    const uint32_t screen_width = 1920;
    const uint32_t screen_height = 1080;
    
    uint32_t tiles_x = (screen_width + tile_size - 1) / tile_size;
    uint32_t tiles_y = (screen_height + tile_size - 1) / tile_size;
    
    // Clear existing tile buffers
    for (auto& se : shader_engines) {
        se.rasterizer.tile_buffers.clear();
    }
    
    // Bin each primitive to overlapping tiles
    for (size_t prim_idx = 0; prim_idx < primitives.size(); ++prim_idx) {
        const auto& prim = primitives[prim_idx];
        
        // Convert vertices to screen space
        float screen_vertices[3][2];
        for (int v = 0; v < 3; ++v) {
            float w = prim.vertices[v].position[3];
            screen_vertices[v][0] = ((prim.vertices[v].position[0] / w) + 1.0f) * screen_width * 0.5f;
            screen_vertices[v][1] = ((prim.vertices[v].position[1] / w) + 1.0f) * screen_height * 0.5f;
        }
        
        // Calculate precise bounding box
        float min_x = std::min({screen_vertices[0][0], screen_vertices[1][0], screen_vertices[2][0]});
        float max_x = std::max({screen_vertices[0][0], screen_vertices[1][0], screen_vertices[2][0]});
        float min_y = std::min({screen_vertices[0][1], screen_vertices[1][1], screen_vertices[2][1]});
        float max_y = std::max({screen_vertices[0][1], screen_vertices[1][1], screen_vertices[2][1]});
        
        // Convert to tile coordinates
        uint32_t min_tile_x = static_cast<uint32_t>(std::max(0.0f, std::floor(min_x))) / tile_size;
        uint32_t max_tile_x = static_cast<uint32_t>(std::min(static_cast<float>(screen_width - 1), std::ceil(max_x))) / tile_size;
        uint32_t min_tile_y = static_cast<uint32_t>(std::max(0.0f, std::floor(min_y))) / tile_size;
        uint32_t max_tile_y = static_cast<uint32_t>(std::min(static_cast<float>(screen_height - 1), std::ceil(max_y))) / tile_size;
        
        min_tile_x = std::min(min_tile_x, tiles_x - 1);
        max_tile_x = std::min(max_tile_x, tiles_x - 1);
        min_tile_y = std::min(min_tile_y, tiles_y - 1);
        max_tile_y = std::min(max_tile_y, tiles_y - 1);
        
        // Add primitive to overlapping tiles with triangle-tile intersection test
        for (uint32_t ty = min_tile_y; ty <= max_tile_y; ++ty) {
            for (uint32_t tx = min_tile_x; tx <= max_tile_x; ++tx) {
                // Test if triangle actually intersects tile
                float tile_min_x = tx * tile_size;
                float tile_max_x = (tx + 1) * tile_size;
                float tile_min_y = ty * tile_size;
                float tile_max_y = (ty + 1) * tile_size;
                
                if (triangle_intersects_tile(screen_vertices, tile_min_x, tile_max_x, tile_min_y, tile_max_y)) {
                    uint64_t tile_id = (static_cast<uint64_t>(ty) << 32) | tx;
                    uint32_t se_index = (tx + ty) % SHADER_ENGINE_COUNT;
                    
                    auto& tile_buffer = shader_engines[se_index].rasterizer.tile_buffers[tile_id];
                    tile_buffer.primitive_ids.push_back(static_cast<uint32_t>(prim_idx));
                    tile_buffer.primitives.push_back(prim);
                    
                    // Initialize hierarchical Z if needed
                    if (tile_buffer.z_buffer_hierarchy[0].empty()) {
                        initialize_tile_hierarchical_z(tile_buffer);
                    }
                }
            }
        }
    }
    
    perf_counters.tiles_processed += tiles_x * tiles_y;
    
    std::cout << "GPU: Production binned " << primitives.size() << " primitives to " 
              << tiles_x << "x" << tiles_y << " tiles" << std::endl;
}

bool GPU::triangle_intersects_tile(float vertices[3][2], float tile_min_x, float tile_max_x, 
                                  float tile_min_y, float tile_max_y) {
    // Test if any triangle vertex is inside tile
    for (int v = 0; v < 3; ++v) {
        if (vertices[v][0] >= tile_min_x && vertices[v][0] < tile_max_x &&
            vertices[v][1] >= tile_min_y && vertices[v][1] < tile_max_y) {
            return true;
        }
    }
    
    // Test if any tile corner is inside triangle
    float tile_corners[4][2] = {
        {tile_min_x, tile_min_y},
        {tile_max_x, tile_min_y},
        {tile_max_x, tile_max_y},
        {tile_min_x, tile_max_y}
    };
    
    for (int c = 0; c < 4; ++c) {
        if (point_in_triangle(tile_corners[c], vertices)) {
            return true;
        }
    }
    
    // Test if any triangle edge intersects tile edges
    float tile_edges[4][4] = {
        {tile_min_x, tile_min_y, tile_max_x, tile_min_y}, // Bottom
        {tile_max_x, tile_min_y, tile_max_x, tile_max_y}, // Right
        {tile_max_x, tile_max_y, tile_min_x, tile_max_y}, // Top
        {tile_min_x, tile_max_y, tile_min_x, tile_min_y}  // Left
    };
    
    for (int t = 0; t < 3; ++t) {
        int next_t = (t + 1) % 3;
        for (int e = 0; e < 4; ++e) {
            if (line_segments_intersect(vertices[t][0], vertices[t][1], vertices[next_t][0], vertices[next_t][1],
                                       tile_edges[e][0], tile_edges[e][1], tile_edges[e][2], tile_edges[e][3])) {
                return true;
            }
        }
    }
    
    return false;
}

bool GPU::point_in_triangle(float point[2], float triangle[3][2]) {
    float v0[2] = {triangle[2][0] - triangle[0][0], triangle[2][1] - triangle[0][1]};
    float v1[2] = {triangle[1][0] - triangle[0][0], triangle[1][1] - triangle[0][1]};
    float v2[2] = {point[0] - triangle[0][0], point[1] - triangle[0][1]};
    
    float dot00 = v0[0] * v0[0] + v0[1] * v0[1];
    float dot01 = v0[0] * v1[0] + v0[1] * v1[1];
    float dot02 = v0[0] * v2[0] + v0[1] * v2[1];
    float dot11 = v1[0] * v1[0] + v1[1] * v1[1];
    float dot12 = v1[0] * v2[0] + v1[1] * v2[1];
    
    float inv_denom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    float u = (dot11 * dot02 - dot01 * dot12) * inv_denom;
    float v = (dot00 * dot12 - dot01 * dot02) * inv_denom;
    
    return (u >= 0) && (v >= 0) && (u + v <= 1);
}

bool GPU::line_segments_intersect(float x1, float y1, float x2, float y2, 
                                 float x3, float y3, float x4, float y4) {
    float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) < 1e-6f) return false; // Parallel lines
    
    float t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    float u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;
    
    return (t >= 0 && t <= 1 && u >= 0 && u <= 1);
}

void GPU::initialize_tile_hierarchical_z(RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer) {
    const uint32_t tile_size = 64;
    
    for (uint32_t level = 0; level < advanced_features.hierarchical_z_levels; ++level) {
        uint32_t level_size = tile_size >> level;
        if (level_size == 0) level_size = 1;
        
        tile_buffer.z_buffer_hierarchy[level].resize(level_size * level_size, 1.0f);
        tile_buffer.z_min_hierarchy[level].resize(level_size * level_size, 1.0f);
        tile_buffer.z_max_hierarchy[level].resize(level_size * level_size, 0.0f);
    }
    
    tile_buffer.early_z_reject_enabled = true;
    tile_buffer.visible_primitive_count = 0;
}

void GPU::process_tile_production(uint32_t tile_x, uint32_t tile_y, 
                                 RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer, uint32_t se_index) {
    const uint32_t tile_size = 64;
    
    // Sort primitives by depth for optimal hierarchical Z performance
    std::sort(tile_buffer.primitives.begin(), tile_buffer.primitives.end(),
              [](const AssembledPrimitive& a, const AssembledPrimitive& b) {
                  float depth_a = (a.vertices[0].position[2] + a.vertices[1].position[2] + a.vertices[2].position[2]) / 3.0f;
                  float depth_b = (b.vertices[0].position[2] + b.vertices[1].position[2] + b.vertices[2].position[2]) / 3.0f;
                  return depth_a < depth_b; // Front to back
              });
    
    // Process each primitive in the tile
    for (size_t prim_idx = 0; prim_idx < tile_buffer.primitives.size(); ++prim_idx) {
        const auto& prim = tile_buffer.primitives[prim_idx];
        
        // Calculate primitive depth bounds
        float z_min = std::min({prim.vertices[0].position[2], prim.vertices[1].position[2], prim.vertices[2].position[2]});
        float z_max = std::max({prim.vertices[0].position[2], prim.vertices[1].position[2], prim.vertices[2].position[2]});
        
        // Hierarchical Z test for early rejection
        if (hierarchical_z_test_production(tile_x, tile_y, z_min, z_max, tile_buffer)) {
            tile_buffer.visible_primitive_count++;
            
            // Rasterize primitive within tile
            rasterize_triangle_production(tile_x, tile_y, prim, tile_buffer, se_index);
            
            // Update hierarchical Z
            update_hierarchical_z_production(tile_x, tile_y, z_min, tile_buffer);
        } else {
            perf_counters.primitives_culled++;
            perf_counters.hierarchical_z_rejects++;
        }
    }
}

void GPU::rasterize_triangle_production(uint32_t tile_x, uint32_t tile_y, const AssembledPrimitive& prim,
                                       RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer, uint32_t se_index) {
    const uint32_t tile_size = 64;
    const uint32_t screen_width = 1920;
    const uint32_t screen_height = 1080;
    
    // Convert vertices to screen space
    float screen_vertices[3][4];
    for (int v = 0; v < 3; ++v) {
        float w = prim.vertices[v].position[3];
        screen_vertices[v][0] = ((prim.vertices[v].position[0] / w) + 1.0f) * screen_width * 0.5f;
        screen_vertices[v][1] = ((prim.vertices[v].position[1] / w) + 1.0f) * screen_height * 0.5f;
        screen_vertices[v][2] = prim.vertices[v].position[2] / w; // Depth
        screen_vertices[v][3] = 1.0f / w; // 1/w for perspective correction
    }
    
    // Calculate edge equations
    float edge_equations[3][3];
    for (int e = 0; e < 3; ++e) {
        int v0 = e;
        int v1 = (e + 1) % 3;
        
        edge_equations[e][0] = screen_vertices[v0][1] - screen_vertices[v1][1]; // A
        edge_equations[e][1] = screen_vertices[v1][0] - screen_vertices[v0][0]; // B
        edge_equations[e][2] = screen_vertices[v0][0] * screen_vertices[v1][1] - screen_vertices[v1][0] * screen_vertices[v0][1]; // C
    }
    
    // Calculate triangle area for barycentric coordinates
    float triangle_area = 0.5f * std::abs(edge_equations[0][0] * screen_vertices[2][0] + 
                                         edge_equations[0][1] * screen_vertices[2][1] + 
                                         edge_equations[0][2]);
    
    if (triangle_area < 1e-6f) return; // Degenerate triangle
    
    // Rasterize pixels within tile
    uint32_t tile_start_x = tile_x * tile_size;
    uint32_t tile_start_y = tile_y * tile_size;
    uint32_t tile_end_x = std::min(tile_start_x + tile_size, screen_width);
    uint32_t tile_end_y = std::min(tile_start_y + tile_size, screen_height);
    
    for (uint32_t y = tile_start_y; y < tile_end_y; ++y) {
        for (uint32_t x = tile_start_x; x < tile_end_x; ++x) {
            // Evaluate edge equations
            float edge_values[3];
            bool inside = true;
            
            for (int e = 0; e < 3; ++e) {
                edge_values[e] = edge_equations[e][0] * x + edge_equations[e][1] * y + edge_equations[e][2];
                if (edge_values[e] < 0) {
                    inside = false;
                    break;
                }
            }
            
            if (inside) {
                // Calculate barycentric coordinates
                float bary[3];
                bary[0] = edge_values[1] / (2.0f * triangle_area);
                bary[1] = edge_values[2] / (2.0f * triangle_area);
                bary[2] = 1.0f - bary[0] - bary[1];
                
                // Interpolate depth with perspective correction
                float inv_w = bary[0] * screen_vertices[0][3] + bary[1] * screen_vertices[1][3] + bary[2] * screen_vertices[2][3];
                float depth = (bary[0] * screen_vertices[0][2] * screen_vertices[0][3] + 
                              bary[1] * screen_vertices[1][2] * screen_vertices[1][3] + 
                              bary[2] * screen_vertices[2][2] * screen_vertices[2][3]) / inv_w;
                
                // Depth test
                if (depth_test_production(x, y, depth)) {
                    // Interpolate vertex attributes
                    FragmentInput fragment;
                    fragment.screen_pos[0] = x;
                    fragment.screen_pos[1] = y;
                    fragment.depth = depth;
                    
                    // Perspective-correct attribute interpolation
                    for (int attr = 0; attr < 16; ++attr) {
                        fragment.attributes[attr] = (bary[0] * prim.vertices[0].attributes[attr] * screen_vertices[0][3] +
                                                   bary[1] * prim.vertices[1].attributes[attr] * screen_vertices[1][3] +
                                                   bary[2] * prim.vertices[2].attributes[attr] * screen_vertices[2][3]) / inv_w;
                    }
                    
                    // Execute pixel shader
                    execute_pixel_shader_production(fragment, se_index);
                    perf_counters.pixels_shaded++;
                }
            }
        }
    }
}

bool GPU::depth_test_production(uint32_t x, uint32_t y, float depth) {
    if (!frame_state.depth_target_bound) {
        return true; // No depth testing
    }
    
    auto& depth_target = render_backends[0].depth_target;
    uint64_t depth_addr = depth_target.base_address + (y * depth_target.pitch + x * 4);
    
    if (depth_addr >= GPU_MEMORY_SIZE) {
        return false;
    }
    
    float* depth_buffer = reinterpret_cast<float*>(gpu_memory.get() + depth_addr);
    float existing_depth = *depth_buffer;
    
    // Depth comparison based on depth function
    bool depth_pass = false;
    switch (graphics_state.depth_func) {
        case 0: // Never
            depth_pass = false;
            break;
        case 1: // Less
            depth_pass = depth < existing_depth;
            break;
        case 2: // Equal
            depth_pass = std::abs(depth - existing_depth) < 1e-6f;
            break;
        case 3: // Less or equal
            depth_pass = depth <= existing_depth;
            break;
        case 4: // Greater
            depth_pass = depth > existing_depth;
            break;
        case 5: // Not equal
            depth_pass = std::abs(depth - existing_depth) >= 1e-6f;
            break;
        case 6: // Greater or equal
            depth_pass = depth >= existing_depth;
            break;
        case 7: // Always
        default:
            depth_pass = true;
            break;
    }
    
    if (depth_pass && graphics_state.depth_write_enable) {
        *depth_buffer = depth;
    }
    
    return depth_pass;
}

void GPU::execute_pixel_shader_production(const FragmentInput& fragment, uint32_t se_index) {
    auto* pixel_shader = get_compiled_shader(graphics_state.pixel_shader_id);
    if (!pixel_shader) {
        // Default pixel shader - output interpolated color
        write_pixel_production(fragment.screen_pos[0], fragment.screen_pos[1], 
                             fragment.attributes[2], fragment.attributes[3], 
                             fragment.attributes[4], fragment.attributes[5]);
        return;
    }
    
    // Find available compute unit for pixel shader execution
    auto& shader_engine = shader_engines[se_index];
    for (auto& cu : shader_engine.compute_units) {
        if (!cu.busy) {
            cu.busy = true;
            
            // Find available wavefront
            for (auto& wf : cu.wavefronts) {
                if (!wf.active) {
                    wf.active = true;
                    
                    // Set up input registers with fragment data
                    for (int attr = 0; attr < 16; ++attr) {
                        cu.vector_registers[attr][0] = fragment.attributes[attr];
                    }
                    cu.vector_registers[16][0] = fragment.screen_pos[0];
                    cu.vector_registers[17][0] = fragment.screen_pos[1];
                    cu.vector_registers[18][0] = fragment.depth;
                    
                    // Execute pixel shader
                    execute_shader_wavefront(cu, wf, *pixel_shader, 0);
                    
                    // Extract output color from registers
                    float r = cu.vector_registers[0][0];
                    float g = cu.vector_registers[1][0];
                    float b = cu.vector_registers[2][0];
                    float a = cu.vector_registers[3][0];
                    
                    // Write pixel with blending
                    write_pixel_production(fragment.screen_pos[0], fragment.screen_pos[1], r, g, b, a);
                    
                    wf.active = false;
                    cu.busy = false;
                    return;
                }
            }
            
            cu.busy = false;
            break;
        }
    }
}

void GPU::write_pixel_production(uint32_t x, uint32_t y, float r, float g, float b, float a) {
    if (!(frame_state.active_render_targets & 1)) {
        return; // No render target bound
    }
    
    auto& color_target = render_backends[0].color_targets[0];
    uint64_t pixel_addr = color_target.base_address + (y * color_target.pitch + x * 4);
    
    if (pixel_addr >= GPU_MEMORY_SIZE) {
        return;
    }
    
    // Clamp color values
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));
    a = std::max(0.0f, std::min(1.0f, a));
    
    uint32_t* pixel = reinterpret_cast<uint32_t*>(gpu_memory.get() + pixel_addr);
    
    if (graphics_state.blend_enable) {
        // Read existing pixel for blending
        uint32_t existing = *pixel;
        float dst_r = ((existing >> 0) & 0xFF) / 255.0f;
        float dst_g = ((existing >> 8) & 0xFF) / 255.0f;
        float dst_b = ((existing >> 16) & 0xFF) / 255.0f;
        float dst_a = ((existing >> 24) & 0xFF) / 255.0f;
        
        // Apply blend equation (simplified alpha blending)
        // TODO: Implement proper blend equation
        switch (graphics_state.blend_op) {
            case 0: // Add
                r = r * a + dst_r * (1.0f - a);
                g = g * a + dst_g * (1.0f - a);
                b = b * a + dst_b * (1.0f - a);
                a = a + dst_a * (1.0f - a);
                break;
            case 1: // Subtract
                r = dst_r - r * a;
                g = dst_g - g * a;
                b = dst_b - b * a;
                break;
            case 2: // Multiply
                r = r * dst_r;
                g = g * dst_g;
                b = b * dst_b;
                a = a * dst_a;
                break;
        }
        
        // Clamp again after blending
        r = std::max(0.0f, std::min(1.0f, r));
        g = std::max(0.0f, std::min(1.0f, g));
        b = std::max(0.0f, std::min(1.0f, b));
        a = std::max(0.0f, std::min(1.0f, a));
    }
    
    // Convert to RGBA8 format
    uint32_t final_color = (static_cast<uint32_t>(a * 255.0f) << 24) |
                          (static_cast<uint32_t>(b * 255.0f) << 16) |
                          (static_cast<uint32_t>(g * 255.0f) << 8) |
                          (static_cast<uint32_t>(r * 255.0f) << 0);
    
    *pixel = final_color;
}

bool GPU::hierarchical_z_test_production(uint32_t tile_x, uint32_t tile_y, float z_min, float z_max,
                                        RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer) {
    if (!advanced_features.hierarchical_z_enabled) {
        return true;
    }
    
    // Test from coarsest to finest level
    for (uint32_t level = advanced_features.hierarchical_z_levels - 1; level > 0; --level) {
        uint32_t level_size = 64 >> level;
        if (level_size == 0) continue;
        
        auto& z_min_buffer = tile_buffer.z_min_hierarchy[level];
        auto& z_max_buffer = tile_buffer.z_max_hierarchy[level];
        
        if (z_min_buffer.empty()) continue;
        
        // Calculate index in this level
        uint32_t level_x = (tile_x % level_size);
        uint32_t level_y = (tile_y % level_size);
        uint32_t index = level_y * level_size + level_x;
        
        if (index < z_min_buffer.size()) {
            float existing_z_min = z_min_buffer[index];
            float existing_z_max = z_max_buffer[index];
            
            // Early reject if primitive is completely behind existing geometry
            if (z_min > existing_z_max) {
                return false;
            }
            
            // Early accept if primitive is completely in front
            if (z_max < existing_z_min) {
                return true;
            }
        }
    }
    
    return true; // Continue to pixel-level testing
}

void GPU::update_hierarchical_z_production(uint32_t tile_x, uint32_t tile_y, float depth,
                                          RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer) {
    if (!advanced_features.hierarchical_z_enabled) {
        return;
    }
    
    // Update all hierarchical Z levels
    for (uint32_t level = 0; level < advanced_features.hierarchical_z_levels; ++level) {
        uint32_t level_size = 64 >> level;
        if (level_size == 0) level_size = 1;
        
        auto& z_buffer = tile_buffer.z_buffer_hierarchy[level];
        auto& z_min_buffer = tile_buffer.z_min_hierarchy[level];
        auto& z_max_buffer = tile_buffer.z_max_hierarchy[level];
        
        if (z_buffer.empty()) continue;
        
        // Calculate index in this level
        // TODO: Implement proper hierarchical Z-buffer indexing
        uint32_t level_x = (tile_x % level_size);
        uint32_t level_y = (tile_y % level_size);
        uint32_t index = level_y * level_size + level_x;

        if (index < z_buffer.size()) {
            z_buffer[index] = std::min(z_buffer[index], depth);
            z_min_buffer[index] = std::min(z_min_buffer[index], depth);
            z_max_buffer[index] = std::max(z_max_buffer[index], depth);
        }
    }
}

void GPU::dispatch_compute_shader(uint32_t group_x, uint32_t group_y, uint32_t group_z) {
    std::cout << "GPU: Dispatching compute shader - groups(" << group_x << ", " 
              << group_y << ", " << group_z << ")" << std::endl;
    
    if (compute_state.compute_shader_id == 0) {
        std::cout << "GPU: No compute shader set" << std::endl;
        return;
    }
    
    auto* compute_shader = get_compiled_shader(compute_state.compute_shader_id);
    if (!compute_shader) {
        std::cout << "GPU: Compute shader not found" << std::endl;
        return;
    }
    
    uint32_t total_groups = group_x * group_y * group_z;
    uint32_t groups_per_cu = std::max(1u, total_groups / (SHADER_ENGINE_COUNT * RDNA2ShaderEngine::CUS_PER_SE));
    
    // Distribute compute work across all CUs
    for (auto& se : shader_engines) {
        for (auto& cu : se.compute_units) {
            if (!cu.busy) {
                cu.busy = true;
                execute_shader_on_cu(cu, *compute_shader);
                cu.busy = false;
            }
        }
    }
    
    perf_counters.compute_dispatches++;
}

void GPU::execute_shader_on_cu(RDNA2ComputeUnit& cu, const CompiledShader& shader) {
    
    // Find available wavefront slot
    RDNA2Wavefront* available_wf = nullptr;
    for (auto& wf : cu.wavefronts) {
        if (!wf.active) {
            available_wf = &wf;
            break;
        }
    }
    
    if (!available_wf) {
        return; // No available wavefront slots
    }
    
    // Initialize wavefront for shader execution
    available_wf->active = true;
    available_wf->pc = 0;
    available_wf->exec_mask = 0xFFFFFFFFFFFFFFFFULL; // All 64 lanes active
    available_wf->instruction_count = shader.bytecode.size();
    
    // Initialize VGPR and SGPR banks
    std::fill(available_wf->vgprs.begin(), available_wf->vgprs.end(), 0);
    std::fill(available_wf->sgprs.begin(), available_wf->sgprs.end(), 0);
    
    // Execute shader instructions
    while (available_wf->pc < available_wf->instruction_count && available_wf->active) {
        uint32_t instruction = shader.bytecode[available_wf->pc];
        
        // Decode RDNA2 instruction format
        uint32_t opcode = (instruction >> 26) & 0x3F;
        uint32_t src0 = (instruction >> 0) & 0xFF;
        uint32_t src1 = (instruction >> 8) & 0xFF;
        uint32_t dst = (instruction >> 16) & 0xFF;
        
        switch (opcode) {
            case 0x01: // V_ADD_F32
                for (int lane = 0; lane < 64; ++lane) {
                    if (available_wf->exec_mask & (1ULL << lane)) {
                        float a = *reinterpret_cast<float*>(&available_wf->vgprs[src0 * 64 + lane]);
                        float b = *reinterpret_cast<float*>(&available_wf->vgprs[src1 * 64 + lane]);
                        float result = a + b;
                        available_wf->vgprs[dst * 64 + lane] = *reinterpret_cast<uint32_t*>(&result);
                    }
                }
                break;
                
            case 0x02: // V_MUL_F32
                for (int lane = 0; lane < 64; ++lane) {
                    if (available_wf->exec_mask & (1ULL << lane)) {
                        float a = *reinterpret_cast<float*>(&available_wf->vgprs[src0 * 64 + lane]);
                        float b = *reinterpret_cast<float*>(&available_wf->vgprs[src1 * 64 + lane]);
                        float result = a * b;
                        available_wf->vgprs[dst * 64 + lane] = *reinterpret_cast<uint32_t*>(&result);
                    }
                }
                break;
                
            case 0x03: // V_MAD_F32 (Multiply-Add)
                for (int lane = 0; lane < 64; ++lane) {
                    if (available_wf->exec_mask & (1ULL << lane)) {
                        float a = *reinterpret_cast<float*>(&available_wf->vgprs[src0 * 64 + lane]);
                        float b = *reinterpret_cast<float*>(&available_wf->vgprs[src1 * 64 + lane]);
                        float c = *reinterpret_cast<float*>(&available_wf->vgprs[dst * 64 + lane]);
                        float result = a * b + c;
                        available_wf->vgprs[dst * 64 + lane] = *reinterpret_cast<uint32_t*>(&result);
                    }
                }
                break;
                
            case 0x10: // S_LOAD_DWORD (Scalar load)
                {
                    uint64_t base_addr = (static_cast<uint64_t>(available_wf->sgprs[src0 + 1]) << 32) | 
                                        available_wf->sgprs[src0];
                    uint32_t offset = available_wf->sgprs[src1];
                    uint64_t load_addr = base_addr + offset;
                    
                    if (load_addr < GPU_MEMORY_SIZE) {
                        available_wf->sgprs[dst] = *reinterpret_cast<uint32_t*>(gpu_memory.get() + load_addr);
                    }
                }
                break;
                
            case 0x20: // V_INTERP_P1_F32 (Pixel interpolation)
                for (int lane = 0; lane < 64; ++lane) {
                    if (available_wf->exec_mask & (1ULL << lane)) {
                        // Simplified barycentric interpolation
                        // TODO: Implement proper barycentric interpolation
                        float i = static_cast<float>(lane % 8) / 8.0f;  // X coordinate
                        float j = static_cast<float>(lane / 8) / 8.0f;  // Y coordinate
                        float attr = *reinterpret_cast<float*>(&available_wf->vgprs[src0 * 64 + lane]);
                        float result = attr * (1.0f - i - j); // Barycentric weight
                        available_wf->vgprs[dst * 64 + lane] = *reinterpret_cast<uint32_t*>(&result);
                    }
                }
                break;
                
            case 0x30: // EXP (Export to render target)
                {
                    uint32_t target = src0 & 0xF;
                    if (target < 8) { // Color target
                        for (int lane = 0; lane < 64; ++lane) {
                            if (available_wf->exec_mask & (1ULL << lane)) {
                                uint32_t pixel_x = lane % 8;
                                uint32_t pixel_y = lane / 8;
                                
                                float r = *reinterpret_cast<float*>(&available_wf->vgprs[(src0 + 0) * 64 + lane]);
                                float g = *reinterpret_cast<float*>(&available_wf->vgprs[(src0 + 1) * 64 + lane]);
                                float b = *reinterpret_cast<float*>(&available_wf->vgprs[(src0 + 2) * 64 + lane]);
                                float a = *reinterpret_cast<float*>(&available_wf->vgprs[(src0 + 3) * 64 + lane]);
                                
                                // Convert to 8-bit and write to render target
                                uint32_t color = (static_cast<uint32_t>(a * 255.0f) << 24) |
                                               (static_cast<uint32_t>(b * 255.0f) << 16) |
                                               (static_cast<uint32_t>(g * 255.0f) << 8) |
                                               (static_cast<uint32_t>(r * 255.0f) << 0);
                                
                                write_pixel_to_render_target(target, pixel_x, pixel_y, color);
                            }
                        }
                    }
                }
                break;
                
            case 0x3F: // S_ENDPGM (End program)
                available_wf->active = false;
                break;
                
            default:
                // Unknown instruction - skip
                break;
        }
        
        available_wf->pc++;
        
        // Simulate instruction latency
        // TODO: Implement proper instruction latency not a simulation
        cu.cycle_count += get_instruction_latency(opcode);
    }
    
    perf_counters.shader_instructions_executed += available_wf->pc;
}

void GPU::rasterize_triangle(const float vertices[9]) {
    
    // Extract vertex positions
    float x0 = vertices[0], y0 = vertices[1], z0 = vertices[2];
    float x1 = vertices[3], y1 = vertices[4], z1 = vertices[5];
    float x2 = vertices[6], y2 = vertices[7], z2 = vertices[8];
    
    // Calculate bounding box
    float min_x = std::min({x0, x1, x2});
    float max_x = std::max({x0, x1, x2});
    float min_y = std::min({y0, y1, y2});
    float max_y = std::max({y0, y1, y2});
    
    // Convert to screen coordinates
    int screen_min_x = static_cast<int>(std::floor(min_x));
    int screen_max_x = static_cast<int>(std::ceil(max_x));
    int screen_min_y = static_cast<int>(std::floor(min_y));
    int screen_max_y = static_cast<int>(std::ceil(max_y));
    
    // Clamp to screen bounds
    screen_min_x = std::max(0, screen_min_x);
    screen_max_x = std::min(static_cast<int>(render_backends[0].color_targets[0].width) - 1, screen_max_x);
    screen_min_y = std::max(0, screen_min_y);
    screen_max_y = std::min(static_cast<int>(render_backends[0].color_targets[0].height) - 1, screen_max_y);
    
    // Calculate edge function coefficients
    float A01 = y0 - y1, B01 = x1 - x0, C01 = x0 * y1 - x1 * y0;
    float A12 = y1 - y2, B12 = x2 - x1, C12 = x1 * y2 - x2 * y1;
    float A20 = y2 - y0, B20 = x0 - x2, C20 = x2 * y0 - x0 * y2;
    
    // Calculate triangle area for barycentric coordinates
    float area = 0.5f * ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
    if (std::abs(area) < 1e-6f) {
        return; // Degenerate triangle
    }
    
    // Rasterize pixels in bounding box
    for (int y = screen_min_y; y <= screen_max_y; ++y) {
        for (int x = screen_min_x; x <= screen_max_x; ++x) {
            float px = static_cast<float>(x) + 0.5f; // Pixel center
            float py = static_cast<float>(y) + 0.5f;
            
            // Calculate edge function values
            float w0 = A12 * px + B12 * py + C12;
            float w1 = A20 * px + B20 * py + C20;
            float w2 = A01 * px + B01 * py + C01;
            
            // Check if pixel is inside triangle (all edge functions same sign)
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                // Calculate barycentric coordinates
                float lambda0 = w0 / (2.0f * area);
                float lambda1 = w1 / (2.0f * area);
                float lambda2 = w2 / (2.0f * area);
                
                // Interpolate depth
                float depth = lambda0 * z0 + lambda1 * z1 + lambda2 * z2;
                
                // Depth test
                uint64_t depth_addr = render_backends[0].depth_target.base_address + 
                                     (y * render_backends[0].depth_target.pitch + x) * 4;
                
                if (depth_addr < GPU_MEMORY_SIZE) {
                    float* existing_depth = reinterpret_cast<float*>(gpu_memory.get() + depth_addr);
                    
                    if (depth < *existing_depth) {
                        *existing_depth = depth;
                        
                        // Shade pixel with interpolated attributes
                        if (graphics_state.pixel_shader_id != 0) {
                            auto* pixel_shader = get_compiled_shader(graphics_state.pixel_shader_id);
                            if (pixel_shader) {
                                shade_pixel_with_attributes(x, y, *pixel_shader, lambda0, lambda1, lambda2);
                                perf_counters.pixels_shaded++;
                            }
                        }
                    }
                }
            }
        }
    }
}

void GPU::shade_pixel(uint32_t x, uint32_t y, const CompiledShader& pixel_shader) {
    shade_pixel_with_attributes(x, y, pixel_shader, 0.33f, 0.33f, 0.34f); // Default barycentric coords
}

void GPU::shade_pixel_with_attributes(uint32_t x, uint32_t y, const CompiledShader& pixel_shader, 
                                     float lambda0, float lambda1, float lambda2) {
    // Find available compute unit for pixel shading
    uint32_t cu_index = (x + y) % shader_engines[0].compute_units.size();
    auto& cu = shader_engines[0].compute_units[cu_index];
    
    // Find available wavefront
    RDNA2Wavefront* wf = nullptr;
    for (auto& wavefront : cu.wavefronts) {
        if (!wavefront.active) {
            wf = &wavefront;
            break;
        }
    }
    
    if (!wf) {
        return; // No available wavefront
    }
    
    // Initialize wavefront for pixel shading
    wf->active = true;
    wf->pc = 0;
    wf->exec_mask = 1ULL; // Single pixel
    
    // Set up pixel shader inputs
    // V0-V3: Barycentric coordinates and screen position
    wf->vgprs[0] = *reinterpret_cast<uint32_t*>(&lambda0);
    wf->vgprs[1] = *reinterpret_cast<uint32_t*>(&lambda1);
    wf->vgprs[2] = *reinterpret_cast<uint32_t*>(&lambda2);
    
    float screen_x = static_cast<float>(x);
    float screen_y = static_cast<float>(y);
    wf->vgprs[3] = *reinterpret_cast<uint32_t*>(&screen_x);
    wf->vgprs[4] = *reinterpret_cast<uint32_t*>(&screen_y);
    
    // Execute pixel shader
    execute_shader_on_cu(cu, pixel_shader);
    
    // Extract output color from VGPR (typically V0-V3 for RGBA)
    float r = *reinterpret_cast<float*>(&wf->vgprs[0]);
    float g = *reinterpret_cast<float*>(&wf->vgprs[1]);
    float b = *reinterpret_cast<float*>(&wf->vgprs[2]);
    float a = *reinterpret_cast<float*>(&wf->vgprs[3]);
    
    // Clamp values to [0, 1]
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));
    a = std::max(0.0f, std::min(1.0f, a));
    
    // Convert to 8-bit color
    uint32_t color = (static_cast<uint32_t>(a * 255.0f) << 24) |
                     (static_cast<uint32_t>(b * 255.0f) << 16) |
                     (static_cast<uint32_t>(g * 255.0f) << 8) |
                     (static_cast<uint32_t>(r * 255.0f) << 0);
    
    // Apply blending if enabled
    if (graphics_state.blend_enabled) {
        color = apply_blending(x, y, color);
    }
    
    // Write to render target
    write_pixel_to_render_target(0, x, y, color);
}

void GPU::process_tile_advanced(uint32_t tile_x, uint32_t tile_y, RDNA2ShaderEngine::Rasterizer::TileBuffer& tile_buffer) {
    const uint32_t tile_size = 64;
    
    // Early hierarchical Z rejection for entire tile
    float tile_z_min = 1.0f, tile_z_max = 0.0f;
    bool tile_has_geometry = false;
    
    // Process primitives in this tile
    for (uint32_t prim_id : tile_buffer.primitive_ids) {
        // Get primitive data (simplified - would come from vertex buffer)
        // TODO: Implement proper vertex fetching
        float vertices[9]; // 3 vertices * 3 components
        get_primitive_vertices(prim_id, vertices);
        
        // Calculate primitive bounding box and depth range
        float prim_min_x = std::min({vertices[0], vertices[3], vertices[6]});
        float prim_max_x = std::max({vertices[0], vertices[3], vertices[6]});
        float prim_min_y = std::min({vertices[1], vertices[4], vertices[7]});
        float prim_max_y = std::max({vertices[1], vertices[4], vertices[7]});
        float prim_min_z = std::min({vertices[2], vertices[5], vertices[8]});
        float prim_max_z = std::max({vertices[2], vertices[5], vertices[8]});
        
        // Check if primitive overlaps with tile
        uint32_t tile_min_x = tile_x * tile_size;
        uint32_t tile_max_x = tile_min_x + tile_size - 1;
        uint32_t tile_min_y = tile_y * tile_size;
        uint32_t tile_max_y = tile_min_y + tile_size - 1;
        
        if (prim_max_x >= tile_min_x && prim_min_x <= tile_max_x &&
            prim_max_y >= tile_min_y && prim_min_y <= tile_max_y) {
            
            // Hierarchical Z test for early rejection
            if (hierarchical_z_test(tile_x, tile_y, prim_min_z, prim_max_z, 0)) {
                tile_buffer.visible_primitive_count++;
                tile_has_geometry = true;
                
                // Update tile depth bounds
                tile_z_min = std::min(tile_z_min, prim_min_z);
                tile_z_max = std::max(tile_z_max, prim_max_z);
                
                // Perform triangle setup for rasterization
                setup_triangle_for_rasterization(vertices, tile_x, tile_y, tile_size);
                
                // Fine rasterization within tile
                rasterize_triangle_in_tile(vertices, tile_x, tile_y, tile_size);
            }
        }
    }
    
    // Update hierarchical Z buffer with tile results
    if (tile_has_geometry) {
        update_hierarchical_z_tile(tile_x, tile_y, tile_z_min, tile_z_max);
    }
    
    // Tile shading - execute pixel shaders for covered pixels
    if (tile_buffer.visible_primitive_count > 0) {
        execute_tile_shading(tile_x, tile_y, tile_buffer);
    }
}

bool GPU::hierarchical_z_test(uint32_t tile_x, uint32_t tile_y, float z_min, float z_max, uint32_t level) {
    if (level >= advanced_features.hierarchical_z_levels) {
        return true; // Accept at deepest level
    }
    
    uint32_t level_tile_size = 64 >> level;
    uint32_t level_tile_x = tile_x >> level;
    uint32_t level_tile_y = tile_y >> level;
    
    // Get hierarchical Z buffer for this level
    uint32_t se_index = (tile_x + tile_y) % shader_engines.size();
    uint32_t tile_id = level_tile_y * (render_backends[0].color_targets[0].width / level_tile_size) + level_tile_x;
    
    if (tile_id >= shader_engines[se_index].rasterizer.tile_buffers.size()) {
        return true; // Accept if no buffer
    }
    
    auto& tile_buffer = shader_engines[se_index].rasterizer.tile_buffers[tile_id];
    
    if (level < tile_buffer.z_buffer_hierarchy.size()) {
        auto& z_buffer = tile_buffer.z_buffer_hierarchy[level];
        auto& z_min_buffer = tile_buffer.z_min_hierarchy[level];
        auto& z_max_buffer = tile_buffer.z_max_hierarchy[level];
        
        if (!z_buffer.empty()) {
            uint32_t buffer_index = level_tile_y * (render_backends[0].color_targets[0].width / level_tile_size) + level_tile_x;
            
            if (buffer_index < z_buffer.size()) {
                float existing_z_min = z_min_buffer[buffer_index];
                float existing_z_max = z_max_buffer[buffer_index];
                
                // Early Z reject if primitive is completely behind existing geometry
                if (z_min > existing_z_min) {
                    return false;
                }
                
                // Early Z accept if primitive is completely in front
                if (z_max < existing_z_max) {
                    return true;
                }
                
                // Recurse to next level for more precise testing
                if (level + 1 < advanced_features.hierarchical_z_levels) {
                    return hierarchical_z_test(tile_x, tile_y, z_min, z_max, level + 1);
                }
            }
        }
    }
    
    return true; // Accept by default
}

CompiledShader GPU::compile_shader_advanced(uint32_t shader_id, const std::vector<uint8_t>& shader_source, uint32_t shader_type) {
    CompiledShader compiled;
    compiled.shader_id = shader_id;
    compiled.shader_type = shader_type;
    
    // Parse shader source (assuming SPIR-V input)
    if (shader_source.size() < 20 || 
        *reinterpret_cast<const uint32_t*>(shader_source.data()) != 0x07230203) {
        std::cout << "GPU: Invalid SPIR-V shader format" << std::endl;
        return compiled;
    }
    
    // SPIR-V header parsing
    uint32_t version = *reinterpret_cast<const uint32_t*>(shader_source.data() + 4);
    uint32_t generator = *reinterpret_cast<const uint32_t*>(shader_source.data() + 8);
    uint32_t bound = *reinterpret_cast<const uint32_t*>(shader_source.data() + 12);
    
    std::cout << "GPU: Compiling SPIR-V shader - version: 0x" << std::hex << version 
              << ", generator: 0x" << generator << ", bound: " << std::dec << bound << std::endl;
    
    // Analyze shader resource usage
    compiled.resource_usage = analyze_shader_resources(shader_source);
    
    // Convert SPIR-V to RDNA2 ISA
    compiled.bytecode = compile_spirv_to_rdna2(shader_source, shader_type);
    
    // Optimize for RDNA2 architecture
    optimize_rdna2_shader(compiled.bytecode);
    
    // Calculate shader statistics
    compiled.instruction_count = compiled.bytecode.size();
    compiled.vgpr_count = calculate_vgpr_usage(compiled.bytecode);
    compiled.sgpr_count = calculate_sgpr_usage(compiled.bytecode);
    compiled.lds_usage = calculate_lds_usage(compiled.bytecode);
    
    std::cout << "GPU: Compiled shader " << shader_id << " - " << compiled.instruction_count 
              << " instructions, " << compiled.vgpr_count << " VGPRs, " 
              << compiled.sgpr_count << " SGPRs" << std::endl;
    
    return compiled;
}

void GPU::sync_with_vulkan() {
#ifdef PSX5_ENABLE_VULKAN
    if (vulkan_backend && vulkan_backend->is_initialized()) {
        // Wait for all GPU operations to complete
        VkDevice device = vulkan_backend->get_device();
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
    }
#endif
}

uint32_t GPU::create_buffer(size_t size, uint32_t usage_flags) {
#ifdef PSX5_ENABLE_VULKAN
    if (vulkan_backend && vulkan_backend->is_initialized()) {
        VkBufferUsageFlags vk_usage = 0;
        if (usage_flags & 0x1) vk_usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (usage_flags & 0x2) vk_usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (usage_flags & 0x4) vk_usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (usage_flags & 0x8) vk_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        uint32_t vk_buffer_id = vulkan_backend->create_buffer(size, vk_usage, VMA_MEMORY_USAGE_GPU_ONLY);
        if (vk_buffer_id != 0) {
            GPUResource resource;
            resource.address = reinterpret_cast<uint64_t>(vulkan_backend);
            resource.size = size;
            resource.format = 0;
            resource.width = 0;
            resource.height = 0;
            resource.depth = 0;
            resource.mip_levels = 0;
            resource.resource_type = 0; // buffer
            resource.compressed = false;
            
            uint32_t resource_id = next_resource_id++;
            gpu_resources[resource_id] = resource;
            
            // Store Vulkan buffer ID in a mapping
            vulkan_buffer_mapping_[resource_id] = vk_buffer_id;
            
            return resource_id;
        }
    }
#endif
    
    // Fallback to software implementation
    GPUResource resource;
    resource.address = reinterpret_cast<uint64_t>(gpu_memory.get()) + memory_allocations.size() * 1024;
    resource.size = size;
    resource.format = 0;
    resource.width = 0;
    resource.height = 0;
    resource.depth = 0;
    resource.mip_levels = 0;
    resource.resource_type = 0; // buffer
    resource.compressed = false;
    
    uint32_t resource_id = next_resource_id++;
    gpu_resources[resource_id] = resource;
    memory_allocations[resource.address] = size;
    
    return resource_id;
}

uint32_t GPU::create_texture(uint32_t width, uint32_t height, uint32_t format, uint32_t mip_levels) {
#ifdef PSX5_ENABLE_VULKAN
    if (vulkan_backend && vulkan_backend->is_initialized()) {
        VkFormat vk_format = VK_FORMAT_R8G8B8A8_UNORM; // Default format
        switch (format) {
            case 1: vk_format = VK_FORMAT_R8G8B8A8_UNORM; break;
            case 2: vk_format = VK_FORMAT_B8G8R8A8_UNORM; break;
            case 3: vk_format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
            case 4: vk_format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
        }
        
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        
        uint32_t vk_image_id = vulkan_backend->create_image(width, height, vk_format, usage, VMA_MEMORY_USAGE_GPU_ONLY);
        if (vk_image_id != 0) {
            GPUResource resource;
            resource.address = reinterpret_cast<uint64_t>(vulkan_backend);
            resource.size = width * height * 4; // Approximate size
            resource.format = format;
            resource.width = width;
            resource.height = height;
            resource.depth = 1;
            resource.mip_levels = mip_levels;
            resource.resource_type = 2; // texture2D
            resource.compressed = false;
            
            uint32_t resource_id = next_resource_id++;
            gpu_resources[resource_id] = resource;
            
            // Store Vulkan image ID in a mapping
            vulkan_image_mapping_[resource_id] = vk_image_id;
            
            return resource_id;
        }
    }
#endif
    
    // Fallback to software implementation
    size_t texture_size = width * height * 4 * mip_levels; // Approximate
    
    GPUResource resource;
    resource.address = reinterpret_cast<uint64_t>(gpu_memory.get()) + memory_allocations.size() * 1024;
    resource.size = texture_size;
    resource.format = format;
    resource.width = width;
    resource.height = height;
    resource.depth = 1;
    resource.mip_levels = mip_levels;
    resource.resource_type = 2; // texture2D
    resource.compressed = false;
    
    uint32_t resource_id = next_resource_id++;
    gpu_resources[resource_id] = resource;
    memory_allocations[resource.address] = texture_size;
    
    return resource_id;
}

void GPU::destroy_resource(uint32_t resource_id) {
    auto it = gpu_resources.find(resource_id);
    if (it == gpu_resources.end()) return;
    
#ifdef PSX5_ENABLE_VULKAN
    if (vulkan_backend && vulkan_backend->is_initialized()) {
        // Check if it's a Vulkan buffer
        auto buffer_it = vulkan_buffer_mapping_.find(resource_id);
        if (buffer_it != vulkan_buffer_mapping_.end()) {
            vulkan_backend->destroy_buffer(buffer_it->second);
            vulkan_buffer_mapping_.erase(buffer_it);
        }
        
        // Check if it's a Vulkan image
        auto image_it = vulkan_image_mapping_.find(resource_id);
        if (image_it != vulkan_image_mapping_.end()) {
            vulkan_backend->destroy_image(image_it->second);
            vulkan_image_mapping_.erase(image_it);
        }
    }
#endif
    
    // Clean up software resources
    memory_allocations.erase(it->second.address);
    gpu_resources.erase(it);
}
