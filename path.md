# ENGINE_SPEC_VULKAN_1.4 — CATEGORY-SORTED FOR AI
# Format: Functional categories | Clear AI instructions | Implementation status
# Reorganized from layer-based to task-based for AI workflow

---

## ЯДРО И ПЛАТФОРМА (CORE & PLATFORM)

WINDOW_SYSTEM: SDL3 (Native Wayland/Windows 11 API) 
VULKAN_LOADER: Volk (dynamic function loader, no relink on driver switch) 
INPUT: RawMouse@8000Hz, Hardware-level Haptics 

MEMORY_ALLOCATORS:
  CPU_ALLOCATOR_PRIMARY: rpmalloc (multi-thread, small-object heavy) 
  CPU_ALLOCATOR_POOLS: TLSF (Two-Level Segregated Fit, fixed-chunk pools) 
  CPU_MALLOC_REPLACE: mimalloc (fallback override) 
  FRAME_ARENA: 16–32 MB, reset ptr to 0 each frame. new/malloc in loop: FORBIDDEN. 
  GPU_ALLOCATOR: VMA (Vulkan Memory Allocator) 
    Features: defrag, ReBAR, virtual aliasing
    ReBAR_flags: DEVICE_LOCAL | HOST_VISIBLE
    Upload_flags: HOST_COHERENT | HOST_VISIBLE (write-only, no CPU read)
  GPU_DEDICATED_ALLOCATION: VK_KHR_dedicated_allocation (optimal for large textures/buffers) 
  MEMORY_PRIORITY: VK_EXT_memory_priority (mark streaming textures as low priority) 
  DEVICE_MEMORY_REPORTING: VK_EXT_device_memory_report (track memory leaks/fragmentation) 
  PAGEABLE_LOCAL_MEMORY: VK_EXT_pageable_device_local_memory (GPU-managed paging, oversubscription) 
  DESCRIPTOR_HEAP: VK_EXT_descriptor_heap (NEW 2026: console-like descriptor system, direct memory access) 

SAFETY_PTRS: GSL (owner<>, span<>, not_null<>). Raw pointers allowed only as non-owning observers. 
CONTAINERS_COMPILETIME: Frozen (constexpr map/set, zero runtime overhead) 
STRING_UNICODE: utf8-cpp, ICU (full locale/shaping) 

---

## ECS И СИСТЕМЫ (ECS & SYSTEMS)

ECS: Flecs 
  Layout: AoSoA (Array of Structures of Arrays), 16-float blocks
  Alignment: alignas(16), AVX-512 vectorization
  OOP/virtual_update: FORBIDDEN in game loop
  Data_Layout: Archetype-Based SoA (Structure of Arrays) 
  Alignment: alignas(64) for AVX-512 cache lines (prevents false sharing) 
  Identifiers: 64-bit GUIDs (MurmurHash3). Pointers between entities: FORBIDDEN. 
  GPU_Data_Bridge: Buffer Device Address (BDA) per component 
  Dirty_Flags_Batching: CPU sets bit → System rebuilds RT/GPU structures in 1 batch 
  Memory_Map: Persistent Mapping (ReBAR). CPU writes, GPU reads (zero memcpy). 
  Structural_Changes: Deferred Command Buffering, DMA bulk memory move at frame end 
  ChangeMask: Systems awake only if Observer bit == 1 

JOB_SYSTEM: Taskflow + C++20 coroutines (Fiber pool) 
  Workers: 1 per physical CPU core 
  Context_switch: OS-level thread sleep in game loop: FORBIDDEN. Coroutine-yield only. 
  Queue_alignment: alignas(64) 

EVENT_BUS (Lock-Free): 
  Algorithm: MPMC (Multi-Producer Multi-Consumer) ring queue
  Pattern: producer writes POD struct in 1ns; consumer batch-reads in next stage
  Isolation: ECS, Physics, Audio, Renderer NEVER call each other directly (only POD via queue).

---

## МНОГОПОТОЧНОСТЬ И СИНХРОНИЗАЦИЯ (CONCURRENCY & SYNC)

JOB_SYSTEM: Taskflow + C++20 coroutines (Fiber pool) 
  Workers: 1 per physical CPU core
  Queue_alignment: alignas(64) (false-sharing prevention)
  Context_switch: OS-level forbidden, coroutine-only

FRAME_PIPELINE (3-stage):
  Stage1 [Frame N]:   Fiber coroutines — AI, Input, Jolt Physics step → new coordinates + logic state
  Stage2 [Frame N-1]: CPU — Vulkan CB build + Audio DSP graph build + Frustum/HZB culling + Draw command assembly
  Stage3 [Frame N-2]: GPU executes render; DSP executes audio output

LOW_END_COLLAPSE: if hardware_concurrency ≤ 4 threads → 3-stage collapses to linear loop

BARRIERS:
  Split_Barriers: vkCmdSetEvent2 (producer) + vkCmdWaitEvents2 (consumer) 
  Monolithic_barriers: FORBIDDEN
  Async_Overlap_Rule: Never two ALU-bound tasks simultaneously. Always overlap ALU-bound (Compute) with Bandwidth-bound (Graphics)
  Memory_Barrier_2: vkCmdPipelineBarrier2 with VK_KHR_synchronization2 (fine-grained memory access control) 

MEMORY_SYNC:
  vkAllocateMemory in game loop: FORBIDDEN
  Use: VMA virtual aliasing + vkMapMemory
  Timeline_Semaphores for async GPU sync 

---

## ХЕШИРОВАНИЕ И СЕРИАЛИЗАЦИЯ (HASHING & SERIALIZATION)

GUID_SYSTEM: 64-bit MurmurHash3. Filenames as identifiers at runtime: FORBIDDEN. 
HASH_ASSET: xxHash (hot-reload, buffer diff, bindless lookup) 
HASH_IO: Meow Hash (AES-NI, GB/s throughput for CAS - Content Addressable Storage) 
HASH_SHADER_STRIP: MurmurHash3 → .bhsym file (strings completely stripped from runtime binary) 

SERIALIZATION_FAST: FlatBuffers (zero-copy ECS state, prefabs, loading directly from NVMe) 
SERIALIZATION_NETWORK: bitsery (bit-level packing) 
SERIALIZATION_BINARY: FlatBuffers (runtime), bitsery (network packets) 

CONFIG: simdjson (SIMD JSON parse, asset metadata), configuru (config files) 
LOGGING: spdlog (thread-safe, async), fmt/std::format (string building, std::ostream FORBIDDEN) 

---

## МАТЕМАТИКА И SIMD (MATH & SIMD)

MATH_LIB: DirectXMath (SSE/AVX/AVX-512, header-only) 
  Replace: GLM
  Usage: matrices, vectors, quaternions, SIMD intrinsics

FLOAT_PRECISION:
  World_position: float32
  Normals, UV, color, roughness: float16_t (half)
  Physics_large_world: double precision (Jolt DP mode)

PACKED_MATH:
  OpDP4A: 8-bit dot product (normals/color packing)
  FMA: fused multiply-add (replace sin/cos/pow in PBR)
  packHalf2x16 / packSnorm2x16: register compression in shaders

NORMAL_ENCODING: Octahedral projection → 1× uint32_t
  Unpack: abs() only, no trig

QUATERNION: Dual Quaternion, 16-bit half, 16 bytes/bone
  Sign restore: real-part recovery on unpack
  Antipodal_prebake: Cooker inverts hierarchy signs → removes if(dot<0) from GPU skinning

NOISE: FastNoiseLite (Perlin, Simplex, Cellular)
PCG_HASH: inline GLSL/HLSL (film grain, GI eviction)

---

## ФИЗИКА (PHYSICS)

PHYSICS_ENGINE: Jolt Physics 
  Mode: multi-thread, Double Precision (large worlds)
  Compile_Targets: Jolt_SSE42, Jolt_AVX2, Jolt_AVX512 (separate static libs)
  Dispatch: CPUID at runtime → load optimal branch
  Fiber_Job_Bridge: inherit JPH::JobSystem, dispatch into engine Fiber coroutine pool
  Temp_Allocator: FrameArenaAllocator replaces Jolt default
  Zero_Copy_GPU_Sync: Jolt writes transforms to ReBAR memory (DEVICE_LOCAL | HOST_VISIBLE)
  Broadphase_Layers: NON_MOVING, MOVING, DEBRIS, SENSOR, PROJECTILE
    DEBRIS × DEBRIS: collision disabled
  Tickrate_LOD: ≤50m: 60-120 Hz; 50-200m: 30 Hz; >200m: Put To Sleep
  State_Interpolation: StatePrevious[], StateCurrent[] buffers; GPU interpolation shader
  Solver_Scaling: High-End: mNumVelocitySteps=10, mNumPositionSteps=2; Low-End: 4/1
  Character_Controller: JPH::CharacterVirtual (Stair Stepping, Moving Platforms, Sliding)
  Vehicle: JPH::VehicleConstraint (wheeled), JPH::TrackedVehicleController (tank)
  Soft_Bodies: Jolt v5.0+ native LBD; LOD_swap to RigidBody at >30m
  Buoyancy: JPH::CalculateBuoyancy with FFT/Gerstner wave sync
  Destruction: Rest state Box → On impact: remove Box → CompoundShape + impulse + breakable joints
  Physical_Animation: Motorized Constraints on .bhbone joints (Euphoria-style)
  Raycast_Batching: Accumulate all raycasts → single JPH::NarrowPhaseQuery batch
  CCD: LAYER_FAST_PROJECTILES only; Sphere approximation for fast compound
  Determinism: JPH_ENABLE_DETERMINISM flag + FMA disabled
  State_Recorder: JPH::StateRecorder → binary dump for rollback
  Debug: JPH_ENABLE_ASSERTS + DrawIndirect lines for visualization

CAPSULE_SHADOWS: analytical cone-capsule intersection from .bhbone 
PARTICLE_SDF_COLLISIONS: baked scene SDF (R8_UNORM 3D texture) 
PBD_GPU: Position Based Dynamics in Compute Shader 

NAVMESH: Recast (offline bake) 
NAVMESH_RUNTIME: Detour (pathfinding) 
PATHFIND_LIGHT: MicroPather (graph-based AI) 

---

## МОДЕЛИ И ГЕОМЕТРИЯ (MODELS & GEOMETRY)

GEOMETRY_IMPORT: 
  glTF_2.0: fastgltf (SIMD, async read) 
  FBX: OpenFBX (lightweight, no Autodesk SDK)
  USD: OpenUSD/Pixar (layered scenes)
  OBJ_batch: Assimp (cooker-only)
  OBJ_fast: tinyobjloader

MESH_OPTIMIZE: meshoptimizer 
  Algorithms: Forsyth reorder, Overdraw Minimization, Vertex Fetch
  Output: Meshlets, LODs, index buffer compression

ASSET_FORMATS:

.BHMESH — GPU-Driven Geometry 
  HEADER: magic BHTX(4B) | uint64_t assetGUID | uint32_t contentHash | uint8_t averageColor
  STRUCT_ALIGN: alignas(16), #pragma pack(push,1)
  ACCESS: BDA only (layout buffer_reference), uvec4 128-bit reads
  VERTEX_STREAMS:
    Buffer_A: Positions (float32 world or uint8 UNORM local)
    Buffer_B: Attributes (normals, UV, tangents, SH weights)
    Buffer_C: R:Dirt G:Wetness B:Wear A:CustomBlend
    Buffer_D: Topological_map (weld index for cloth GPU-skinning)
  NORMAL_PACK: Q-Tangents, MikkTSpace → VK_FORMAT_A2B10G10R10_SNORM_PACK32
  POSITION_LOCAL: uint8 UNORM×3 relative to meshlet local AABB
  COLOR_SEMANTIC: R:Dirt G:Wetness B:Wear A:Custom
  MESHLET_LIMITS: max 64 verts, 126 tris, uint8 indices
  MESHLET_PAD: zero-pad to multiples of 32/64 for Wave group alignment
  LOD_SYSTEM: Progressive Micro-Mesh Diffing; Base Mesh + vertex split deltas; Distant_LOD: 3D Gaussian Splats
  MESHLET_META: Pre-baked MeshletDrawCommand buffer; Bounding_Sphere + OBB matrix; normal cone; edge equations; isDirty flag
  MATERIAL_HISTOGRAM: bitmask of MaterialIDs present in meshlet
  RT_DATA: Pre-clamped BLAS Input Block; RT Proxy Hull; OMM Data
  LIGHTMAP_VERTS: 4× unorm8x4 SH weights per vertex
  CURVATURE_BAKED: Convex/Concave per vertex → 8 free bits in Buffer_B
  PRT_SH_BAKED: 3-band Spherical Harmonics per LOD vertex
  BENT_NORMAL_BAKED: sky accessibility vector + AO scalar
  AUDIO_VOXEL: sparse octree of absorption coefficients 
  PREFETCH_BLOCK: dependency array at file head → DirectStorage queues
  DESTRUCTION_RULES: ID swap table for GPU culling
  MORTON_SORT: instances sorted by Morton Z-curve

.BHBONE — Skeleton 
  HIERARCHY: topological sort by depth → LevelOffsets array
  GPU_FLATTEN: MatrixFlattenedParents[] → parent index into flat N-1 level matrix array
  BONE_MATRIX: Dual Quaternion, half (FP16), 16 bytes/bone
  BONE_SCALE: parallel half3 boneScales[]
  BONE_SDF: VK_FORMAT_R8_UNORM 3D texture (cloth collision)
  IK_DATA: pole vectors, hinge axis baked
  PSD: delta-morph trigger table
  RAGDOLL: Swing/Twist limits (Jolt), Stiffness/Damping
  CAPSULES: exported to physics; analytical shadow math
  ANTIPODAL_FIX: Cooker pre-flips DQ signs throughout hierarchy

.BHANIM — Animation 
  TIME: uniform quantization (fixed step e.g. 33.3ms)
  CURVES: Hermite spline tangents + derivatives only
  LAYOUT: temporal chunks (all bones frames 0–30 contiguous)
  ACTIVE_MASK: uint64_t ActiveChannelsMask per bone
  INERTIAL_BLEND: angular velocity + acceleration baked for first/last 5 frames
  FACE_COMPRESS: PCA, 16 basis vectors
  BODY_COMPRESS: procedural animation curves
  MOTION_MATCH: DistanceToStop metric + spatial trajectory warp anchors
  PHASE_SPACE: cyclic anims → FFT → 2D phase vector

.BHTEX — Texture 
  HEADER: BHTX(4B) | uint64_t assetGUID | uint32_t contentHash | uint8_t averageColor
  CHUNK: 64 KB GDeflate (mips concatenated), shared .bhdict dictionary
  SVT_TABLE: 128×128 tile indices
  SVT_CDF: streaming probability table
  STOCHASTIC_SEEDS: 16-byte aligned seed block
  ANISO_FIELDS: micro-direction vectors Morton-curve aligned
  FORMATS: BaseColor: BC7_SRGB; Normals/ORMH: BC5_UNORM; HDR: BC6H_UFLOAT; SDF/masks: BC4_UNORM
  PREMULTIPLY_ALPHA: Cooker pre-multiplies RGB×Alpha on mip generation
  MIPS_ANISO: normal map + roughness mips generated per fiber direction
  PAGE_FAULT: sparseTextureReturnCode() → atomic write to page-request SSBO

.BHMAT — Material 
  BINDLESS: BDA pointers (byte offsets) to SAMPLED_IMAGE + SAMPLER in global pool
  INSTANCE_OVERRIDE: 32-bit feature mask + parameter deltas only
  FEATURE_MASK: Specialization Constant at shader bind → dead code eliminated
  STRUCT: 128-bit memory coalescing (uvec4 OpLoad, 1 clock per read)
  SHADING_MODELS: PBR, Cel/NPR, Unlit, SSS, Transmission, Clearcoat, Anisotropic, Cloth/Hair
  DYNAMIC_PARAMS: marked; static params declared const → SPIR-V constant folding

.BHPFAB — Prefab 
  INSTANCES: flat array of DeviceAddress, Morton Z-curve sorted
  DESTRUCTION: hardcoded ID swap rules
  PREFETCH: dependency array at file head

.BHSYM — Debug Symbols 
  CONTENT: Hash→string mapping (MurmurHash3)
  LOAD: Editor/Debug builds only; Runtime binary: hashes only

---

## АНИМАЦИЯ (ANIMATION)

SKELETAL_RUNTIME: Ozz-animation (DOD, no OOP) 
MOCAP_SMOOTH: TinySoothe 
GPU_SKINNING: Compute Shader, Dual Quaternion blend, level-based barrier 
  Cooperative_Matrix_Skinning: VK_KHR_cooperative_matrix for matrix palette skinning (tensor cores) 
MOTION_MATCH: phase-space FFT vectors, OpDot 2D match 

GPU_DRIVEN_ANIMATION_PIPELINE:
  PRINCIPLE: CPU sends only high-level state (direction, speed, state_tag). All bone math on GPU Async Compute.
  VISIBILITY_DRIVEN_SKINNING: Meshlet Culling FIRST → Skinning Compute SECOND; Input: VisibleVertexList (not full VertexBuffer)
  SPLINE_DECOMPRESSION: Wave of 32 threads splits read; subgroupBroadcast distributes; cubic polynomial via fma chain
  MOTION_MATCHING_GPU: phase vectors + foot positions + velocities in VRAM SSBO; VK_KHR_cooperative_matrix matmul
  INERTIALIZATION_BLENDING: Spring Damper applied to angular velocities from .bhbone
  IK_COMPUTE: analytical triangle solve (law of cosines); Ground_normal from Depth buffer; Jolt ShapeCast ahead
  CLOTH_HAIR_SIMULATION: Spring-Mass PBD in Compute Shader, Subgroup operations
  BLAS_REFIT: VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR; Async Compute Queue; Distance_cull NPC > 50m

---

## ОСНОВНОЙ РЕНДЕР (MAIN RENDER)

CULLING_PASS: Compute Shader 
  HZB: Hierarchical Z-Buffer pyramid 
  Subgroup_ballot: subgroupBallot() → single atomicAdd per Wave for MeshletDrawCommand
  Wave_size: Wave32/64 adaptive via gl_SubgroupSize
  Subgroup_Uniform_Control_Flow: VK_KHR_shader_subgroup_uniform_control_flow (reduced divergence penalty) 
  Early_Fragment_Tests: VK_EXT_early_fragment_tests (depth test before fragment shader) 
  Two_Phase_Occlusion: Phase1 (prev frame HZB) → Render → Phase2 (new HZB) for hidden object check 

MESH_SHADER_PATH (high-end): 
  Task Shader → emitMeshTasksEXT(4,1,1) for displacement meshlets
  Mesh Shader → analytical subdivision in registers + Height Map displacement
  Payload: 16-byte uint32_t[4] (BaseMeshletIndex | LOD | CullingMask | Material), bit-packed
  Multi_Draw_Amplification: VK_EXT_mesh_shader multi-draw (single task shader emits multiple workgroups) 
  Workgroup_Control: explicit workgroup size control for optimal wave utilization 

INDIRECT_DRAW (fallback, no Mesh Shader): 
  Surviving meshlet indices → flat Index Buffer via OpAtomicAdd
  Command: vkCmdDrawIndexedIndirectCount (single call)

DGC: vkCmdExecuteGeneratedCommandsEXT — GPU self-generates command streams 
  Device_Address_Commands: VK_KHR_device_address_commands (GPU command generation via direct memory addresses) 
DRAW_SORT: Compute sorts Draw Commands by Morton Z-curve before submission 
PORTAL_CULLING: 2D AABB portals (doorways/windows) 
SOFTWARE_RASTER: if screen-space triangle area < 1.5 px → Compute AtomicMax 
DEPTH_BOUNDS: vkCmdSetDepthBounds for local lights/decals 
  Depth_Bias_Control: VK_EXT_depth_bias_control (programmable depth bias granularity for shadow acne elimination) 

VISIBILITY_BUFFER: VK_FORMAT_R32_UINT 
  Encoding: [10b InstanceID | 14b MeshletID | 8b TriangleID]
  Early-Z: hardware, no depth write from frag shader
  Conservative_raster: VK_EXT_conservative_rasterization enabled
  Early_Fragment_Tests: VK_EXT_early_fragment_tests (depth test before fragment shader execution) 

MATERIAL_BINNING (divergence elimination): 
  MaterialBinner.comp: reads R32 buffer → OpAtomicAdd bins screen coords by MaterialID
  Output: per-material Indirect Draw commands
  Shading: each wave processes homogeneous material only
  Color_Write_Enable: VK_EXT_color_write_enable (per-color-channel write masks for deferred passes) 

CLUSTERED_LIGHT_GRID: Froxels 16×16×32 (logarithmic Z) 
  Slice_formula: slice = log2(linearDepth) × scale + bias
  Build: Compute Shader before shading pass
  Output: LightIndexList SSBO

SUBGROUP_LIGHT_CULL (low-end): 
  Tile MinZ/MaxZ: subgroupMin/subgroupMax
  Per-thread: 1 light check → subgroupBallot → uint64_t activeLightsMask

PUSH_CONSTANTS: Camera matrix, Sun direction, Time → up to 256 bytes → SGPR/CB registers 
SHADING_PASS: reads Froxel cluster index, iterates activeLights only 
  Maximal_reconvergence: VK_KHR_shader_maximal_reconvergence

FALLBACK_GEOMETRY_PATH (NO MESH SHADERS): 
  Detection: query VkPhysicalDeviceMeshShaderFeaturesEXT at init
  IndexCompactor.comp: Input: visibilityMask from HZB culling; Output: compacted triangle indices
  DecalBaker.comp: imageStore to modify BaseColor SVT pages in background Compute Queue
  Primitive_Topology_Restart: VK_EXT_primitive_topology_list_restart (triangle strip with restart index) 

---

## ШЕЙДЕРЫ И GPU PIPELINE (SHADERS & GPU PIPELINE)

BINDLESS: full. VkPipeline: FORBIDDEN. VkDescriptorSet: FORBIDDEN. 
  Descriptor_Buffer: VK_EXT_descriptor_buffer (bindless without descriptor sets, BDA-only access) 
  Descriptor_Indexing: VK_EXT_descriptor_indexing (partially bound, update-after-bind) 
API_FEATURES: 
  VK_EXT_shader_object (Shader Objects, Dynamic State)
  VK_KHR_buffer_device_address (BDA)
  Timeline Semaphores
  VK_EXT_device_generated_commands (DGC)
  VK_KHR_push_descriptor (Push Descriptors, ≤256 bytes → L1 CB registers)
  VK_KHR_present_wait + VK_KHR_present_id (zero-latency frame pacing)
  VK_EXT_conservative_rasterization
  VK_EXT_depth_clip_control (Depth Bounds Test)
  VK_KHR_fragment_shading_rate (VRS)
  VK_KHR_ray_query (inline ray tracing from Compute)
  VK_KHR_opacity_micromap (OMM, hardware alpha-test in RT)
  VK_NV_ray_tracing_invocation_reorder (SER)
  VK_KHR_cooperative_matrix (tensor cores, FP16/INT8)
  VK_EXT_pageable_device_local_memory
  VK_EXT_host_image_copy (UI tex → VRAM, no staging) 
  VK_KHR_shader_maximal_reconvergence
  VK_KHR_shader_subgroup_extended_types
  VK_EXT_global_priority (3-queue priorities)
  VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT (Sparse)

VULKAN_1.4_2026_EXTENSIONS: 
  VK_EXT_descriptor_heap (NEW 2026: complete descriptor system replacement, console-like memory access, supersedes descriptor_buffer)
  VK_KHR_device_address_commands (NEW 2026: GPU command generation via direct memory addresses, CPU-free command processing)
  VK_KHR_ray_tracing_position_fetch (accurate ray hit position without any-hit shader)
  VK_KHR_fragment_shader_interlock (atomic fragment operations, OIT without sorting)
  VK_EXT_shader_atomic_float (atomicAdd/sub/min/max on float16/float32/float64)
  VK_EXT_shader_image_atomic_int64 (64-bit atomic operations on images)
  VK_KHR_shader_expect_assume (branch prediction hints: __builtin_expect for GPU)
  VK_EXT_descriptor_buffer (bindless without descriptor sets, BDA-only access - DEPRECATED by descriptor_heap)
  VK_NV_low_latency2 (NVIDIA Reflex integration, reduced input latency for 8000Hz mice)
  VK_KHR_cooperative_matrix 2.0 (matrix multiply accumulate, FP8/INT4 support)
  VK_EXT_mesh_shader extensions (multi-draw meshlet amplification, task shader workgroups)
  VK_KHR_dynamic_rendering_local_read (read current render target in same pass)
  VK_EXT_graphics_pipeline_library (pipeline caching, faster shader compilation)
  VK_KHR_maintenance5 (descriptor indexing improvements, robustness)
  VK_EXT_attachment_feedback_loop_layout (feedback loops without separate passes)
  VK_KHR_external_fence_win32 (cross-process GPU sync)
  VK_EXT_sample_locations (programmable sample positions for MSAA)
  VK_KHR_load_store_op_none (no-op load/store for unused attachments)
  VK_EXT_nested_command_buffers (command buffer reusability)
  VK_EXT_pipeline_properties (pipeline compilation feedback)
  VK_KHR_shader_subgroup_uniform_control_flow (improved subgroup divergence handling)
  VK_EXT_image_2d_view_of_3d (2D view of 3D texture slice, no copy)
  VK_KHR_swapchain_mutable_format (dynamic swapchain format change)
  VK_EXT_hdr_metadata (HDR metadata for displays)
  VK_EXT_color_write_enable (per-color-channel write masks)
  VK_EXT_depth_bias_control (programmable depth bias granularity)
  VK_EXT_line_rasterization (accurate line rendering, Bresenham)
  VK_EXT_provoking_vertex (provoking vertex control for flat shading)
  VK_EXT_shader_tile_image (tile-based access to framebuffer in fragment shader)
  VK_EXT_primitive_topology_list_restart (triangle strip with restart)
  VK_KHR_shader_float16_int8 (native FP16/INT8 in shaders)
  VK_KHR_zero_initialize_workgroup_memory (zero-init LDS without explicit stores)
  VK_KHR_workgroup_memory_explicit_layout (explicit LDS layout for bank conflict avoidance)
  VK_EXT_legacy_vertex_attributes (legacy vertex attribute formats)
  VK_KHR_pipeline_library (pipeline binary caching across applications)
  VK_EXT_descriptor_indexing (partially bound descriptors, update-after-bind)
  VK_EXT_image_view_min_lod (min LOD clamp for texture streaming)
  VK_EXT_shader_atomic_float2 (vector atomic operations)
  VK_EXT_opacity_micromap (displacement micromaps for micro-geometry RT)
  VK_NV_shader_subgroup_partitioned (NEW 2026: dynamic wave partitioning for material binning, zero divergence penalty)
  VK_NV_compute_shader_derivatives (NEW 2026: dFdx/dFdy in compute shaders for visibility buffer texturing)
  VK_EXT_image_compression_control (NEW 2026: hardware compression control for HDR/shadow buffers, bandwidth optimization)
  VK_EXT_attachment_feedback_loop_dynamic_state (NEW 2026: dynamic feedback loops for post-processing without intermediate copies)
  VK_KHR_calibrated_timestamps (NEW 2026: microsecond-accurate CPU/GPU time correlation for Tracy profiler)
  VK_AMDX_shader_enqueue (NEW 2026: Hardware Work Graphs - GPU autonomous task scheduling, zero CPU involvement)
  VK_KHR_pipeline_binary (NEW 2026: native shader binary caching, eliminates shader compilation stutter)
  VK_KHR_internally_synchronized_queues (NEW 2026: parallel queue submission from multiple threads, driver-managed sync)
  VK_NV_push_constant_bank (NEW 2026: separate constant banks, zero contention for per-shader data)
  VK_KHR_shader_constant_data (NEW 2026: isolated constant memory, L1 cache preservation)
  VK_KHR_shader_abort (NEW 2026: soft shader abort on error, prevents GPU hangs)
  VK_EXT_shader_long_vector (NEW 2026: long math vectors in registers, physics simulation optimization)
  VK_NV_compute_occupancy_priority (NEW 2026: compute occupancy priority, critical shaders get maximum resources)
  VK_KHR_shader_float_controls2 (NEW 2026: absolute float behavior control, fast rounding for PBR speed)
  VK_KHR_shader_subgroup_rotate (NEW 2026: subgroup data rotation, eliminates LDS for blur/neighbor operations)
  VK_KHR_shader_relaxed_extended_instruction (NEW 2026: relaxed math variants, hardware-accelerated alternatives)
  VK_EXT_shader_replicated_composites (NEW 2026: hardware structure replication, single-tact composite construction)
  VK_NV_raw_access_chains (NEW 2026: raw memory access chains, bypass complex addressing for SSBO)
  VK_NV_memory_decompression (NEW 2026: hardware texture decompression, UASTC support, zero CPU overhead)

QUEUE_TOPOLOGY: 3 queues 
  Graphics: REALTIME priority — geometry, opaque pass, UI
  Async_Compute: HIGH priority — culling N+1, shadows, particles, skinning
  DMA_Transfer: MEDIUM priority — DirectStorage, SVT streaming (24/7)
  Video_Decode: LOW priority — video texture decoding via VK_KHR_video_queue 
  External_Sync: VK_KHR_external_fence_win32 for cross-process GPU sync 

IO: DirectStorage (NVMe→PCIe→VRAM, bypasses CPU) 
  Fallback: Thread-pool decompression via Fiber Jobs (SATA/legacy)
  Format: GDeflate 64 KB chunks + .bhdict
  Hardware_decompress: GPU decompresses directly to VRAM

COMMAND_BUFFERS: VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT (always, DGC rebuilds every frame) 
  Nested_Command_Buffers: VK_EXT_nested_command_buffers (command buffer reusability for repeated passes) 
  Command_Pool_Recycling: Reuse command pools to reduce allocation overhead 
FRAME_LOOP: vkWaitSemaphores() on Timeline Semaphore (not sleep_for) 
  Present: VK_KHR_present_wait; CPU wakes on display scanout interrupt
  Input_read: immediately after wake, 1ms before CB record

SHADER_MICRO-OPTIMIZATION:
  Target_Metric: Occupancy (Wave fill rate). Goal: 100% ALU utilization
  VGPR_Reduction: Scope_limiting; Matrix_ban (no mat4); Zero-reg_pack (uint norm_rough_metal via packHalf2x16); Budget: ≤64 VGPR
  Subgroup_Replace_Shared: subgroupQuadSwapHorizontal/Vertical; subgroupMin for bindless read; subgroupBallot replaces atomicAdd
  Branchless: float mask = step(threshold, val); result = mix(a, b, mask); VK_KHR_shader_maximal_reconvergence
  FP16_Global: GL_EXT_shader_explicit_arithmetic_types_float16; Exceptions: WorldPos, Depth → float32 only
  FP8_INT8_Optimization: VK_KHR_cooperative_matrix 2.0; FP8 for weights, INT8 for indices (tensor cores)
  Transcendental_Elimination: pow(x,5): x2=x*x; x4=x2*x2; return x4*x → 3 FMA; sin/cos: Taylor series or 1D LUT
  Memory_Coalescing: pack structs into uvec4 → single 128-bit OpLoad per cache line
  ILP_Pattern: Issue texture fetch → Execute 10–15 ALU instructions → Apply fetched data
  Loop_Unroll: pass iteration count as Specialization Constant + #pragma unroll
  LDS_Bank_Conflict_Fix: shared float data[1024 + 32]; data[index + (index / 32)]
  Demote_VS_Discard: VK_EXT_shader_demote_to_helper_invocation (no broken dFdx/dFdy)
  Subgroup_Size_Control: VK_EXT_subgroup_size_control; VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT
  Texture_Filter_Fallback: VK_FILTER_LINEAR; FidelityFX CAS pass in Uber-Post
  IO_Fallback_Ring_Buffer: HOST_VISIBLE | HOST_COHERENT ring buffer in RAM
  Expect_Assume_Branching: VK_KHR_shader_expect_assume; assume(condition) for likely branches
  Atomic_Float_Reductions: VK_EXT_shader_atomic_float; atomicMin/Max on float for min/max reduction
  Shader_Interlock_OIT: VK_KHR_fragment_shader_interlock; ordered independent transparency without sorting
  Zero_Init_LDS: VK_KHR_zero_initialize_workgroup_memory; automatic LDS zero-init (no manual stores)
  Explicit_LDS_Layout: VK_KHR_workgroup_memory_explicit_layout; avoid bank conflicts via padding
  Tile_Image_Access: VK_EXT_shader_tile_image; direct framebuffer access in fragment shader (deferred shading optimization)
  Sample_Location_Control: VK_EXT_sample_locations; programmable MSAA sample positions for better edge quality
  Load_Store_None: VK_KHR_load_store_op_none; skip load/store for unused attachments (depth-only passes)
  Float16_Int8_Native: VK_KHR_shader_float16_int8; native FP16/INT8 arithmetic (no emulation)
  Image_Atomic_Int64: VK_EXT_shader_image_atomic_int64; 64-bit atomics for global counters in images
  Subgroup_Partitioned: VK_NV_shader_subgroup_partitioned; dynamic wave partitioning for material binning (zero divergence)
  Compute_Derivatives: VK_NV_compute_shader_derivatives; dFdx/dFdy in compute shaders for visibility buffer texturing
  Float_Controls2: VK_KHR_shader_float_controls2; absolute float behavior control, fast rounding for PBR
  Subgroup_Rotate: VK_KHR_shader_subgroup_rotate; subgroup data rotation, eliminates LDS for blur/neighbor ops
  Relaxed_Instructions: VK_KHR_shader_relaxed_extended_instruction; relaxed math variants, hardware-accelerated
  Replicated_Composites: VK_EXT_shader_replicated_composites; hardware structure replication, single-tact construction
  Raw_Access_Chains: VK_NV_raw_access_chains; raw memory access, bypass complex addressing for SSBO
  Long_Vector: VK_EXT_shader_long_vector; long math vectors in registers, physics simulation optimization
  Scalarize_Waterfall: matID == subgroupMin(matID) waterfall loop for L1 bindless read
  Constant_Bank_Isolation: VK_NV_push_constant_bank / VK_KHR_shader_constant_data; separate constant banks, zero contention

SHADER_COMPILATION:
  Cooker_Precompile: SPIR-V micro-database compiled offline
  Warmup: loading screen, Fiber Job System, vkCreateShadersEXT for all known permutations
  Generic_Fallback: substitute Generic PBR shader immediately; background thread completes compilation → atomic BDA pointer replace
  Pipeline_Library_Caching: VK_KHR_pipeline_library (cache pipeline binaries across applications) 
  Pipeline_Properties_Query: VK_EXT_pipeline_properties (get compilation feedback for optimization) 
  Graphics_Pipeline_Library: VK_EXT_graphics_pipeline_library (faster shader compilation via pipeline caching) 
  Pipeline_Binary_Caching: VK_KHR_pipeline_binary (native shader binary caching, eliminates compilation stutter) 
  Shader_Abort: VK_KHR_shader_abort (soft shader abort on error, prevents GPU hangs) 

SHADER_RULES:
  Specialization_constants: layout(constant_id=X) for RT/SDF/Tensor/Quality toggles (no #ifdef)
  Register_budget: <64 VGPR per lighting shader
  Pack_method: uint normal_rough_metal; packHalf2x16+packSnorm2x16; unpack inline in fma
  Transcendentals: sin/cos/pow → FMA polynomial approximation
  Branching: step()/mix() preferred; if-else only with maximal_reconvergence
  Quad_derivs: subgroupQuadSwapHorizontal/Vertical (1 clock, no rasterizer)
  Wave_scalarize: matID == subgroupMin(matID) waterfall loop for L1 bindless read
  FP16_enforce: --hlsl-enable-16bit-types; float16_t everywhere except WorldPos+Depth
  Matrix_size: 4×3 or quaternion only (no 4×4 matrices in shaders)
  Scope_limit: { } blocks for variable aliasing → register reuse
  Cooperative_matrix: VK_KHR_cooperative_matrix for skinning, FFT-water, cloth

---

## МАТЕРИАЛЫ И BRDF (MATERIALS & BRDF)

CORE_BRDF: Multi-Scattering GGX (energy conservation)
SKIN_SSS: Random Walk SSS
CLOTH_HAIR: Charlie BRDF (Sheen)
DUAL_LOBE: Dual GGX (wide+narrow specular for skin/plastic)
HAIR_COLOR: Melanin physical model + UV Root-to-Tip
EYE: Iris Depth Parallax (cornea refraction) + Limbal Ring Specular Mask
OCEAN: FFT displacement + Single Scattering + Depth Extinction + Jacobian foam
TERRAIN_BLEND: height-based (sand fills stone depth cavities, not linear alpha)
TSS: Texture Space Shading (heavy grass/fur lighting cached to texture via Compute pre-pass)
DECAL_DBUFFER: D-Buffer decals (correct normal-aware lighting), ClampToBorder sampler
DIRECTIONALOCC: dot(dominantLightVec, NormalMap) → micro-shadow in 2 FMA instructions

LOW_END_FALLBACKS (Specialization Constant, not separate shaders):
  Smith_masking → Schlick-GGX
  Multi_scatter → Albedo correction scalar
  BRDF_LUT → analytical curve: color*(F0*x + y)
  Detail_normal/Clearcoat/Anisotropy: skipped if distance > 15m

---

## RAY TRACING И ГЛОБАЛЬНОЕ ОСВЕЩЕНИЕ (RT & GI)

RT_INLINE: VK_KHR_ray_query from Compute Shader (no Any-Hit/Closest-Hit shaders) 
RT_FLAGS: gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT (boolean shadow only) 
OMM: VK_KHR_opacity_micromap (foliage/alpha-test hardware rejection in RT core) 
SER: VK_NV_ray_tracing_invocation_reorder (reorders divergent material hits) 

RESTIR_DI: 
  Reservoir: struct { uint LightID; float Weight; float TargetPdf; float RayDistance; }
  Passes: Spatial reuse (4 neighbors) + Temporal reuse (prev frame)
  Result: 1 shadow ray/pixel supports 100k+ lights

RESTIR_GI:  (spatiotemporal reservoir reuse for indirect illumination)

HASH_GRID_GI (World-Space Radiance Cache): 
  Storage: SSBO hash table (PCG Hash or MurmurHash3 on XYZ)
  Cache_hit: immediate read; cache_miss: compute + atomic write
  Eviction: FrameIndex-based decay
  Light_Leak_Prevention: VK_KHR_ray_query validation before cache read (block light through thin walls) 

SURFEL_GI: 
  Surfel: { vec3 Position; uint PackedNormal; uint PackedIrradiance_RGBE; float Radius; } alignas(16)
  Spawn: on visible geometry surface (VisBuffer read)
  Update: Async Compute, short VK_KHR_ray_query rays from surfels
  Sample: pixel blends 3–4 nearest surfels via spatial hash
  Leak_reject: dot(PixelNormal, SurfelNormal) < 0 → weight = 0

 

SG_LIGHTMAPS: 
  Format_color: VK_FORMAT_BC6H_UFLOAT_BLOCK (HDR)
  Format_dir: VK_FORMAT_BC5_UNORM_BLOCK (direction+sharpness)
  Access: BDA pointer, 1 instruction read
  Stream: DirectStorage 64 KB chunks per SVT table
  Specular: preserved mathematically (no RT needed for glossy from baked)

DDGI_PROBES:  (dynamic probes for NPC; inject static SG lightmap bounce)
BENT_NORMALS:  (sky accessibility + AO baked in .bhmesh)


DENOISING: SVGF (Spatiotemporal Variance Guided Filtering) + A-SVGF (Adaptive SVGF)
  Technique: Analyzes color variance; if pixel differs from neighbors → it's "noisy"
  Smoothing: Gradually spreads noise using Motion Vectors and Depth-Weighting
  Result: Near AI-denoiser quality, 10x faster, no training required

FULL_PATH_TRACING: 
  Rays_per_pixel: 0.25 (Base)
  ReSTIR_GI: Spatio-Temporal Resampling
  ReSTIR_PT: Path Tracing with temporal/spatial reuse (multi-bounce light transport) 
  Bounces: Infinite via Radiance Caching (World-Space Hash Grid)
  SER: VK_NV_ray_tracing_invocation_reorder bundles rays by Material ID
  OMM: VK_KHR_opacity_micromap (hardware bitmask for alpha-test)
  DMM: Displacement Micromaps (micro-geometry integrated into BVH)
  Denoising: SVGF (Spatiotemporal Variance Guided Filtering)
  Position_Fetch: VK_KHR_ray_tracing_position_fetch (accurate hit position without any-hit shader) 
  Ray_Tracing_Pipeline_Cache: VK_KHR_pipeline_library (cross-frame pipeline caching) 
  Adaptive_RT: Checkerboarding (0.5 rpp) + VRS-guided RT (2x2 in shadows/motion blur) 

---

## ТЕНИ (SHADOWS)

VSM (Virtual Shadow Maps): 
  Page_size: 128×128 px
  Allocation: Sparse Binding vkBindImageMemory2 (empty pages → null physical block)
  Invalidation: Compute Shader projects dynamic object BoundingSpheres → atomic isDirty flag
  Render: vkCmdDrawMeshTasksIndirectEXT for isDirty==1 pages only
  Static_shadows: baked once, zero redraw cost

SMRT (Shadow Map Ray Tracing on VSM): 
  Technique: Ray Marching along light vector through VSM Clipmap mip hierarchy
  Mip_skip: empty mip → jump large space in 1 clock
  Result: contact-hardening soft shadows
  Micro_Shadows: DMM micro-shadows for displacement micromaps (cracks, brick details) 

RSM (Reflective Shadow Maps — no RT): 
  Shadow_pass: simultaneously writes Flux (albedo) + Normal to extra RTs
  Compute_VPLs: 16–32 Poisson-disk samples around projected pixel in shadow tex
  Math: each sample = Virtual Point Light → additive GI contribution

CAPSULE_SHADOWS (crowd/NPCs, no shadow maps): 
  Input: .bhbone Jolt capsule data
  Formula: analytical cone-capsule intersection in Compute lighting shader
  Cost_geo: 0 polygons | Cost_VRAM: 0 bytes

SCREEN_CONTACT_SHADOWS: ⚠️ PARTIAL (C++ обвязка готова, шейдер не готов)
  Steps: 8–12 Ray March over linear depth buffer
  Direction: toward light source
  Thickness_heuristic: prevents back-surface shadowing

---

## ПОСТОБРАБОТКА (POST-PROCESSING)

UBER_POST: single Compute Shader, 1 pixel read 
  Tonemapping: AgX, linear space 
  Exposure: Subgroup Histograms, ~0.01 ms 
  Color_grade: 3D LUT, VK_FORMAT_A2B10G10R10_UNORM_PACK32, min16float math 
  Vignette: math only 
  Film_grain: PCG_Hash(uvec3(x,y,time)), 2 ALU clocks, no texture 
  DoF: tile-classify 16×16; in-focus tile → Wave return (skip) 
  Bloom: subgroupQuadSwap (Kawase in registers, no shared memory); red channel isolated for Halation 

COMPRESSION_BUFFERS: SSR, SSGI → YCoCg (Luma 16-bit, Chroma 8-bit) 

VRS: ⚠️ PARTIAL (VRS есть, автоматическое применение не реализовано)
  Extensions: VK_KHR_fragment_shading_rate 
  Rates: 1×1 (full), 1×2, 2×2, 4×4 
  Apply: motion blur zones, deep shadow, screen periphery 
  Foveated: fovea 1×1, periphery 4×4 
  Motion-VRS: fast objects → 2×2 

GTAO: ⚠️ PARTIAL (C++ обвязка готова, шейдер не готов)
  Deinterleaved (checkerboard), HZB-stepped ray 

HZB_SSRT: screen-space reflections via HZB pyramid ray step 
SDF_FACES: 2D SDF in face texture; dot(Light,Forward) as threshold → vector shadow 
CURVATURE_WEAR: fwidth(Normal) → edge wear (ALU, no texture if not pre-baked) 
MESH_OUTLINE: meshlet normal-inflate (Inverted Hull), hardware 

FROXEL_VOLUMETRICS: ⚠️ PARTIAL (C++ обвязка готова, шейдер не готов)
  Grid: 160×90×64, logarithmic Z 
  Update: Async Compute (async with graphics queue) 
  Ray_march: through froxels + VSM ray-march shadows 
  Temporal: 3D TAA with velocity-shifted history buffer 
  Multi_scatter_LUT: 2D transmittance+inscatter texture 
  Multiple_Scattering: VK_KHR_cooperative_matrix for dense fog (milk-like) simulation 
  VSM_Integration: Volumetric light reads VSM for god rays 

ATMOSPHERE_LUT: precomputed Transmittance + In-scatter 2D texture 

GSAA: length(fwidth(Normal)) → dynamic Roughness boost (specular AA on distant high-poly) 
RNM_BLEND: Reoriented Normal Map via quaternion or RNM Whiteout method 
DERIV_CLAMP: SVT UV seam gradients clamped before page table read 
TEXTURE_GATHER: OpImageGather for 2×2 shadow PCF 

SAFE_MATH:
  Normal Z restore: sqrt(max(1.0 - dot(xy,xy), 0.0)) 
  GGX denom: max(Roughness, 0.045) 
  UDIM layer: clamp(uint(floor(UV.x)) + uint(floor(UV.y))*10, 0, MAX_LAYERS-1) 

SSR_HZB: ray jumps across HZB mip levels; Fallback (4 steps, no hit): trilinear sample from nearest baked Reflection Probe 
  Hybrid_SSR_RT_Fallback: Primary SSR → RT fallback for off-screen reflections 
  SDF_Fallback: Global SDF step for off-screen reflections (no RT cores) 
CONTACT_SHADOWS_VARIABLE: Steps_formula proportional to depth variance in tile; Budget: <0.5 ms 
LENS_FLARE_MESH: Compute Shader finds pixels > 10.0 nits → writes XY to SSBO; vkCmdDrawIndirect → procedural Quads 

DYNAMIC_RESOLUTION: 
  GPU_Timing: vkCmdWriteTimestamp2 per render pass
  PID_Controller: Target configurable; adjust internal render resolution via Render Graph aliasing; Range: 50%–100%
  Class: ResolutionManager queries vkGetQueryPoolResults → adjusts VkImage sizes
  Image_View_Min_LOD: VK_EXT_image_view_min_lod (min LOD clamp for texture streaming without mipmap generation) 

UPSCALERS:  (external SDKs)
  DirectSR (Microsoft Agility SDK) → auto-selects FSR/DLSS/XeSS per GPU
  DLSS 4: NVIDIA Streamline SDK
  FSR 4/3.1: FidelityFX SDK
  XeSS: Intel SDK
  Native TSR fallback: own implementation (YCoCg subgroup variance clipping + Halton jitter)
  Latency_Reduction: VK_NV_low_latency2 (NVIDIA Reflex integration) 

FRAME_GENERATION: 
  Velocity_source: per-pixel motion vectors including skinned meshes + particles
  UI_split: Render Graph separates 3D world from UI layer
  UI_composition: UI drawn after frame gen

HDR_&_DISPLAY_OUTPUT: 
  Swapchain_Format_Priority:
    1. VK_FORMAT_A2B10G10R10_UNORM_PACK32 + VK_COLOR_SPACE_HDR10_ST2084_EXT (HDR10, ST.2084 PQ)
    2. VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT (scRGB linear, Windows Auto-HDR, OLED)
    3. VK_FORMAT_B8G8R8A8_SRGB + VK_COLOR_SPACE_SRGB_NONLINEAR_KHR (SDR fallback)
  HDR_Math: Workspace: linear scRGB; Tonemapper: AgX; Bright_emitters: up to 1000+ nits; OLED_black: 0.0001 nit
  Display_Shader: presentation pixel shader converts linear nits to ST.2084 PQ curve
  HDR_Metadata: VK_EXT_hdr_metadata (HDR10+ metadata for displays) 
  Swapchain_Mutable_Format: VK_KHR_swapchain_mutable_format (dynamic format change without recreation) 

STYLIZATION_&_NPR: 
  Precision_Rule: float16_t for all color math; float32 for Depth/Position
  Branching_Rule: Specialization Constants per effect
  Kuwahara_Anisotropic: Flow_field from Normal/Velocity; Shared Memory 16×16 blocks; Temporal HistoryBuffer
  Hatching_Halftone: WorldPosition (Triplanar); Math_density: sin/cos; Evaluation: if Luminance < density → black
  Edge_Detection: InstanceID diff via Visibility Buffer; Sobel filter on Normal/Depth; Resolution: 0.5x → Depth-Aware Upsampling
  Animation_Aesthetics: Step_Frame multiplier; Motion_Smear via Velocity Buffer
  Retro_Pixelation: Edge-Preserving Upscaling; Blue Noise Dithering
  Lens_Distortion_Math: uv + direction * (dist^2); Chromatic_Aberration: length(uv - 0.5) channel offset

---

## UI И ТИПОГРАФИКА (UI & TYPOGRAPHY)

UI_FRAMEWORK: ImGui (Docking Branch) 
GIZMOS: ImGuizmo (3D translate/rotate/scale manipulators) 
FILE_DIALOG: nfd-extended (native OS open/save dialogs) 
LAYOUT_ENGINE: Yoga/Meta (Flexbox C++, auto layout) 
VECTOR_ANIM: Rive C++ Runtime (GPU vector animation via Vulkan) 
TEXT_SHAPE: HarfBuzz (Arabic, CJK, ligatures) 

FONT_FORMAT: .BHFONT 
  Technology: MSDF 2.0 (Multi-channel Signed Distance Field)
  Shaping: HarfBuzz GPU (Compute Shader text layout)
  Anti_aliasing: VK_KHR_fragment_shader_barycentric (subpixel edge reconstruction without TAA)

UI_PRIMITIVES:
  Storage: Bindless array of UIPrimitive structs (SSBO)
  Draw_call: 1 DrawIndirect per UI layer
  Shapes: Vector Bezier curves evaluated analytically in fragment shader
  Clipping: Bitmask / AABB intersection in shader (no VkCmdSetScissor)

COMPOSITING:
  Frosted_Glass: VK_KHR_dynamic_rendering_local_read (read L1 cache color directly)
  Luma_Stealing: UI reads background Mip level → auto-inverts text color
  Resolution_Scale: UI natively 4K; 3D scene scales dynamically via PID controller
  Feedback_Loop_Layout: VK_EXT_attachment_feedback_loop_layout (feedback loops without separate passes) 

LATENCY_OPTIMIZATION:
  Cursor: Hardware Cursor Sync tied to VK_KHR_present_wait
  Input_thread: High-priority OS thread → Shared Buffer (GPU reads at frame start)
  Caching: Dirty Rect Cache via VK_EXT_host_image_copy
  NVIDIA_Reflex: VK_NV_low_latency2 (reduced input latency, GPU sleep/wake optimization) 
  Late_Latching: Matrix update immediately before vkQueueSubmit (reduces motion-to-photon latency) 

---

## ЗВУК (AUDIO)

AUDIO: miniaudio (header-only, low-latency, DSP filters) 
RT_AUDIO: ray-traced audio via acoustic voxel octree 

FORMAT: .BHAUD 
  SFX (short): ADPCM compressed (4:1), loaded fully into RAM/VRAM
  Streams (music/dialogue/ambient): Opus codec; streamed in 64 KB chunks via DirectStorage

STREAMING: AudioStreamer uses same I/O interface as textures; reserves pinned RAM ring buffer 

SPATIAL_AUDIO_HIGH_END (GPU): 
  Input: Acoustic Voxels from .bhmesh
  Queue: Async Compute, VK_KHR_ray_query micro-traces
  Reflection: rays reflect off surfaces; material tag → LP filter cutoff
  Output: Impulse Response → CPU Convolution Reverb

SPATIAL_AUDIO_LOW_END (CPU): 
  Method: Jolt RaycastBatcher
  Logic: ray hits wall → read physical material tag → apply DSP filter preset
  Function: CalculateAudioOcclusion() → LPF coefficients

DSP_MIXING: 
  Model: directed graph (DSP Graph), nodes = filters/reverb/generators
  Traversal: no virtual functions; strict SIMD batch processing (AVX2/AVX-512)
  Threading: Fiber Job System distributes 10ms audio buffer chunks across all free cores
  HRTF: Head-Related Transfer Function for headphone spatial audio; AVX vectorized

PHYSICS_AUDIO_SYNC: 
  Trigger: Jolt ContactEvent → struct { Hash audioID; vec3 pos; float impactSpeed; } → Lock-Free Event Bus
  Processing: Audio system reads batch from queue; applies Pitch + Volume + Transient Shaper
  Animation_sync: sound cue hashes from .bhanim events

---

## VFX И ЧАСТИЦЫ (VFX & PARTICLES)

GPU_PARTICLE_SYSTEM: 
  Compute Shader, fixed-size arrays, bitwise AND for modulo
  PBD_GPU: Position Based Dynamics in Compute Shader
  Particle_SDF_Collisions: baked scene SDF (R8_UNORM 3D texture)
  Atomic_Float_Particles: VK_EXT_shader_atomic_float for particle physics (atomic min/max on float positions) 

VOLUMETRICS: NanoVDB/NVIDIA (sparse volumes, GPU ray march, R16F 3D tex) 
NOISE_PCG: FastNoiseLite (terrain, clouds, destruction) 
LEVEL_GEN: LibWFC (Wave Function Collapse, tile-logic) 

---

## AI И НАВИГАЦИЯ (AI & NAVIGATION)

NAVIGATION_DATA: .BHNAV 
  Format: 3D Flow Fields (Morton ordered distance fields)
  Pathfinding: Unit reads velocity vector from 3D texture based on WorldPos
  Dynamic_obstacles: Jolt Physics writes AABB → Async Compute boolean subtract

BEHAVIOR_GOAP (Vectorized): 
  Structure: Bitmasks for world state and goals
  Evaluation: subgroupBallot() + findMSB() (1000 agents processed in 1 Wave)
  Logic_fallback: FSM (Finite State Machine) batched 64 units per Wave

 

SENSORY_SYSTEM: 
  Vision: SDF-Vision (Ray-less). Evaluates Global SDF distance at player coordinate
  Occlusion_Cost: 1 texture read (0 physics raycasts)

LOCAL_AVOIDANCE: 
  Algorithm: GPU RVO (Reciprocal Velocity Obstacles)
  Execution: Compute Shader analyzes neighbors in radius → output offset vector
  Integration: Write directly to Jolt ReBAR kinematic targets

LOD_SCALING: 
  Visibility: Checked via HZB pyramid
  Out_of_sight: Update frequency drops to 10 Hz; Animation disables

---

## ВИРТУАЛЬНАЯ ГЕОМЕТРИЯ И ТЕРРЕЙН (VIRTUAL GEOMETRY & TERRAIN)

VIRTUAL_GEOMETRY_DATA: .BHVIRT 
  Structure: Directed Acyclic Graph (DAG) of Meshlet Clusters (128 triangles each)
  Precision: 8/10/12-bit quantized local coordinates
  Metric: Target projection error < 0.5 pixel

LOD_SELECTION (Task Shader): 
  Input: Camera matrix, HZB, DAG root
  Evaluation: Wave32/64 evaluates 32 clusters/clock
  Culling: Frustum + Backface cone + HZB Occlusion
  Command_Gen: Device Generated Commands (DGC) writes to draw SSBO
  Image_2D_View_of_3D: VK_EXT_image_2d_view_of_3d (2D view of 3D texture slice for terrain heightfield without copy) 

HYBRID_RASTERIZATION: 
  Hardware_Path: Triangle > 4 pixels → standard pipeline
  Software_Path: Triangle < 4 pixels → Compute Shader Software Rasterizer
  VisBuffer_Write: R64_UINT (32-bit Depth | 32-bit ID: Instance+Cluster+Triangle) via atomicMax

STREAMING (GPU PAGE FAULTS): 
  Cache_Miss: GPU writes ClusterID to Request SSBO
  Bypass: DMA Queue reads SSBO → DirectStorage fetches from NVMe to VRAM

TERRAIN_SYSTEM: .BHTER 
  Geometry: Virtual Heightfield Mesh (VHM) + Clipmaps
  Tessellation: Task Shader curvature analysis (flat = 1 tri, cliff = dense meshlets)
  Texturing: Sparse Virtual Texturing (SVT) 128K x 128K; Triplanar Auto-Mapping

FOLIAGE_INSTANCING: 
  Placement: Compute Shader + Biome Seed → Matrix4x3 array
  Culling: Biome Radius → Frustum → HZB
  LOD: Distance > threshold → Shadow Impostors or 3D Gaussian Splats

---

## ПОГОДА И АТМОСФЕРА (WEATHER & ATMOSPHERE)

SKY_DOME: 
  Scattering: Analytical Rayleigh/Mie via 32x128 LUTs
  Multiple_Suns: Supported, each injects vector into ReSTIR DI
  Stars: Compute Shader PCG generation + Perlin noise scintillation

VOLUMETRIC_CLOUDS: .BHCLOUD 
  Algorithm: Sparse Ray-Marching (1/4 or 1/16 screen res)
  Optimization: Bitmask Culling (skips empty 3D space)
  Upsampling: Temporal Reconstruction with velocity re-projection
  Shadows: Asynchronous 2D Shadow Mask generation
  Cooperative_Matrix_Lighting: VK_KHR_cooperative_matrix for cloud lighting (tensor cores) 

WEATHER_SYSTEM: 
  Wind: GVF (Global Vector Field) 3D Texture (3 frequencies)
  Precipitation: Mesh Shader particles (lines/streaks), Global SDF collision
  Fog: Froxel 3D grid, Subgroup Integration for light scattering

FLUID_SIMULATION: .BHSIM 
  Macro_Water: Shallow Water Equations (SWE) in Compute (2D surface, Jolt impulses)
  Micro_Splash: Hybrid SPH (Smoothed Particle Hydrodynamics)
  Gas/Fire: Sparse VDB (Sparse Volume Data), simulates only active voxels
  Jolt_Coupling: Fluid velocity vectors written to Jolt ReBAR force maps
  Cooperative_Matrix_Fluid: VK_KHR_cooperative_matrix for SPH neighbor lookups (tensor cores) 

---

## ДЕФОРМИРУЕМЫЕ ОБЪЕКТЫ (DEFORMABLES)

HAIR_STRANDS: .BHHAR 
  Simulation: Guide Hair XPBD (Extended Position Based Dynamics) in Compute (1-5% guides)
  Generation: Mesh Shader expands guides to ribbons/tubes based on LOD
  Shading: Marschner BRDF (R + TRT scattering)
  Transparency: OIT via VK_KHR_fragment_shader_barycentric + Fragment Density

CLOTH_SIMULATION: 
  Algorithm: Multi-Layer XPBD
  Optimization: Tiled Constraint Solving (16x16 tiles in LDS + Subgroup Partitioning)
  Collision: Global SDF (zero-penetration wrapper) + Spatial Hash self-collision
  Wrinkles: Stress Map → dynamic normal map generation
  Cooperative_Matrix_Constraints: VK_KHR_cooperative_matrix for constraint solving (tensor cores) 

SOFT_BODIES: 
  Algorithm: Cluster-Based PBD (Tetrahedral volume preservation)
  Blend: Linear Blend Skinning (Base) + XPBD (Secondary Jiggle/Inertia)
  Materials: Stiffness LUT in texture atlas
  Cooperative_Matrix_SoftBody: VK_KHR_cooperative_matrix for tetrahedral volume constraints (tensor cores) 

---

## АССЕТ ПАЙПЛАЙН (ASSET PIPELINE)

COOKER_RULES:
  All heavy compute: offline only
  Runtime: zero-copy deserialization
  Chunk_size: 64 KB (GDeflate/Kraken dictionary in header)
  GUID: 64-bit MurmurHash3, no filenames
  Struct_alignment: alignas(16), std430/std140
  CAS: Blake3 content-addressable dedup of micro-chunks
  Shared_dict: .bhdict per material category (+20–30% compression)
  RDO: rate-distortion optimization before BC block packing

TEXTURE_COMPRESS: Basis Universal → KTX2/ETC/BC7 ⚠️ PARTIAL (placeholder в AssetCooker.cpp)
TEXTURE_HDR: TinyEXR (OpenEXR, skyboxes, lightmaps) 
TEXTURE_SIMPLE: stb_image / stb_image_write (editor icons, PNG/JPG/TGA) 
TEXTURE_FORMATS_2026: 
  ASTC_4x4/6x6/8x8: Adaptive Scalable Texture Compression (mobile/console optimized)
  BC7_SRGB_FEATURE: High-quality sRGB compression with perceptual error metrics
  ETC2/EAC: OpenGL ES standard formats
  RDO_Compression: Rate-Distortion Optimization before BC block packing (perceptually optimal)
  Texture_Compression_ASTC_HDR: HDR ASTC compression for HDR textures
  UASTC_Compression: Ultra-high quality compression (replaces Basis Universal placeholder) 
  Hardware_Decompression: VK_NV_memory_decompression (GPU decompresses UASTC, zero CPU) 
  Image_Compression_Control: VK_EXT_image_compression_control (hardware compression control for HDR/shadow buffers) 
FONT_RASTER: FreeType (cooker-side glyph prep) ⚠️ PARTIAL
FONT_FIELD: msdfgen (MSDF glyphs → sharp at any scale) 
COMPRESSION_CPU: Zstd, libdeflate, LZ4/Lizard (RAM-to-RAM) 
COMPRESSION_GPU: GDeflate (NVIDIA/Microsoft reference, hardware GPU decompress) 
COMPRESSION_FLOAT: ZFP (heightmaps, SDF fields, lossless float) 
PCG_WORLDGEN: FastNoiseLite, LibWFC (Wave Function Collapse, tile-based levels) 

ASSET_COOKER_UTILS: 
  AssetCooker.cpp/hpp: cookModel() ✅, cookProceduralTerrain() ✅, cookTexture() placeholder ❌, cookNavMesh() placeholder ❌, cookConvexHulls() placeholder ❌

---

## СЕТЬ И СКРИПТИНГ (NETWORK & SCRIPTING)

NETWORK:
  TRANSPORT: GameNetworkingSockets/Valve (reliable UDP, encryption, DDoS protection) 
  PHYSICS_NET: Yojimbo (deterministic UDP, network physics) 
  PACKET_SERIAL: bitsery (bit-level, replaces Protobuf) 
  Determinism: Jolt Fixed-Point Math fallback
  Interpolation: Network Jitter Buffer (GPU animates/predicts gaps)

SCRIPTING:
  WASM_RUNTIME: Wasmer or Wasmtime (C++/Rust/C# scripts in sandbox) 
  HOT_RELOAD: Clang/LLVM LibTooling (C++ hot-reload + reflection autogen) 

EXECUTION_SANDBOX:
  User_Mods: WebAssembly (WASM) via Wasmer/Wasmtime (Memory isolated)
  Engine_Native: .dll/.so dynamic linking
  Access: ECS Components via SIMD iterators (DOD over OOP)

HOT_RELOAD_PIPELINE:
  Compiler: Embedded Clang/LLVM (Background Incremental Compilation)
  Process: State Serialization → Library Unload/Load → Pointer Patching → State Restoration
  Latency: 100-300ms, execution without dropping a frame

GPU_INTEGRATION:
  Compute_Hooks: Scripts can inject .hlsl/.glsl nodes into Render Graph
  Work_Graphs: Trampoline functions write directly to GPU Device Generated Commands

---

## РЕДАКТОР И ИНСТРУМЕНТЫ (EDITOR & TOOLS)

EDITOR_ARCHITECTURE: 
  Paradigm: Editor = Game (same Render Graph, same ECS). No separate engine application.
  Transitions: Zero-Wait Playback (State Snapshot cloning in RAM, no scene reload).
  Isolation: Editor UI and Game Render on separate Vulkan Queues

ASSET_BROWSER_&_WORKSPACE:
  Thumbnails: DirectStorage streaming directly to VRAM atlases
  Import: Zero-Import Pipeline (reads .usd/.fbx directly, converts in background)
  Search: Tag-based search + Guid-based caching

INTERACTIVITY:
  Selection: VisBuffer Picking (read R32_UINT ID directly from mouse pos, 0 CPU raycasts).
  Modification: Direct Memory Linking (Slider writes to BDA via ReBAR directly).
  Undo/Redo: Persistent Transaction Log (Bit-wise XOR Deltas of ECS components in memory journal).
  Batching: Multi-Select Batching (1 command alters 10,000 objects in VRAM)

CREATIVE_TOOLS:
  Live_Sculpting: Modifies DAG clusters dynamically, Task Shader tessellates brush area
  GPU_Booleans: SDF coalescence (non-destructive subtraction)
  Grease_Pencil: Vector MSDF strokes in 3D space tied to bone matrices

RENDER_CAPTURE:
  RenderDoc API: Native integration. 1-button frame capture directly from Editor UI.

DEBUG_VISUALIZATION (Jolt & Render):
  Method: DrawIndirect arrays. Subsystems write Capsule/Line/Vector structs into SSBO.
  Execution: Single GPU Indirect Draw call at end of frame for all debug lines. Zero CPU iteration.

PROFILER_VIEW: Tracy (CPU+GPU timeline, barriers, tasks, real-time) 
TABLE_LOG: libfort (formatted console tables)

---

## ДИАГНОСТИКА И ТЕЛЕМЕТРИЯ (DIAGNOSTICS & TELEMETRY)

FAULT_TOLERANCE (ZERO-CRASH): 
  VK_EXT_robustness2: robustBufferAccess2/robustImageAccess2 (returns 0 on out-of-bounds, no TDR)
  GPU_Watchdog: Async Compute tasks > 2ms preempted/reset automatically
  Virtual_Frame_Recovery: On GPU fault, load ECS Snapshot from RAM, recreate VkDevice in 1s (seamless) 

TELEMETRY_&_MARKERS: 
  Breadcrumbs: VK_AMD_buffer_marker (writes markerID to Host-Visible buffer before EVERY dispatch/draw)
  GPU_Faults: VK_EXT_device_fault (extracts GPU registers/L1 cache state on crash)
  Timestamps: vkCmdWriteTimestamp2 wrapping every Render Graph node
  Memory_Track: VK_EXT_device_address_binding_report (BDA access tracking for invalid pointers)

CRASH_HANDLING: 
  Stack_trace: Backward-cpp (stack trace with file+line injection) 
  Minidumps: Crashpad (out-of-process minidump submit, works even if heap is corrupted)  (stub)

PROFILER (CPU+GPU): 
  Tool: Tracy Profiler
  Scope: Real-time tracking of CPU fibers, GPU execution times, memory allocations, and lock-free queues.

AUTOMATED_TESTING:
  Method: Snapshot Regression (Bit-Identical frame comparison)
  Safety: Symbolic Execution Pre-pass in Asset Cooker blocks infinite while-loops or div-by-zero

---

## КАМЕРА И ДИСПЛЕЙ (CAMERA & DISPLAY)

PHYSICAL_CAMERA:
  Parameters: Focal Length, T-Stops, Aperture Blades (Bokeh shape)
  Auto_Exposure: Compute Histogram (Subgroup Reductions) → physical eye adaptation
  Artifacts: Rolling Shutter (velocity based), ISO Noise (procedural grain)

CAMERA_DYNAMICS:
  Collision: Jolt Sphere Cast (prevents clipping)
  Smoothing: Spring-Damper system (mass/stiffness parameters)
  Shake: Blue Noise multi-frequency procedural jitter

VIEWPORTS_&_MULTI-CAM:
  Portals/Mirrors: VK_KHR_multiview (geometry duplicated at hardware level)
  PiP (Picture-in-Picture): Rendered at lower resolution with VRS 2x2/4x4
  Foveation: Eye-Tracked VRS (focus = 1x1, periphery = 4x4)
  Prediction: Late Latching (Matrix update immediately before vkQueueSubmit)

---

## РАЗВЕРТЫВАНИЕ И ДОСТУПНОСТЬ (DEPLOY & ACCESSIBILITY)

BUILD_PIPELINE:
  Packaging: Zero-Copy Packaging (align to 64KB NVMe chunks, Blake3 dedup)
  Fat_Binary: Multi-vendor shader compilation (Tensor / Work Graphs / Mobile Tile-based)
  Streaming: Cloud-Link (Play at 5GB downloaded, DirectStorage streams remainder in background)

NETWORKING:
  Determinism: Jolt Fixed-Point Math fallback (cross-client identical physics)
  Interpolation: Network Jitter Buffer (GPU animates/predicts gaps using history)

ACCESSIBILITY (GPU-DRIVEN):
  Colorblind: Uber-Post Spectral Shift filters
  Visibility: High-Contrast ID-based rendering mode (replaces complex PBR)
  Haptics: Synthesis derived from Jolt audio-physics events (Hz to controller motors)

---

## ЗАПРЕЩЕННЫЕ ПАТТЕРНЫ (FORBIDDEN PATTERNS)

FORBIDDEN:
  - OOP / Virtual functions / vtables in the game loop or ECS systems (Use Data-Oriented Design, SoA).
  - Pointers between ECS entities (Use 64-bit GUIDs).
  - new / malloc / std::make_shared in the game loop (Use Frame Arena or TLSF Pools).
  - std::mutex, std::lock_guard between engine subsystems (Use Lock-Free MPMC Event Bus).
  - Direct subsystem-to-subsystem function calls (e.g., Physics calling Audio). Send POD events.
  - OS-level thread sleeping like std::this_thread::sleep_for (Use C++20 coroutine yields/Fibers).
  - std::string operations, concatenation, or regex in the game loop.
  - Using string paths (like "textures/grass.dds") at runtime (Use MurmurHash3 GUIDs).
  - std::ostream / std::cout for logging (Use fmt / std::format / spdlog).
  - Synchronous Disk I/O (All file reads MUST go through DirectStorage or async DMA queues).
  - RTTI (dynamic_cast / typeid) (Use ECS component bitmasks or compile-time static reflection).
  - VkPipeline (use Shader Objects)
  - VkDescriptorSet (use BDA + Push Descriptors or VK_EXT_descriptor_heap)
  - vkAllocateMemory in game loop
  - pow/sin/cos in PBR shaders (use FMA approx)
  - additive normal blending (n1+n2)
  - 4×4 matrices in shaders
  - #ifdef LOW_END in shaders (use Specialization Constants)
  - monolithic vkCmdPipelineBarrier2
  - texture() for 2×2 PCF (use textureGather/OpImageGather)
  - sRGB pow(color,2.2) in ALU (mark texture VK_FORMAT_..._SRGB)
  - CPU read of HOST_VISIBLE upload heap
  - std::this_thread::sleep in main loop
  - atomicAdd where subgroupBallot suffices
  - two simultaneous ALU-bound queue tasks
  - any per-frame string operations (use hashes)
  - discard in alpha-test shaders (use demote, VK_EXT_shader_demote_to_helper_invocation)
  - atomicAdd in culling loops (use subgroupBallot)
  - loop with texture()/BDA access and runtime iteration count
  - shared[] array without +32 padding (bank conflict)
  - ADPCM/Opus decode on render thread (use Fiber Job audio chunks)
  - physics transforms copied via CPU memcpy to VkBuffer (use ReBAR direct write)
  - Jolt fixed-step on render thread (decouple, State Buffering + GPU interpolation)
  - vkCreateShadersEXT during gameplay (warmup on load screen only)
  - SoftBody at >30m (swap to RigidBody cylinder)
  - CCD on all layers (restrict to LAYER_FAST_PROJECTILES only)
  - per-object BLAS rebuild every frame for NPC > 50m
  - OOP / Entity pointers in scripts (use GUIDs and Component arrays)
  - vkAllocateMemory during structural changes (use VMA defrag / DMA bulk moves)
  - If-else chains in Uber-Post stylization (use layout constant_id for dead code elimination)
  - CPU Raycasts for AI vision (use Global SDF distance checks)
  - Synchronous asset imports in editor (must be async / background cooked)
  - Full object copying for Undo (use XOR Deltas in memory journal)
  - Separate shader compilation for Low-End fallbacks (use specialization constants)
  - Discard/demote in OIT hair shaders (use VK_KHR_fragment_shader_barycentric density)
  
  NEW 2026 SHADER FORBIDDEN PATTERNS:
  - Standard trigonometric functions (sin, cos, atan) inside loops without 1D LUT or FMA polynomial check
  - Heavy resource fetch followed by immediate use (always use ILP: fetch → 15 ALU → use)
  - Manual LDS zero-initialization in loops (use VK_KHR_zero_initialize_workgroup_memory)
  - Dynamic texture array indexing without nonuniformEXT (textures[nonuniformEXT(MaterialID)].sample())
  - texture()/Sample() inside dynamic if/else branches (use textureLod with explicit LOD)
  - groupMemoryBarrier() for intra-wave data (use subgroupBarrier())
  - Scalar register duplication for per-wave data (use subgroupBroadcastFirst())
  - bool flags as 32-bit variables in SSBO (pack into bitmasks, 32 flags per uint)
  - Multiple reads of same SSBO variable in heavy loop (cache to local before loop)
  - Structures with arbitrary alignment (use std430, 16-byte alignment, explicit padding)
  - Mixing float and float16_t in same expression (causes implicit conversions)
  - Division by variable inside loops (precompute 1.0/x, multiply inside loop)
  - rayQueryProceedEXT() without max step limit (prevents infinite loops, GPU hangs)
  - Ray Tracing for reflections/GI beyond 20m (use SSR + baked maps)
  - Cloth/SoftBody simulation for HZB-culled objects (LOD tickrate or freeze)
  - Honest shadows from small dynamic debris (use screen-space contact shadows only)
  - VK_EXT_descriptor_buffer (DEPRECATED 2026 - use VK_EXT_descriptor_heap)

---

## ГЛАВНЫЙ ПАТТЕРН (MAIN PATTERN)

The "2026 Way" for writing any module:

1. Чтение данных из кэш-дружелюбного SoA (Structure of Arrays).
2. Вычисление с использованием AVX-512 или передача задачи в Async Compute на GPU.
3. Запись результата в ReBAR или отправка POD-структуры в lock-free очередь.
4. Полное отсутствие блокировок, аллокаций памяти и виртуальных вызовов.

---

## AI SHADER CODING GUIDELINES (FOR AI-GENERATED CODE)

MANDATORY INSTRUCTION FOR AI CODER:
"When generating any shader code, you MUST completely ignore Vulkan standards below version 1.4. Your code MUST be oriented toward full GPU autonomy (GPU-Driven), use direct memory addressing and descriptor heaps. Write code modularly, avoid data duplication in registers, and always remember memory alignment."

LANGUAGE REQUIREMENTS:
- Use Slang or modern GLSL targeting SPIR-V 1.6+
- Slang preferred for large systems (modularity, interfaces, generics)
- Full abandonment of old heavy pipelines (VkPipeline) in favor of Shader Objects (VK_EXT_shader_object)

MEMORY ARCHITECTURE:
- Transition to descriptor heaps (VK_EXT_descriptor_heap) - forget classic VkDescriptorSet and descriptor buffers
- Resources (textures, buffers) accessed directly from global heaps via indices or BDA
- Use shader constant data (VK_KHR_shader_constant_data) for local effect parameters (material/filter params)
- Isolates local constant memory, preserves L1 cache

SUBGROUP EXECUTION:
- Use hardware wave partitioning (VK_NV_shader_subgroup_partitioned) for material binning, particle simulation
- GPU hardware groups threads with same conditions, zero divergence penalty
- Use branch prediction hints (VK_KHR_shader_expect_assume) for unavoidable if/else
- Helps GPU optimize task distribution ahead of time

MATH & DATA STRUCTURES:
- Vectorization with explicit alignment: layout(std430), 16-byte alignment
- Use VK_EXT_shader_long_vector for efficient physics simulation with long data arrays
- Native low precision: GL_EXT_shader_explicit_arithmetic_types, use float16_t/int16_t everywhere possible
- Colors, masks, non-critical params: use half precision

BINDLESS & TEXTURES:
- Dynamic texture indexing: ALWAYS use nonuniformEXT (textures[nonuniformEXT(MaterialID)].sample())
- Dynamic branches: use textureLod with explicit LOD level, precompute gradients
- Never texture()/Sample() inside dynamic if/else

SUBGROUP & EXECUTION:
- Intra-wave data: use subgroupBarrier(), NEVER groupMemoryBarrier()
- Per-wave scalars: use subgroupBroadcastFirst() for common data (sun position, camera params)
- Subgroup rotate (VK_KHR_shader_subgroup_rotate): eliminates LDS for blur/neighbor operations

MEMORY & BUFFERS:
- Bool flags: pack into bitmasks (32 flags per uint), never 32-bit bool variables
- Loop variables: cache SSBO reads to local before loop start
- Alignment: strict std430, 16-byte alignment, explicit padding
- Raw access chains (VK_NV_raw_access_chains): bypass complex addressing for SSBO

MATHEMATICS:
- Division: precompute 1.0/x before loop, multiply inside
- Float mixing: avoid float/float16_t mixing in same expression
- Trigonometry: sin/cos/atan in loops → 1D LUT or FMA polynomial
- Float controls: VK_KHR_shader_float_controls2 for fast rounding in PBR

RAY QUERY:
- ALWAYS add max step limit to rayQueryProceedEXT() loops (prevents GPU hangs)
- RT beyond 20m: use SSR + baked maps, never honest RT

OFFLINE BAKING PRIORITY:
- Maximum offline baking, minimum real-time computation
- Heavy work in Cooker, runtime only assembles ready frames

---

## VULKAN 1.4 2026 OPTIMIZATIONS SUMMARY

New extensions and optimizations added for maximum performance in 2026:

### MAJOR 2026 BREAKING CHANGES:
- VK_EXT_descriptor_heap — COMPLETE descriptor system replacement, console-like memory access, supersedes VK_EXT_descriptor_buffer (DEPRECATED)
- VK_KHR_device_address_commands — GPU command generation via direct memory addresses, CPU-free command processing
- VK_AMDX_shader_enqueue — Hardware Work Graphs, GPU autonomous task scheduling, zero CPU involvement

### RENDERING & SHADERS:
- VK_KHR_ray_tracing_position_fetch — accurate ray hit position without any-hit shader
- VK_KHR_fragment_shader_interlock — atomic fragment operations, OIT without sorting
- VK_EXT_shader_atomic_float / VK_EXT_shader_atomic_float2 — atomic operations on float16/float32/float64
- VK_EXT_shader_image_atomic_int64 — 64-bit atomic operations on images
- VK_KHR_shader_expect_assume — branch prediction hints for GPU
- VK_KHR_cooperative_matrix 2.0 — improved tensor cores with FP8/INT4 support
- VK_EXT_mesh_shader extensions — multi-draw meshlet amplification
- VK_KHR_dynamic_rendering_local_read — read current render target in same pass
- VK_EXT_graphics_pipeline_library — pipeline caching for faster compilation
- VK_KHR_shader_maximal_reconvergence — improved convergence after divergence
- VK_KHR_shader_subgroup_extended_types — extended types for subgroup operations
- VK_KHR_shader_subgroup_uniform_control_flow — reduced divergence penalty
- VK_EXT_shader_tile_image — direct framebuffer access in fragment shader
- VK_EXT_sample_locations — programmable MSAA sample positions
- VK_KHR_load_store_op_none — skip load/store for unused attachments
- VK_KHR_shader_float16_int8 — native FP16/INT8 arithmetic (no emulation)
- VK_KHR_zero_initialize_workgroup_memory — automatic LDS zero-init
- VK_KHR_workgroup_memory_explicit_layout — explicit LDS layout for bank conflict avoidance
- VK_EXT_early_fragment_tests — depth test before fragment shader execution
- VK_NV_shader_subgroup_partitioned — dynamic wave partitioning for material binning (zero divergence)
- VK_NV_compute_shader_derivatives — dFdx/dFdy in compute shaders for visibility buffer texturing
- VK_KHR_shader_float_controls2 — absolute float behavior control, fast rounding for PBR
- VK_KHR_shader_subgroup_rotate — subgroup data rotation, eliminates LDS for blur/neighbor ops
- VK_KHR_shader_relaxed_extended_instruction — relaxed math variants, hardware-accelerated
- VK_EXT_shader_replicated_composites — hardware structure replication, single-tact construction
- VK_EXT_shader_long_vector — long math vectors in registers, physics simulation optimization
- VK_NV_compute_occupancy_priority — compute occupancy priority, critical shaders get maximum resources
- VK_KHR_pipeline_binary — native shader binary caching, eliminates compilation stutter
- VK_KHR_shader_abort — soft shader abort on error, prevents GPU hangs

### SYNCHRONIZATION & MEMORY:
- VK_EXT_pageable_device_local_memory — GPU-managed paging for oversubscription
- VK_KHR_dedicated_allocation — optimal allocation for large textures/buffers
- VK_EXT_memory_priority — priority marking for streaming textures
- VK_EXT_device_memory_report — memory leak/fragmentation tracking
- VK_KHR_maintenance5 — descriptor indexing improvements, robustness
- VK_KHR_external_fence_win32 — cross-process GPU sync
- VK_EXT_nested_command_buffers — command buffer reusability
- VK_KHR_internally_synchronized_queues — parallel queue submission from multiple threads
- VK_NV_push_constant_bank / VK_KHR_shader_constant_data — separate constant banks, zero contention
- VK_KHR_calibrated_timestamps — microsecond-accurate CPU/GPU time correlation for Tracy profiler
- VK_EXT_image_compression_control — hardware compression control for HDR/shadow buffers

### DISPLAY & OUTPUT:
- VK_EXT_hdr_metadata — HDR10+ metadata for displays
- VK_KHR_swapchain_mutable_format — dynamic swapchain format change without recreation
- VK_EXT_color_write_enable — per-color-channel write masks
- VK_EXT_depth_bias_control — programmable depth bias granularity
- VK_EXT_line_rasterization — accurate line rendering (Bresenham)
- VK_EXT_provoking_vertex — provoking vertex control for flat shading
- VK_NV_low_latency2 — NVIDIA Reflex integration, reduced input latency for 8000Hz mice

### TEXTURES & IMAGES:
- VK_EXT_image_2d_view_of_3d — 2D view of 3D texture slice without copy
- VK_EXT_image_view_min_lod — min LOD clamp for texture streaming
- VK_EXT_attachment_feedback_loop_layout — feedback loops without separate passes
- VK_EXT_attachment_feedback_loop_dynamic_state — dynamic feedback loops for post-processing
- VK_NV_memory_decompression — hardware texture decompression, UASTC support, zero CPU overhead

### PERFORMANCE & LATENCY:
- VK_EXT_pipeline_properties — pipeline compilation feedback for optimization
- VK_KHR_pipeline_library — pipeline binary caching across applications
- VK_NV_raw_access_chains — raw memory access, bypass complex addressing for SSBO

### NEW 2026 RENDERING TECHNIQUES:
- Two-Phase Occlusion Culling — prev frame HZB → render → new HZB for hidden object check
- Displacement Micromaps (DMM) — hardware micro-geometry for infinite detail (Nanite-like)
- ReSTIR PT (Path Tracing) — multi-bounce light transport with temporal/spatial reuse
- SMRT (Shadow Map Ray Tracing) — contact-hardening soft shadows on VSM
- Micro-Shadows — DMM micro-shadows for displacement micromaps (cracks, brick details)
- Multiple Scattering — VK_KHR_cooperative_matrix for dense fog (milk-like) simulation
- Wetness & Fluorescence — dynamic roughness/IOR changes, UV fluorescence
- Hair/Fur Scatter — Marshner model for directional light scattering (halo effect)
- Hybrid SSR + RT Fallback — primary SSR → RT fallback for off-screen reflections
- SDF Fallback — Global SDF step for off-screen reflections (no RT cores)
- Adaptive Ray Tracing — Checkerboarding (0.5 rpp) + VRS-guided RT (2x2 in shadows/motion blur)
- Light Leak Prevention — VK_KHR_ray_query validation before cache read (block light through thin walls)
- VSM Integration — volumetric light reads VSM for god rays

### COOPERATIVE MATRIX APPLICATIONS:
- Skinning: matrix palette skinning on tensor cores
- Cloth: constraint solving
- Soft Bodies: tetrahedral volume constraints
- Fluid: SPH neighbor lookups
- Clouds: lighting calculations
- Physics: XPBD/PBD constraint solving
- Audio: convolution reverb

### OFFLINE BAKING TECHNIQUES:
- VAT (Vertex Animation Textures) — pre-baked physics/destruction, zero runtime cost
- PVS (Potentially Visible Sets) — pre-baked visibility zones, single bitmask check
- Baked Acoustics — pre-baked impulse responses, zero runtime geometry calculation
- PRT (Precomputed Radiance Transfer) — pre-baked SH for dynamic objects in static worlds
- DDGI + SG Lightmaps — static geometry: SG Lightmaps, dynamic: DDGI Probes

### ULTIMATE OPTIMIZED RENDER PIPELINE:
1. Two-Phase Occlusion Culling (HZB pyramid)
2. Stochastic ReSTIR + VRS (0.25 rpp, checkerboarding, VRS-guided)
3. SG Lightmaps + DDGI Probes (max offline baking, min RT)
4. Hybrid SSR + RT Fallback + SDF Fallback
5. Froxel Volumetrics with temporal reprojection

### MAIN FORBIDDEN RULES FOR STABLE FPS:
- Ray Tracing for reflections/GI beyond 20m (use SSR + baked maps)
- Cloth/SoftBody simulation for HZB-culled objects (LOD tickrate or freeze)
- Honest shadows from small dynamic debris (use screen-space contact shadows only)
- VK_EXT_descriptor_buffer (DEPRECATED 2026 — use VK_EXT_descriptor_heap)

---

## НОВЫЕ КОНЦЕПТЫ ОПТИМИЗАЦИИ 2026 (NEW 2026 OPTIMIZATION CONCEPTS)

### ТЕКСТУРЫ (BHTEX) — Максимальная свобода и скорость
Аппаратный Sampler Feedback: Видеокарта сама запрашивает нужные кусочки (страницы) текстуры через аппаратную обратную связь сэмплера. Экономит такты процессора и делает подгрузку бесшовной.

Поддержка UDIM прямо в SVT: Одна логическая развертка, разбитая на сетку квадратов для гигантского разрешения без переключения контекста материалов.

Процедурная генерация страниц "на лету": Видеокарта рисует детали прямо в пустые страницы виртуальной текстуры с помощью вычислительных шейдеров. Уменьшает размер файлов игры.

Оптимизированное цветовое пространство (YCoCg) для масок: Использование цветового пространства YCoCg перед сжатием для технических текстур (roughness, metalness, occlusion). Сохраняет резкие детали без "квадратиков" от сжатия.

### МАТЕРИАЛЫ (BHMAT) — Гибкость без потери производительности
Послойные материалы через Work Graphs (VK_AMDX_shader_enqueue): Видеокарта с помощью аппаратных графов сама разобьет экран на микро-задачии и мгновенно посчитает каждый слой только там, где он реально виден.

Глобальный массив профилей подповерхностного рассеивания (SSS): Единственная крошечная библиотека профилей (LUT-текстура) для кожи, листьев или воска. Шейдер берет готовый ответ, освобождая регистры.

Bindless-декали в один проход: Единый глобальный массив декалей. В вычислительном шейдере видеокарта просто спрашивает: "Есть ли тут наклейка?", и если да — мгновенно подмешивает ее.

### ЗАПЕКАНИЕ ДЛЯ МОДЕЛЕЙ И ГЕОМЕТРИИ
Запеченная анимация для массовки (Vertex Animation Textures - VAT): На этапе подготовки движения запекаются в обычную текстуру, где цвет пикселя — это координаты смещения. Видеокарта анимирует толпы практически бесплатно.

Октаэдрические импосторы для дальних планов: Запекание со всех ракурсов в октаэдрические текстуры. Сложная модель превращается в два треугольника, которые всегда поворачиваются нужной стороной к камере.

Сверхплотная упаковка вершин (10-10-10-2): Упаковка нормалей и касательных в формат, где на оси X, Y и Z выделяется ровно по 10 бит, и 2 бита на знак.

Предвычисленная видимость помещений (PVS - Potentially Visible Sets): Движок заранее просчитывает пространственную сетку: из какой зоны какие объекты физически видны. Алгоритмы отсечения становятся мгновенными.

Запеченные дистанционные поля геометрии (Mesh SDF): Детализированный SDF запекается в низком разрешении прямо внутрь статических .BHMESH. Для мягких теней или физики частиц видеокарте не нужно проверять треугольники.

### РЕНДЕР БЕЗ RT (Классический путь)
Запекание кластеров света (Static Froxel Grid): Для статических ламп движок заранее прописывает индексы в пространственную сетку. В игре видеокарта просто берет готовый список света.

Гибридные порталы и PVS: Для закрытых пространств заранее просчитывается, что откуда видно. Если дверь закрыта, видеокарта даже не получит команду рисовать то, что скрыто.

Глобальные маски теней (Shadow Masks): Свет и тень от статичных объектов запекаются в специальные легкие текстуры-маски. Динамические тени рисуются только для того, что реально движется.

### RAY TRACING (Умный и бережный подход)
Ограничение длины луча (Ray Length Capping): Полноценные лучи только на дистанции 10–15 метров вокруг камеры. Если луч улетает дальше, он берет цвет из запеченных зондов (DDGI Probes) или SG Lightmaps.

RT только для динамики (Hybrid RT Shadowing): Тензорные ядра считаются только для лучей от фонарика или вспышек. Статика не отнимает время у RT-ядер.

Валидация лучей через толщину стен (Light Leak Prevention): Луч сверяется с запеченным SDF: "я внутри стены?" — если да, он мгновенно затухает.

### ТЕНИ И ВИЗУАЛ (Иллюзия сложности)
VSM (Виртуальные карты теней) — Заморозка страниц: Если в секторе нет движущихся объектов, страница тени "замораживается" навсегда.

Аналитические капсульные тени (Capsule Shadows): Тень от толпы существ строится по простым математическим капсулам, привязанным к костям. Видеокарта считает это за микросекунды.

Запекание микро-теней (Bent Normals): Изогнутые нормали в .bhmesh заменяют тяжелый SSAO для статичных объектов.

### РЕНДЕР ПРОЗРАЧНОСТЕЙ И VFX
OFF-SCREEN TRANSPARENCY (Раздельный рендер частиц): Густой дым, огонь, вода с сильным overdraw рендерятся в отдельный буфер в уменьшенном разрешении (1/2 или 1/4). Затем применяется Depth-Aware Bilateral Upsampling.

VFX TICKRATE DECOUPLING (Асинхронное обновление частиц): Логика и физика систем частиц, которые находятся далеко или за спиной камеры, обновляются не каждый кадр (15-30 Гц).

STOCHASTIC ALPHA-TESTING (Стохастическая прозрачность для листвы): Использование паттерна Байера (Bayer Matrix) или Blue Noise. Пиксели листвы отбрасываются по вероятностному шуму, а TAA собирает из этого плотную картинку.

### ОПТИМИЗАЦИЯ ГЕОМЕТРИИ НА ЭТАПЕ COOKER'А
STATIC CLUSTER MERGING (Физическое слияние геометрии): Cooker находит неподвижные объекты с одинаковым материалом и намертво "сшивает" их мешлеты в единый супер-кластер.

SHADOW PROXY MESHES (Прокси-геометрия для теней): Cooker генерирует отдельный, экстремально упрощенный невидимый меш для отбрасывания теней.

### ПРЕДИКТИВНЫЙ СТРИМИНГ
VELOCITY-BASED PAGE FETCH (Предиктивная загрузка текстур): Движок математически предсказывает, куда посмотрит или побежит игрок через 1-2 секунды, и заранее отправляет асинхронные запросы на загрузку страниц SVT.

### ШУМ И ТЕМПОРАЛЬНОЕ НАКОПЛЕНИЕ
INTERLEAVED GRADIENT NOISE (IGN для всех ресурсоемких эффектов): Единый, глобальный паттерн шума, который сдвигается по специальной сетке каждый кадр. Применяется ко всем тяжелым расчетам с 1-2 сэмплами на пиксель.

### PURE COMPUTE SHADING (Полный отказ от Fragment Shader)
Compute-Only Visibility Resolution: Этап расшифровки Visibility Buffer полностью перенесен в Compute Shader с использованием VK_NV_compute_shader_derivatives для dFdx/dFdy.

### DECOUPLED LIGHTING FREQUENCIES (Разделение частоты освещения)
Split Diffuse & Specular Passes: Diffuse-свет считается в половинном разрешении экрана (или даже 1/4), Specular — в полном нативном разрешении. Экономия ALU-инструкций достигает 60-70%.

### MESH SHADER MICRO-CULLING (Субпиксельное отсечение в регистрах)
Sample-Point Rejection: Если треугольник в экранном пространстве проецируется так, что он промахивается мимо центра пикселя, он не будет нарисован аппаратно.

### ASYNCHRONOUS ROTATIONAL REPROJECTION (Репроекция при вращении камеры)
Decoupling Camera from Render: Если игрок двигает только мышью (вращение камеры), движок пропускает этап генерации G-Buffer/Visibility для всей статической геометрии. Вычислительный шейдер берет глубину и цвет прошлого кадра, сдвигает пиксели на основе новой матрицы камеры.

### DATA-DRIVEN MATERIAL ELIMINATION (Агрессивное схлопывание материалов)
Distance-Based Material Downgrade: На дистанции больше 15-20 метров сложный PBR заменяется на базовый Lambert+Phong через подмену BDA-указателей.

### ИНТЕЛЛЕКТУАЛЬНЫЙ РАСТЕРИЗАТОР
TASK SHADER DYNAMIC ROUTING (Динамическая маршрутизация геометрии): Если мешлет состоит из микро-треугольников, Task Shader не вызывает emitMeshTasksEXT, а напрямую пишет данные в SSBO для SOFTWARE_RASTER.

SUB-PIXEL JITTER REJECTION (Отбраковка на этапе субпиксельного сдвига): Compute-растеризатор учитывает сдвиг камеры (Halton sequence) до вычисления барицентрических координат.

### АДАПТИВНЫЙ ШЕЙДИНГ (CONTENT-ADAPTIVE SHADING)
COMPUTE-BASED VRS ANALYSIS (Анализ контента перед шейдингом): Микро-Compute Shader пробегает по Visibility Buffer и вычисляет производную нормалей и глубины. Если участок плоский, шейдер переключает тайл 16x16 в режим VRS 4x4.

WAVE-MATCHED DEFERRED TEXTURING (Коалесцирование памяти при чтении текстур): Использование subgroupPartitionNV для перегруппировки потоков так, чтобы все 32 потока одновременно запросили текстуры только для одного MaterialID.

### УПРАВЛЕНИЕ ВРЕМЕНЕМ КАДРА (FRAME TIMING & ASYNC)
ASYNC PREDICTIVE CULLING (Culling на шаг впереди): Фаза отсечения полностью переносится в Async Compute и выполняется параллельно с пост-процессингом и UI текущего кадра.

STOCHASTIC CONTACT SHADOWS (Микро-тени через стохастику): Вместо 8-12 тяжелых шагов Ray Marching делается всего 1-2 шага со случайным оффсетом. Результат сглаживается темпоральным фильтром.

### СВЕРХБЫСТРОЕ ОСВЕЩЕНИЕ
LIGHT GRID DEAD-ZONE CULLING (Отсечение мертвых зон в Froxel): Compute Shader проверяет глубину HZB. Если весь кластер находится перед геометрией или глубоко за ней, кластер помечается как DeadZone.

ALBEDO-ONLY PASS (Схлопывание освещения для тьмы): Если кластер Froxel не содержит активных источников света, шейдинг-пасс переключается на микро-шейдер, который читает только Albedo и Ambient Occlusion.

### СВЕРХБЫСТРАЯ ПАМЯТЬ И ПРОПУСКНАЯ СПОСОБНОСТЬ
SINGLE-PASS DOWNSAMPLE (SPD) ДЛЯ HZB: Алгоритм SPD в одном Compute Shader генерирует все 12 мип-уровней за один вызов благодаря операциям внутри субгрупп.

ZERO-CPU ASSET STREAMING (Прямой канал в память): Использование DirectStorage (NVMe → PCIe) и VK_NV_memory_decompression. Сжатые чанки копируются прямо в VRAM, видеокарта сама их распаковывает.

### КЭШИРОВАНИЕ И ЛОКАЛЬНЫЕ ОБНОВЛЕНИЯ
DIRTY-RECTANGLE SHADOW UPDATES (Микро-обновления теней): Compute Shader вычисляет точный Bounding Box движущегося объекта внутри страницы VSM. При отрисовке через VkCmdSetScissor рамка обрезается ровно по этому объекту.

COMPUTE DECAL INJECTION (Безгеометрические декали): Декали пакуются в пространственную кластерную сетку. Шейдер просто проверяет, есть ли в этой точке декаль, и математически смешивает ее текстуру с базовым материалом.

### ЭКСТРЕМАЛЬНАЯ ПОСТОБРАБОТКА
TILE-MEMORY LOCAL READ (Пост-процессинг без VRAM): Использование VK_KHR_dynamic_rendering_local_read для объединения Tonemapping, Color Grading, Film Grain и Vignette в один массивный проход. Шейдер читает сырой HDR-цвет напрямую из внутренней памяти блоков ROP.

ANALYTICAL MOTION BLUR (Аналитическое размытие без сэмплинга): Математическое растягивание Visibility-буфера вдоль вектора движения прямо в Compute Shader.

### ЭВРИСТИКА ГЕОМЕТРИИ
DEPTH-ONLY PREPASS ДЛЯ ДИНАМИКИ: Динамические объекты (персонажи, монстры, двери) пускаются в микро-проход Depth-Only перед основным рендером. Это гарантирует, что тяжелый пиксель динамического персонажа не будет рассчитан, если он стоит за статической стеной.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ФИЗИКИ
HYBRID DEBRIS OFF-LOADING (Односторонняя эскалация обломков на GPU): Jolt Physics обрабатывает только геймплейно-важные объекты. При разрушении мелкие осколки передаются в GPU Compute (PBD), где они продолжают сталкиваться с SDF сцены.

ZERO-COST STATE DOUBLE-BUFFERING (Бесплатная двойная буферизация состояний): Использование подмены указателей в заголовке ECS-архетипа. Кадр N пишет в буфер А, кадр N+1 пишет в буфер B.

ASYNC AI SPATIAL QUERIES (Асинхронные сенсоры ИИ без блокировок): Тысячи запросов видимости от ИИ-агентов собираются в SSBO и отправляются в фоновый Async Compute Queue на видеокарту. GPU проверяет их об запеченный Scene SDF.

### ИЗОЛИРОВАННЫЕ ПРОСТРАНСТВА И МИРЫ
RELATIVE ISLAND KINEMATICS (Относительная симуляция внутри транспорта): Интерьер корабля выделяется в отдельный, полностью изолированный экземпляр Jolt (Local Space), который всегда находится в координатах 0,0,0.

WAKE-UP PREDICTION (Предиктивное пробуждение спящих островов): Использование грубого математического Ray-March снаряда на шаг вперед в параллельном Fiber-воркере. Остров принудительно переводится в активное состояние за несколько миллисекунд до реального физического контакта.

### ОПТИМИЗАЦИЯ СЛОЖНЫХ СТРУКТУР
TENSOR-ACCELERATED JACOBIAN SOLVER (Тензорное решение ограничений): Матрицы Якобиана отправляются напрямую в тензорные ядра через VK_KHR_cooperative_matrix.

TWO-WAY KINEMATIC-DYNAMIC COUPLING (Двусторонняя GPU-CPU связь): Процессор (Jolt) рассчитывает только грубые "плавучие сферы". Точная деформация волн происходит на GPU. GPU через ReBAR записывает усредненные векторы выталкивающей силы в крошечный Host-Visible буфер.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА ФИЗИКИ ДЛЯ ИИ
FORBIDDEN В ФИЗИЧЕСКОЙ ПОДСИСТЕМЕ:
- Копирование массивов трансформаций (memcpy): Использовать только Swapping указателей.
- Вызовы Jolt API из Render-потока: Рендер должен иметь доступ только к Read-Only буферам.
- Синхронные лучевые проверки (Raycasts) для визуальных эффектов: Использовать GPU через Global SDF.
- Создание мелких RigidBody в реалтайме: Использовать предварительно аллоцированный пул.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Интерполяция: Всегда писать шейдеры с учетом интерполяции: Position = mix(StatePrevious, StateCurrent, alpha).
- Выравнивание данных Jolt: Любые структуры должны строго использовать alignas(16) или alignas(64).
- Отвязка логики от физического тика: Игровые скрипты должны работать на фиксированном шаге, полностью отделенном от частоты кадров рендера.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ VFX И ЧАСТИЦ
HARDWARE WORK GRAPHS ДЛЯ ЧАСТИЦ (VK_AMDX_shader_enqueue): Процессор только дает триггер ("взрыв бочки"). Дальше GPU сам запускает узел-эмиттер, который генерирует частицы, аппаратно распределяет их по узлам-симуляторам.

MESH-SHADER PARTICLE CULLING (Кластерное отсечение частиц): Task Shader группирует частицы по 64 штуки. Если весь кластер дыма оказался за стеной (проверка по HZB), Task Shader просто не вызывает Mesh Shader.

COMPUTE-BASED FRUSTUM BINNING (Пространственная корзина частиц): Compute Shader заранее разбивает экран на сетку 16×16 тайлов. Каждая частица записывает свой индекс в тот тайл, куда она проецируется.

### ОПТИМИЗАЦИЯ СМЕШИВАНИЯ И ПРОЗРАЧНОСТИ
SUBGROUP BITONIC SORT (Аппаратная сортировка прозрачности): Использование VK_KHR_shader_subgroup_rotate. Внутри вычислительного шейдера потоки аппаратно перебрасывают данные друг другу, выполняя битонную сортировку.

VRS-DRIVEN OVERDRAW MITIGATION (Снижение разрешения в густом дыму): Движок отслеживает плотность частиц на экране. Если в одной точке скапливается слишком много полупрозрачных слоев, видеокарта автоматически включает Variable Rate Shading 2×2 или 4×4.

### ПРАВИЛА ГЕНЕРАЦИИ VFX ДЛЯ ИИ
FORBIDDEN В СИСТЕМЕ ЧАСТИЦ:
- Динамическая аллокация частиц на CPU: Использовать только гигантские заранее выделенные кольцевые буферы.
- Сортировка полупрозрачности на процессоре: Сортировка должна быть исключительно локальной на GPU.
- Чтение количества частиц обратно на CPU: Использовать только vkCmdDrawIndirectCount или Work Graphs.
- Биллбординг на CPU: Поворот к камере вычисляется строго в Mesh/Vertex шейдере.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Упаковка данных: Состояние частицы должно занимать строго 16 или 32 байта (alignas(16)).
- Локальные оси времени: Время жизни хранить в нормализованном виде от 0.0 до 1.0.
- Коллизии: Использовать исключительно чтение из запеченной 3D текстуры Scene SDF.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ЗВУКА И АКУСТИКИ
PRE-BAKED ACOUSTIC PORTALS (Акустические порталы без лучей): Cooker заранее разбивает геометрию уровня на «Акустические Зоны» и «Порталы». В игре процессор просто смотрит в этот граф.

GPU-ACCELERATED CONVOLUTION REVERB (Сверточная реверберация на видеокарте): Звуковой поток отправляется в VRAM, GPU накладывает реалистичное эхо за микросекунду и возвращает готовый буфер обратно в RAM.

PREDICTIVE AMPLITUDE CULLING (Отсечение неслышимых звуков): До стадии декомпрессии легковесный Fiber-воркер просчитывает грубую громкость. Если итоговая амплитуда падает ниже порога слышимости, звук аппаратно помечается как Virtual.

PROCEDURAL IMPACT SYNTHESIS (Процедурные вариации без затрат памяти): В памяти лежит всего 1 базовый сэмпл. При событии столкновения система через SIMD-инструкции "на лету" меняет Pitch, Attack и Envelope этого сэмплла.

### ПРАВИЛА ГЕНЕРАЦИИ АУДИО-КОДА ДЛЯ ИИ
FORBIDDEN В АУДИО-ПОДСИСТЕМЕ:
- Аллокации и блокировки в аудио-потоке: Абсолютно запрещены new/malloc, std::mutex, std::lock_guard.
- Объектно-Ориентированный DSP (OOP DSP): Запрещены виртуальные функции для аудио-фильтров.
- Поточечная обработка (Sample-by-sample processing): Запрещено обрабатывать звук по одному сэмплу.
- Синхронное чтение с диска: Запрещено использовать fread или std::ifstream.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Блочная SIMD-обработка (Block Processing): Вся обработка звука должна выполняться блоками (например, по 256 или 512 сэмплов) в формате SoA.
- Связь через MPMC-очереди: Любое взаимодействие игрового кода со звуком должно происходить исключительно через Lock-Free MPMC очередь.
- Предварительная аллокация голосов (Voice Pooling): При старте движка выделяется фиксированный пул "голосов".

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ASSET COOKER
SEMANTIC MESHLET CLUSTERING (Семантическая нарезка мешлетов): Cooker группирует треугольники так, чтобы внутри одного мешлета был только один материал и не было резких разрывов UV-координат.

PRE-CALCULATED GPU COMMANDS (Предвычисление команд загрузки): В готовом .BHMESH или .BHTEX уже лежит готовый бинарный блок команд для DMA-очереди.

SYMBOLIC SANITY VALIDATION (Символическая валидация от сбоев): Cooker прогоняет каждый ассет через жесткую символическую валидацию: удаляет вырожденные треугольники, исправляет нормали.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА COOKER ДЛЯ ИИ
FORBIDDEN В ASSET COOKER:
- Текстовые форматы в рантайме: Если исходник — это JSON, Cooker ОБЯЗАН скомпилировать его в плоский бинарный массив.
- Неявное выравнивание памяти (Implicit Padding): Структуры должны использовать явное выравнивание.
- Синхронный парсинг исходников: Cooker должен быть построен на Task-графах.
- Утечка строковых имен: Все строки должны быть захэшированы (MurmurHash3).

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Абсолютное совпадение структур (Zero-Copy Ready): Структура C++ в Cooker должна побайтово совпадать со структурой, которую читает игровой движок.
- Разделение Hot и Cold данных: Данные, которые нужны на каждом кадре, складываются в начало файла (Header).
- Детерминизм сборки: Одинаковый исходный файл должен всегда собираться в одинаковый бинарник.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ИИ И НАВИГАЦИИ
TENSOR INFLUENCE MAPS (Тензорные карты тактического влияния): Тактические карты оцениваются как матричные свёртки (Convolution) на тензорных ядрах видеокарты (VK_KHR_cooperative_matrix).

RENDER-AWARE AGGRESSION (Поведение на основе данных рендера): NPC читают пирамиду глубины (HZB) или Visibility Buffer текущего кадра. Если ИИ аппаратно понимает, что игрок его не видит, он меняет паттерн поведения.

MACRO-MICRO ROUTING (Двухуровневая гибридная навигация): Процессор считает только "Макро-граф" — абстрактные связи между городами или биомами. Видеокарта строит высокодетализированное Векторное Поле только в радиусе 50 метров вокруг игрока.

TRAJECTORY-DRIVEN ANIMATION COUPLING (Прямая интеграция AI в Motion Matching): Мозг NPC генерирует параметрическую кривую будущей траектории на 2 секунды вперед, которая напрямую "скармливается" в систему Motion Matching на GPU.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА ИИ ДЛЯ НЕЙРОСЕТИ
FORBIDDEN В СИСТЕМЕ ИИ И НАВИГАЦИИ:
- Объектно-ориентированные деревья поведения (OOP Behavior Trees): Категорически запрещено создавать классы узлов с виртуальными методами.
- Индивидуальный перерасчет пути (Per-Agent A): Запрещено вызывать алгоритмы поиска пути для каждого агента отдельно.
- Прямое чтение координат игрока (Hive-Mind Behavior): Запрещено ИИ напрямую читать точные мировые координаты игрока в каждом кадре.
- Динамическое выделение памяти для путей: Запрещены std::vector<Vector3> внутри структур NPC.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Скомпилированные деревья поведения (Data-Driven BT): Деревья логики должны компилироваться в непрерывный байт-код.
- Blackboard как разделяемая память: Все переменные состояния агентов лежат в плоских массивах (Blackboards).
- Асинхронные сенсорные очереди: Любые запросы ИИ к физике должны оформляться как POD-структуры и отправляться в lock-free очередь.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ UI И ТИПОГРАФИКИ
PRE-BAKED STATIC HUD (Тотальное запекание статического интерфейса): Cooker анализирует макет интерфейса еще на этапе сборки. Все неподвижные элементы намертво запекаются в единый статический .BHMESH.

ZERO-CPU ASSET UPLOAD ДЛЯ ИНТЕРФЕЙСА: Текстуры интерфейса уходят сразу в видеопамять (VRAM), полностью минуя процессор, с использованием аппаратной декомпрессии.

ANALYTICAL MESH SHADER PRIMITIVES (Аналитические примитивы без вершин): Процессор отправляет в SSBO только координаты центра, ширину, высоту и радиус скругления. Mesh Shader аппаратно разворачивает это в идеальную геометрию.

PRE-SHAPED TEXT BATCHING (Предварительная формовка текста): Для всего статического текста Cooker выполняет формовку (Text Shaping) оффлайн и запекает готовые координаты глифов в бинарный блок.

UI VISIBILITY CULLING (Отсечение 3D-мира под интерфейсом): Если игрок открывает полноэкранный инвентарь или карту, интерфейсная система устанавливает глобальный битмаск окклюзии. Графический конвейер мгновенно отсекает рендер всего 3D-мира.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА UI ДЛЯ ИИ
FORBIDDEN В СИСТЕМЕ UI И ТИПОГРАФИКИ:
- Immediate Mode (ImGui) для релизного HUD: Категорически запрещено использовать парадигму Immediate Mode для финального игрового интерфейса.
- Форматирование строк в цикле рендера: Абсолютно запрещено использовать std::sprintf, std::to_string или std::format в горячем цикле.
- Загрузка шрифтов в рантайме: Запрещен парсинг файлов .ttf или .otf во время игры.
- Альфа-блендинг перекрытых окон: Запрещено рисовать слои интерфейса, которые на 100% перекрыты другими непрозрачными слоями.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Упаковка цвета и координат: Все координаты вершин UI должны быть упакованы в 16-битные half или int16_t. Цвета UI-элементов строго упаковываются в 32-битный целочисленный формат.
- Единый Bindless Атлас: Все иконки, текстуры меню и глифы шрифтов должны лежать в одном глобальном Bindless-массиве.
- Асинхронный векторный рендер (Rive): Любые сложные векторные анимации интерфейса должны вычисляться параллельно в фоне через Job System.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ FLECS ECS
REBAR-NATIVE COMPONENTS (Прямое размещение колонок ECS в VRAM): Для компонентов, которые ежекадров читает GPU (Transforms, Velocities, Colors), аллокатор колонок переопределяется так, чтобы они физически размещались в ReBAR-памяти.

CUSTOM FIBER OS-API (Полный перехват многопоточности Flecs): Движок переопределяет ecs_os_api, подменяя функции создания потоков и мьютексов на твой Fiber Job System.

COMPILE-TIME ARCHETYPE SEALING (Блокировка графа архетипов): Cooker анализирует все префабы на этапе сборки. При старте игры движок инициализирует и "запечатывает" граф архетипов.

DECOUPLED SYSTEMS (Отказ от встроенных flecs::system в пользу flecs::query): Используются только закэшированные flecs::query. Твой Frame Pipeline сам решает, в какой момент дернуть query.iter().

DIRTY CHUNK BITMASKS (Сверхбыстрое отслеживание изменений): Использование встроенного механизма flecs::query с фильтром ecs_changed(). Flecs аппаратно помечает измененные таблицы на уровне целых блоков памяти.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА FLECS ДЛЯ ИИ
FORBIDDEN В СИСТЕМЕ ECS:
- Структурные изменения в горячем цикле: Категорически запрещено вызывать entity.add<T>(), entity.remove<T>() или entity.destruct() внутри систем во время итерации.
- Динамические контейнеры внутри компонентов: Компоненты ОБЯЗАНЫ быть тривиально копируемыми (POD). Абсолютно запрещено хранить std::vector, std::string или std::shared_ptr.
- Наследование компонентов (Component OOP): Запрещено использовать наследование C++ в структурах компонентов.
- Глубокая иерархия сущностей: Запрещено строить глубокие деревья entity.child_of(parent) для тысяч динамических объектов.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Отложенные команды (Deferred Operations): Все спавны сущностей, изменения архетипов или удаления должны происходить строго через flecs::defer.
- Векторизация (AVX/SIMD): Внутри цикла итерации код должен быть написан так, чтобы компилятор мог выполнить автовекторизацию.
- Разделение тегов и данных: Использовать пустые структуры-теги для фильтрации запросов.
- Прямые указатели на GPU (Buffer Device Address): В каждом архетипе, который рисуется, должен лежать специальный компонент struct GPUAddress { uint64_t bda; }.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ПАМЯТИ
NUMA-AWARE FIBER ALLOCATION (Топологически зависимая память): Пулы памяти (TLSF) жестко привязываются к воркерам (Taskflow). Поток, работающий на ядрах первого чиплета, выделяет память только из контроллера памяти этого же чиплета.

VIRTUAL MEMORY ALIASING (Магия виртуальной памяти для кольцевых буферов): Использование системных вызовов ОС (VirtualAlloc / mmap), чтобы отобразить одни и те же физические страницы памяти по двум адресам подряд.

ZERO-OVERHEAD TAGGED POINTERS (Аппаратная защита от Use-After-Free): В верхние 16 бит указателя записывается "Тэг Поколения" (Generation ID). При каждом обращении кастомный gsl::not_null сверяет тэг.

GPU UNIFIED HEAP DEFRAGMENTATION (Фоновая дефрагментация VRAM): Использование Async DMA Queue для прозрачного для рендера сдвига блоков данных в фоновом режиме.

### ГЛОБАЛЬНАЯ ОПТИМИЗАЦИЯ ПРОФИЛИРОВАНИЯ
FLIGHT DATA RECORDER / BLACKBOX (Черный ящик на случай краша): Движок выделяет 50 МБ в оперативной памяти под циклический "Черный ящик". Туда сбрасываются последние 10,000 событий Event Bus и треки Tracy.

HARDWARE PERFORMANCE COUNTERS (PMU TRACKING): Движок напрямую читает регистры процессора (Performance Monitoring Unit). Рядом с временем выполнения в профайлере появляются графики: Cache Misses и Branch Mispredictions.

SHADER OCCUPANCY TELEMETRY (Телеметрия здоровья видеокарты): Движок использует расширения Vulkan для чтения статистики пайплайна. Выводится процент Wave Occupancy и счетчики расхождения потоков.

### ПРАВИЛА ГЕНЕРАЦИИ КОДА ПАМЯТИ И ПРОФИЛИРОВАНИЯ ДЛЯ ИИ
FORBIDDEN В УПРАВЛЕНИИ ПАМЯТЬЮ И ПРОФИЛИРОВАНИИ:
- Глобальные аллокации в игровом цикле: Категорически запрещено использовать new, malloc или std::allocator внутри Update(), Tick() или любых систем ECS.
- Форматирование строк в профайлере: Абсолютно запрещено передавать динамические строки в зоны профилирования.
- Синхронный логгинг (File I/O Logging): Запрещено писать логи в файл через блокирующие вызовы std::ofstream.
- Аллокации внутри lock-free структур: Запрещено использовать аллокаторы внутри многопоточных очередей Event Bus.

ОБЯЗАТЕЛЬНЫЕ ПРАВИЛА:
- Передача контекста аллокатора (Allocator Injection): Любой класс или подсистема, требующая долгоживущей памяти, ОБЯЗАНА принимать указатель на Allocator* в конструкторе.
- Выравнивание кэш-линий (Cache-Line Alignment): Все массивы данных, обрабатываемые в параллельных воркерах, должны быть строго выровнены: alignas(64).
- Тотальное покрытие макросами (ZoneScoped): Каждая функция в ядре и подсистемах должна начинаться с макроса ZoneScoped (Tracy).
- Vulkan Timestamp Wrappers: Каждый проход Render Graph должен быть автоматически обернут в vkCmdWriteTimestamp2.

---

## ДОПОЛНИТЕЛЬНЫЕ АРХИТЕКТУРНЫЕ ТРЕБОВАНИЯ 2026

### 1. Чего не хватает (Infrastructure & Pipeline)

**Custom Shader Reflection Toolchain (Собственный рефлектор):**
Ты делаешь ставку на VK_EXT_descriptor_heap. Это отлично, но как движок узнает, куда записывать данные?
Решение: Тебе нужен кастомный препроцессор/рефлектор SPIR-V, который парсит шейдеры и генерирует C++ структуры (POD) для push_constants или descriptor_heap смещений. Без этого твои программисты будут тратить часы на отладку «почему данные в шейдере не те».

**Visual Regression Testing (Визуальные тесты):**
При таком уровне оптимизаций (Mesh shaders, DGC) регрессия неизбежна.
Решение: Автоматизированная система «Золотых кадров» (Gold Images). Билд-сервер прогоняет тесты на разных GPU, сравнивает SSIM (Structural Similarity Index) кадров. Если отклонение > 0.001% — билд крашится.

**Memory Compaction (Дефрагментация VRAM):**
VMA — это круто, но при длительной работе с виртуальной текстуризацией и SVT фрагментация VRAM неизбежна.
Решение: Система фонового «уплотнения» памяти (VRAM defrag). Ты должен иметь возможность перемещать ресурсы в VRAM без остановки рендера, используя асинхронные копии (DMA queue) и обновляя BDA (Buffer Device Address) в дескрипторной куче.

**Data-Oriented Debugging UI:**
Стандартные дебаггеры (Visual Studio/GDB) бесполезны для ECS с 100,000 сущностей.
Решение: Свой визуализатор памяти (Inspector), который умеет парсить archetypes во Flecs и показывать их в виде таблиц, графиков и гистограмм в реальном времени.

### 2. Критически важные уточнения (Hardware & API)

**Queue Family & Subgroup Strategy:**
Ты много пишешь про Async Compute. Но ты не учел Hardware Scheduling.
Риск: Если графическая очередь и Async Compute очередь работают на одном Compute Unit (CU), у тебя будут конфликты за L1/LDS.
Решение: Нужно реализовать «Smart Queue Budgeting». Движок должен динамически изменять priority очередей через VK_EXT_global_priority, если GPU начинает проседать по occupancy.

**Precision and Stability (FMA/IEEE):**
Ты запретил FMA в некоторых местах для детерминизма, но разрешил для скорости.
Риск: Несовместимость между NVIDIA/AMD/Intel из-за разной точности FMA.
Решение: Использование VK_KHR_shader_float_controls2 для принудительного округления до ftz (flush-to-zero) и daz (denormals-are-zero) на всех GPU. Без этого кросс-платформенная физика/освещение будет "плавать".

**C++26 Features:**
Раз уж ты целишся в C++26, используй std::mdspan для работы с многомерными массивами (текстуры, Froxels, Compute Grid). Это даст тебе безопасность массивов без потери производительности (bounds checking в дебаге, его отсутствие в релизе).

### 3. Где могут быть "дыры" в производительности

**Lock-Free Queue Bottleneck:**
Ты полагаешься на MPMC-очереди. Если все системы (ECS, Physics, Audio) начнут спамить в одну очередь, ты получишь contention (конкуренцию за кеш-линию).
Решение: Система "Thread-Local Queues". Каждое ядро пишет в свою локальную очередь, а "Event Bus" лишь периодически "собирает" (gather) события в пакеты.

**Async Compute Stalls:**
Если ты вызываешь vkQueueSubmit слишком часто, ты убьешь драйвер оверхедом.
Решение: Frame Pacing & Batching. Все запросы на Async Compute должны агрегироваться в один vkCmdExecuteGeneratedCommandsEXT (DGC). Не делай Submit для каждой задачи.

### 4. Золотой совет для архитектора

Ты пишешь движок с нуля. Самая большая проблема таких проектов — "The Complexity Death Spiral". Когда движок становится таким сложным, что добавление одной фичи (например, нового типа источника света) требует изменений в 10 местах.
Совет: Внедряй Data-Driven Pipelines.
Пусть шейдеры (написанные на Slang) при компиляции сами генерируют JSON-описание своих входов (индексы дескрипторов, push-constants).
Твой RenderGraph должен автоматически собирать конвейер на основе этого JSON, а не на основе "ручного" кода.

**Чего не хватает для завершенности:**
- System-Level Telemetry: Интеграция Tracy — это база, но тебе нужен свой "telemetry agent", который будет сбрасывать статистику GPU-времени в базу данных в реальном времени, чтобы видеть, как меняется производительность от билда к билду.
- Shader Hot-Reloading: Реальная горячая перезагрузка шейдеров с сохранением состояния (pipeline state preservation). Как ты будешь менять логику шейдера, если он использует descriptor_heap, который уже заполнен данными? (Спойлер: тебе нужна система "Versioning Descriptors").

### 5. Что я бы убра

**Neural Semantic Search для ассетов:**
Почему: Это задача уровня Google. На практике достаточно Tag-based search + Guid-based caching. Не трать силы на создание AI-поиска, пока у тебя нет 10,000+ ассетов.

**Собственный "Micro-MLP" для всего подряд (AI, GI):**
Почему: Слишком много абстракций. Если ты хочешь быть лучшим — делай один мощный вычислительный "движок" тензорных операций (cooperative_matrix), а не отдельные ML-решения для всего. Пусть будет один "матричный решатель", а не три разных ML-системы.

### 6. Где "лучшие решения" могут подвести (Критика архитектуры)

**Jolt Physics Double Precision:**
DP (Double Precision) в физике сильно замедляет AVX-инструкции.
Альтернатива: Большинство AAA-движков (Unreal, Unity) используют Floating Origin (смещение мира вокруг игрока). Это быстрее и проще, чем DP Jolt. Если ты не делаешь симулятор космических полетов от Земли до Луны в одном "мире", перейди на Single Precision, но с Floating Origin.

**Memory Allocators (rpmalloc + mimalloc + TLSF):**
В чем проблема: Ты используешь слишком много аллокаторов. Это усложняет отладку утечек.
Совет: Оставь mimalloc для всего (он лучший по производительности/безопасности) + Frame Arena для горячего цикла. Не плоди сущности.

**GPU Command Generation (DGC):**
В чем проблема: DGC невероятно мощный, но он "черный ящик". Если что-то не отрисовалось, ты не увидишь это в обычном RenderDoc.
Совет: Внедряй систему "Debug Bypass". У тебя должен быть ключ запуска --use-classic-indirect, который отключает DGC и переключает всё на обычный vkCmdDrawIndexedIndirect, чтобы ты мог дебажить сцену.

### 7. Что стоит добавить (Фундаментальные вещи)

**"Pipeline Binary Caching" (Native):**
Ты упомянул VK_KHR_pipeline_binary. Это критически важно. Сделай так, чтобы при первом запуске на новой видеокарте движок не "фризил", а сразу подгружал уже скомпилированные бинарники из кэша, который ты (теоретически) можешь поставлять вместе с игрой.

**Hot-Reloading (Deep Dive):**
Ты упомянул Clang/LLVM. Добавь сюда State Serialization. Если ты меняешь логику системы (ECS System), движок должен уметь сериализовать текущее состояние компонентов в JSON, перезагрузить DLL, и десериализовать обратно. Это "Святой Грааль" разработки.

**Cross-process Synchronization:**
Добавь поддержку VK_KHR_external_fence_win32. Если ты захочешь сделать отдельное окно для редактора или оверлей, без этого ты получишь мерцание и рассинхрон кадров.

### 8. Гибридный RT (Ray Tracing) вместо нейросетей

**ReSTIR DI (Direct Illumination):**
Позволяет обрабатывать миллионы источников света (включая мелкие эммитеры) без классического "форвардного" или "деферред" лимита ламп.

**ReSTIR GI (Global Illumination):**
Вместо нейронок используй Temporal & Spatial Resampling. Это дает результат уровня Path Tracing, используя всего 0.5–1 луч на пиксель, собирая освещение из "соседей" во времени и пространстве. Это математика, а не нейронка. Это стабильно, детерминировано и дает картинку «как в кино».

**Отражения: Гибрид (SSR + RT Fallback)**
Алгоритм: Сначала всегда идет SSR (Screen Space Reflections) с использованием HZB (Hierarchical Z-Buffer) для ускорения пересечений.
Fallback: Если луч SSR выходит за пределы экрана или попадает в Occlusion (промах), ты делаешь Inline Ray Query (через rayQuery в Compute Shader).
Важно: Не используй нейро-апскейлеры отражений. Используй Roughness-Aware Filtering (размытие отражений в зависимости от шероховатости) через фильтр Importance Sampling — это дает идеально чистые, не шумные отражения.

**Денойзинг (без нейросетей)**
Вместо AI-денойзера используй Spatiotemporal Variance Guided Filtering (SVGF) или его современные вариации.
Он анализирует "дисперсию" цвета: если пиксель сильно отличается от соседей — он "шумит". Денойзер плавно "размазывает" этот шум, опираясь на Motion Vectors и Depth-Weighting. Это стандарт, который выглядит почти так же чисто, как AI-денуизер, но работает в 10 раз быстрее и не требует обучения.

### 9. Как оптимизировать "Гибридный пайплайн" (Советы профи)

Чтобы картинка была "лучшей в мире", тебе нужно сосредоточиться не на ML, а на Data-Oriented Geometry & Light:

**Visibility Buffer (Визибилити-буфер):**
Это база для 2026 года. Ты не рисуешь G-Buffer. Ты рисуешь InstanceID, MeshletID и TriangleID в R32_UINT.
Это экономит тонну памяти (особенно на нормалях/roughness) и позволяет делать очень быстрый отложенный шейдинг.

**Cluster-Based Shading (Froxels):**
Для GI и теней не обязательно считать каждый пиксель. Дели экран на 3D-сетку (кластеры) и считай освещение только там, где есть свет. Это классика, которая при хорошей реализации работает на порядок быстрее любой "нейронки".

**Variable Rate Shading (VRS) для оптимизации:**
Используй VRS не по AI-предикции, а по анализу контента:
Если поверхность плоская (низкий градиент нормалей) — рисуй 4x4.
Если это мелкая деталь — 1x1.
Это дает до 30% прироста FPS без потери качества.

### 10. Global Signed Distance Fields (GSDF) — "Сердце" без RT-ядер

Если у тебя нет RT-ядер для ускорения BVH, тебе нужно SDF-покрытое поле всего мира.
Что это: Ты запекаешь геометрию всей сцены в 3D-текстуры (атлас SDF-блоков).
Зачем:
- GI: Ты выпускаешь лучи не в BVH, а в SDF-поле. Математически это быстрее в разы.
- Shadows: Вместо того чтобы тратить циклы на Shadow Map, ты делаешь "Ray Marching" до поверхности через SDF. Тени будут мягкими, корректными и почти бесплатными.
- AO: Мгновенное расчет AO любой сложности.
Внедрение: Добавь в пайплайн GenerateGlobalSDF.comp (на этапе Cooker) и SDFRaymarch.hlsl (в рантайме).

### 11. DDGI (Dynamic Diffuse Global Illumination) — Пробы вместо AI

Это технология, которую популяризировала NVIDIA, но она прекрасно работает на Compute-шейдерах без RT-ядер.
Суть: Ты расставляешь в мире сетку "зондов" (проб). Каждый зонд хранит освещение вокруг себя (в формате Spherical Harmonics).
Почему это лучше: Тебе не нужны нейросети. Зонды обновляются асинхронно, собирая свет из окружения.
Твой апгрейд: Вместо того чтобы пускать лучи из зондов в BVH (что требует RT-ядер), пускай их в GSDF (см. п. 10). Это будет работать даже на старых картах с невероятной скоростью.

### 12. Software Rasterization (Micro-triangle Culling)

Если у тебя нет Mesh Shader'ов (или они работают медленно на старых картах), ты должен уметь сам "рисовать" треугольники в Compute Shader.
Зачем: Для самых мелких деталей (трава, камни, мусор).
Технология: Используй "Tiled Rasterization". Разбей экран на плитки 8x8, внутри каждой плитки Compute Shader рисует треугольники в локальную память (LDS), а затем записывает в буфер. Это дает бесконечную детализацию без нагрузки на геометрический конвейер видеокарты.

### 13. SDF-based Reflections (Отражения без RT)

SSR (Screen Space Reflections) всегда промахиваются, когда объект за краем экрана.
Решение: Используй SDF Raymarching для отражений.
Алгоритм:
- Если объект виден на экране — SSR.
- Если объект за экраном — делаешь "Ray Marching" по Global SDF.
Результат получается идеально корректным, без артефактов "черных дыр" по краям экрана. Это стандарт для качественных движков без RT.

### 14. Infinite/Stochastic Probe Grids

Вместо того чтобы хранить GI в вокселях (VXGI), которые едят память, храни освещение в Spatial Hash Grid.
Это позволяет тебе иметь "GI" с разрешением почти как у настоящего Path Tracing, но при этом данные плотно упакованы.
Используй Temporal Accumulation: не считай GI за один кадр. Считай 1/16 часть зондов каждый кадр. В итоге через 16 кадров у тебя полная картинка GI. Человеческий глаз не заметит разницы, а GPU будет отдыхать.

### 15. Total Pipeline State Control (Zero-Recompile)

Ты используешь VK_EXT_graphics_pipeline_library и VK_EXT_shader_object, но чтобы вообще забыть о статтерах:
VK_EXT_extended_dynamic_state3: Внедри поддержку этого расширения по максимуму.
Суть: Ты выносишь в динамические состояния почти всё: AlphaToCoverageEnable, DepthClampEnable, LogicOp, PolygonMode, RasterizerDiscardEnable.
Профит: У тебя остается один (или два) конвейера на материал. Ты переключаешь свойства материала через vkCmdSet... (это пустая операция для GPU), вместо пересоздания Pipeline. Это убирает потребность в тысячах комбинаций VkPipeline.

### 16. Micro-Optimization: Управление регистрами (VGPR Pressure)

Многие движки "захлебываются", потому что компилятор использует слишком много регистров на один поток, из-за чего на CU/SM (Compute Unit) запускается меньше потоков (Wave Occupancy падает).
Регистровое бюджетирование: В параметрах компиляции шейдеров (glslangValidator или dxc) используй ключ -maxrregcount.
Твоя задача: Заставить компилятор уложиться в 64 или 32 регистра.
Почему: Если шейдер потребляет 128 регистров, ты запустишь в 2-4 раза меньше потоков параллельно. Лучше пусть шейдер чуть дольше работает (spilling в LDS), но работает массово, чем быстро, но в одиночку.
Локальные данные в const: Внутри шейдеров делай const переменные для всего, что не меняется. Драйверы AMD/NVIDIA лучше оптимизируют регистры, если они знают, что переменная константна.

### 17. Memory Layout: "Cache-Line Friendly" структуры

Ты используешь SoA, но давай доведем это до идеала:
128-битные загрузки: Современные GPU читают память кусками по 128 бит (4 флоата).
Оптимизация: Выравнивай структуры так, чтобы они занимали 16 байт. Если у тебя есть структура из 3-х флоатов, добавь 4-й (padding) как unused. Если структура меньше 16 байт, объединяй данные из двух разных массивов в одну структуру uvec4, чтобы при одной выборке из памяти прилетали данные сразу для двух операций.
Struct Packing: Используй packed структуры только для хранения (в буферах), но никогда не используй их внутри SSBO или UBO напрямую. Всегда делай Unpack в Local Memory (регистры) в начале шейдера. Это "бесплатная" операция, которая спасает от медленного random access чтения.

### 18. Branching & Flow Control (The "Fast Path")

Branch Prediction Hints: Ты упомянул VK_KHR_shader_expect_assume. Иди дальше:
Используй [branch] или [flatten] (в HLSL/Slang) явно, чтобы подсказать компилятору, когда нужно развернуть цикл, а когда — оставить прыжок.
Для PBR/Материалов: Вместо if (materialType == 0) используй switch с явным [[attribute(flatten)]]. Компилятор сгенерирует прыжковую таблицу, что быстрее, чем цепочка if.
Waterfall Loops: Если у тебя bindless доступ (массив текстур), оберни его в паттерн "Waterfall".

### 19. Geometry & Indirect Command Buffer (DGC)

Ты делаешь ставку на vkCmdExecuteGeneratedCommandsEXT. Чтобы выжать максимум:
Chained DGC: Сделай так, чтобы один вычислительный шейдер генерировал команды для нескольких последующих проходов (Depth, G-Buffer, Shadow). Не делай "Генерация → Сброс на CPU → Рисование". Генерация должна жить полностью на GPU.
Visibility Buffer (V-Buffer):
Не храни нормали/UV в G-Buffer. Храни только TriangleID + InstanceID.
Оптимизация: В проходе шейдинга (Compute) не читай текстуры сразу. Используй ddx/ddy (Compute Derivates), чтобы вычислить MIP-уровень текстуры заранее. Это уберет "шум" на текстурах и ускорит кэширование L1.

### 20. Culling (Early-Z и не только)

Conservative Depth: Если ты не используешь discard (или demote), шейдер всегда должен записывать gl_FragDepth. Если ты это делаешь, GPU может выполнить Early-Z Test (проверка глубины ДО запуска шейдера).
Правило: Никогда не делай discard (или clip) в сложных шейдерах. Если нужно скрыть пиксель, используй demote или alpha-to-coverage.
Stencil-based Early-Z: Для сложных объектов (сетчатые заборы, листва) используй Stencil Buffer, чтобы "вырезать" пустые зоны. Это дешевле, чем заставлять GPU проверять Depth у каждого пикселя.

### 21. Синхронизация (The Hidden Killer)

Split Barriers (vkCmdSetEvent2 + vkCmdWaitEvents2): Ты их используешь, но добавь правило: "Никогда не используй один большой барьер в конце кадра".
Дробление: Разбивай барьеры на мелкие (например, для одного текстурного юнита). GPU может начать выполнять следующий Compute-пасс, пока предыдущий еще дописывает данные в непересекающиеся области памяти. Это называется Async Compute Overlap.

### 22. CPU Topology & Hybrid Core Affinity (P/E Cores)

В современных CPU (Intel 12th+ / AMD 9000+) ядра физически разные.
Правило: Твой Job System должен быть "Aware of Topology".
P-cores (Performance): Сюда кидаешь только самые тяжелые задачи: Jolt Physics step, Audio DSP Graph, Animation blending.
E-cores (Efficiency): Сюда кидаешь фоновые задачи: I/O, декомпрессию, обновление статистики, телеметрию, логирование.
Привязка: Используй SetThreadAffinityMask (Windows) или pthread_setaffinity_np (Linux), чтобы закрепить потоки воркеров на конкретных P-ядрах. Не давай ОС перемещать их, иначе кэш L2/L3 будет постоянно "прогреваться" заново.

### 23. "Branchless" Coding (Искоренение предсказаний)

Процессор ненавидит if/else, если условие непредсказуемо. Ошибка предсказания переходов (branch misprediction) сбрасывает конвейер, что стоит 15–20 циклов CPU.
Правило: Замени ветвления на математические операции или cmov (conditional move).
Вместо: if (val > threshold) result = a; else result = b;
Используй: result = b + (val > threshold) * (a - b); (компилятор часто преобразует это в одну инструкцию cmov).
Используй std::bit_cast и std::countr_zero: C++20/23/26 функции для работы с битами. Они компилируются в одну инструкцию CPU (BSR, BSF, POPCNT), которая работает в разы быстрее, чем любые условия.

### 24. PGO + LTO (Profile-Guided Optimization)

Это самый мощный инструмент оптимизации, который многие игнорируют.
LTO (Link Time Optimization): Позволяет компилятору видеть код всего движка целиком, а не по отдельным .cpp файлам. Это дает инлайнинг через границы модулей.
PGO:
- Собираешь билд с флагом профилирования.
- Запускаешь игру, играешь 10 минут (собираются данные, какие пути в коде горячие).
- Пересобираешь движок с этими данными.
Результат: Компилятор переставит блоки кода в памяти так, чтобы "горячие" пути шли подряд (улучшает кэш инструкций) и лучше инлайнит функции, которые реально вызываются часто. Прирост производительности в Hot Path может достигать 10-15%.

### 25. Cache-Line Awareness & False Sharing

Даже если ты используешь alignas(64), это не всё.
Padding: Если у тебя есть структура с данными, к которым обращаются разные потоки, убедись, что переменные, которые пишутся разными потоками, не лежат на одной кэш-линии (64 байта). Иначе возникнет "False Sharing" — CPU будет гонять данные между кэшами ядер, даже если переменные разные.
SoA (Structure of Arrays): Ты уже это используешь, но добавь Prefetching. Используй _mm_prefetch (или std::prefetch в C++23) для данных, которые понадобятся через 100 циклов. Пока CPU делает математику, он уже подтянет данные из RAM в L1.

### 26. Data-Oriented Memory Management (Allocator Locality)

Не просто "используй пулы", а "используй пулы, близкие по адресу".
Правило: Компоненты ECS должны аллоцироваться строго последовательно. Если твой аллокатор разбросал сущности по разным адресам в RAM, ты получишь "Pointer Chasing" (промахи кэша), и AVX-512 будет ждать данных из памяти 90% времени.
Хитрость: Используй Huge Pages (2MB/1GB) для основных массивов ECS. Это уменьшает размер таблицы страниц (TLB), что критично при больших объемах памяти (когда у тебя 100к+ сущностей).

### 27. Minimizing Syscalls (System Call Overhead)

Каждый вызов malloc, free, printf, или работа с файлами — это прерывание, переключение в режим ядра, смена контекста.
Правило: "Zero-Syscall Game Loop". Все, что нужно игре, должно быть выделено при старте или при загрузке уровня. Внутри игрового цикла (Update/Render) не должно быть ни одного системного вызова, кроме Present.
Логирование: Не пиши логи в файлы синхронно. Пиши их в Lock-free кольцевой буфер в памяти, а фоновый поток (на E-core) будет сбрасывать их на диск.

### 28. Explicit Vectorization (AVX-512)

Ты упомянул AVX-512.
Совет: Если ты используешь AVX-512, снижай частоту CPU принудительно (throttling). Некоторые процессоры сбрасывают частоту, если AVX-512 загружен на 100% долгое время.
Решение: Если задача не критически важная, используй AVX2. Для самых тяжелых (математика частиц, скиннинг, физика) — AVX-512. Балансируй это, чтобы не "душить" остальные ядра.

### 29. std::atomic и memory_order

Большинство разработчиков по умолчанию используют std::memory_order_seq_cst (строгая синхронизация), которая добавляет "барьеры памяти", замедляющие CPU.
Правило: Используй std::memory_order_relaxed там, где тебе не важна строгая последовательность (например, счетчик кадров или простые статистики). Это позволяет CPU переупорядочивать инструкции как ему угодно, что дает колоссальный прирост.

### 30. Архитектурный уровень: Pointer-less Data (32-bit Handles)

Проблема: В 64-битных системах указатели (BDA) занимают 8 байт. В огромных массивах ECS это убивает кэш L1/L2.
Решение: Откажись от 64-битных указателей (BDA) везде, где это возможно, в пользу 32-битных хэндлов (смещений) относительно базы буфера.
Как: Твой аллокатор VMA/TLSF должен возвращать не uint64_t address, а uint32_t handle.
Результат: Ты экономишь 50% памяти на индексах, что делает данные "плотнее". При чтении из GPU ты делаешь base_address + (handle * sizeof(T)). Это позволяет упаковывать больше данных в одну кэш-линию.

### 31. GPU-side PGO (Profile Guided Optimization)

Обычные шейдеры компилируются "вслепую".
Решение: Внедряй систему Shader Feedback Loop.
Как: В рантайме собирай статистику с GPU (количество выполненных инструкций, количество обращений к памяти, Wavefront Occupancy). Если какой-то шейдер работает медленно, движок помечает его.
Внедрение: При следующем запуске игры движок пробует перекомпилировать этот шейдер с другими флагами (например, форсировать subgroupSize=32 вместо 64 или изменить maxrregcount), и если FPS вырос — сохраняет этот "оптимизированный" бинарник.
Результат: Движок «подстраивается» под конкретную архитектуру GPU (NVIDIA vs AMD vs Intel) пользователя.

### 32. Wavefront Occupancy Tuning (Контроль над потоками)

Проблема: Шейдеры часто "умирают" из-за того, что компилятор использует слишком много регистров (VGPRs), из-за чего на SM/CU запускается мало потоков одновременно.
Решение: Принудительный контроль размера Wavefront.
Используй VK_EXT_subgroup_size_control.
Правило: Для всех тяжелых Compute-шейдеров (GI, Shadows, Particles) форсируй VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT и фиксированный размер (например, 32).
Это позволяет тебе математически точно рассчитать, сколько регистров ты можешь использовать, чтобы получить 100% occupancy. Если ты выходишь за лимит — компилятор выдаст ошибку, и ты будешь вынужден оптимизировать код, пока он не станет идеальным.

### 33. Wait-Free Data Structures (Следующий шаг после Lock-Free)

Ты используешь Lock-Free MPMC очереди. Это хорошо, но они всё равно требуют atomic операций, которые "дергают" шину памяти.
Решение: Wait-Free алгоритмы.
Для систем, где данные обновляются постоянно (например, статистика фреймрейта или позиция камеры), используй структуры, где ни один поток не ждет другого.
Секрет: Используй std::atomic_ref для доступа к данным из разных потоков без создания объектов-оберток. Это дает прямое взаимодействие с кэшем без оверхеда на абстракции.

### 34. Cache Coloring (Цветовая маркировка кэша)

Это уже уровень «бога оптимизации».
Проблема: Если разные потоки постоянно пишут в адреса памяти, которые отображаются на один и тот же индекс в кэше (Cache Associativity), данные вытесняют друг друга (Cache Conflict).
Решение:
Разноси данные потоков в памяти так, чтобы они не попадали в одну "линию" кэша.
Это требует контроля над тем, как ты выделяешь память (Alignment). Используй std::hardware_destructive_interference_size (из C++17) для выравнивания объектов в памяти, чтобы они физически не могли находиться на одной кэш-линии.

### 35. Pipeline Binary Caching (Native Binary Injection)

Ты упомянул VK_KHR_pipeline_binary.
Решение: Сделай так, чтобы при обновлении игры бинарники шейдеров не перекомпилировались, а поставлялись предкомпилированными под основные архитектуры.
Для Windows: собирай шейдеры под DXIL/SPIR-V с флагами для NVIDIA/AMD/Intel отдельно.
При запуске движок проверяет VkPhysicalDeviceProperties::pipelineCacheUUID. Если совпадает с тем, что у тебя в пакете — ты грузишь бинарник напрямую в GPU.
Результат: "Stutter-free" геймплей. Никаких лагов при первой встрече с эффектом.

### 36. Virtual Geometry (Nanite-like approach, но проще)

Если ты не хочешь делать нейросети, сделай свою систему кластерного отсечения (Cluster Culling).
Суть: Любой меш разбивается на кластеры (по 64-128 треугольников).
Logic: Каждый кластер имеет свой Bounding Sphere и Normal Cone (конус нормалей).
GPU-Side: Compute шейдер проверяет:
- Виден ли кластер (Frustum)?
- Повернут ли он к камере (Backface Culling по конусу)?
- Перекрыт ли он (HZB)?
Result: Ты рисуешь только те треугольники, которые реально видны. Это работает быстрее, чем любые LOD'ы, потому что отсечение происходит на уровне "микро-треугольников".

### 37. Telemetry Feedback Loop (Self-Tuning)

Твой движок должен быть "умным".
Суть: Внедряй "Adaptive Quality". Если профайлер видит, что FrameTime начал расти (например, из-за сложных спецэффектов), движок автоматически:
- Урезает дистанцию прорисовки травы.
- Уменьшает количество сэмплов в GI (ReSTIR).
- Переключает VRS с 1x1 на 2x2.
Почему это круто: Игрок не замечает падения FPS, потому что движок "плавно" снижает качество там, где это наименее заметно, и возвращает его обратно, когда нагрузка падает.

### 38. Итоговый список "Железных правил" для path.md

- 32-bit Handles: Использовать хэндлы вместо указателей для всех данных в GPU-буферах.
- Explicit Occupancy Control: Фиксированный размер Wavefront для всех Compute-шейдеров.
- Wait-Free Structures: Минимум atomic операций, использование memory_order_relaxed.
- Hardware-Specific Binaries: Поставка предкомпилированных пайплайнов для каждой вендорской архитектуры.
- Cluster Culling: Процедурное отсечение геометрии по "нормальным конусам" (Normal Cones).
- Self-Tuning Engine: Автоматическая регулировка настроек (VRS, LOD, Sampling) на основе FrameTime в реальном времени.

### 39. Использование тензорных ядер (Cooperative Matrix) для физики

Тензорные ядра для физики и высокоуровневых симуляций — это высший пилотаж.
Суть: большинство физических ограничений (XPBD, PBD) — это решение систем уравнений вида Ax=b. Если у тебя много частиц (ткань, жидкость), это превращается в задачу разреженных матриц.
Как это работает:
- Ты группируешь ограничения (constraints) в блоки (например, по 16x16 или 32x32 частицы).
- Записываешь Якобианы (производные ограничений) в матрицы.
- Используешь VK_KHR_cooperative_matrix для выполнения GEMM (General Matrix Multiply).
Твой профит: Тензорные ядра выполняют тысячи умножений с накоплением за один такт. Это в десятки раз быстрее, чем считать те же ограничения через стандартный float32 SIMD (AVX/SSE).
Что добавить в пайплайн:
- Constraint Batching: Алгоритм, который на CPU (или Compute Shader) сортирует частицы ткани так, чтобы соседние ограничения попадали в один "тайл" матрицы.
- Sparsity: Используй sparse block matrix multiplication. Не считай нули.

### 40. NUMA-Aware Memory Allocation (Секрет серверов)

Многие топовые движки забывают, что современный процессор (особенно AMD Ryzen или Threadripper) — это не один монолитный кусок, а набор чиплетов.
Проблема: Если поток на "чиплете А" пытается прочитать память, выделенную на "чиплете Б", возникает задержка.
Решение:
Твой аллокатор памяти (TLSF/Arena) должен быть NUMA-aware.
При запуске движок определяет топологию процессора. Воркеры (Fibers) "прибиваются" к конкретным ядрам, и аллокатор выдает память именно из того контроллера, который ближе всего к этому ядру.
Итог: Это убирает микро-задержки доступа к RAM, которые съедают 5-10% FPS в сценах с тысячами объектов.

### 41. GPU: Avoid LDS Bank Conflicts (Магия производительности)

Ты хочешь использовать LDS (Local Data Share) для оптимизации. Но если ты не будешь осторожен, шейдеры будут "тупить".
Проблема: LDS разделен на банки (обычно 32). Если ты читаешь данные так, что несколько потоков обращаются к разным адресам, попадающим в один банк, происходит Bank Conflict (задержка в 32 раза).
Решение (Swizzling):
При записи данных в LDS (например, позиции частиц для физики) делай "сдвиг" (swizzle).
Используй формулу: shared_data[index + (index >> 5)].
Это разносит данные по банкам, и потоки читают их без коллизий.
Результат: Ты получаешь почти 100% пропускную способность памяти GPU, а не 3-5%, как в обычных реализациях.

### 42. GPU: Indirect Command Buffer & "DGC" Chain

Ты используешь VK_EXT_device_generated_commands (DGC). Доведи это до абсолюта:
Full GPU Command Pipeline: Процессор вообще не должен касаться команд отрисовки.
Процессор только обновляет Uniform данные (Camera, Time).
Все остальное: отсечение, LOD, сортировка, выбор материалов — делается в Compute Shader'ах, которые пишут в IndirectBuffer.
Твоя фишка: Используй "Persistent Command Buffers". Не очищай их каждый кадр полностью. Очищай только те части, которые изменились (на основе isDirty флагов ECS). Это уберет оверхед записи команд.

### 43. CPU: "Data-Oriented" File I/O (Memory Mapping)

Никаких fstream, никакой сериализации в рантайме.
Решение: Memory Mapped Files (mmap / CreateFileMapping).
Как это работает: Ты "мапишь" весь файл уровня (ассеты, меши) в виртуальное адресное пространство процесса.
Плюс: Ты не загружаешь файл в RAM. Операционная система сама подгружает нужные страницы памяти (Demand Paging), когда ты к ним обращаешься. Это делает "загрузку" уровня практически мгновенной.
Совместимость: Это работает идеально с твоим подходом DirectStorage (NVMe -> VRAM).

### 44. CPU: Branchless ECS Systems

Ты используешь Flecs. Это круто, но можно быстрее.
Трюк: Используй Bitwise Dispatch.
Вместо того чтобы делать if (entity.has), используй битовую маску архетипа.
Напиши свои Systems так, чтобы они принимали указатели на колонки (SoA) и использовали _mm512_mask_load_ps (AVX-512 маскированная загрузка). Это позволяет обрабатывать сущности, даже если они имеют "дырки" в данных, без единого ветвления (if).

### 45. Архитектура «GPU-Resident Scene Graph»

Обычно CPU проходит по иерархии объектов, считает матрицы (Model-View-Projection), делает frustum culling и только потом отправляет данные на GPU. Это медленно.
Твое решение: Перенеси весь граф сцены на GPU.
Как: Весь мир — это один гигантский плоский массив (структура).
CPU только пишет input (повороты/позиции).
GPU Compute Shader делает обход графа, перемножает матрицы (через VK_KHR_cooperative_matrix для скорости) и заполняет IndirectDrawBuffer.
Итог: CPU вообще не знает, что там рисуется. Он — лишь «почтальон», передающий команды. Нагрузка на CPU падает практически до нуля.

### 46. Texture Space Shading (TSS) — Экономия пиксельного шейдера

Классический рендер пересчитывает освещение для каждого пикселя на экране каждый кадр. Это безумие.
Твое решение: Используй TSS. Ты рисуешь освещение (сложный PBR, GI) не на экране, а в текстуры, привязанные к поверхности модели (World Space).
Как: Если объект не двигается, ты берешь освещение из прошлого кадра.
Итог: Ты считаешь сложное освещение один раз для объекта, а не каждый раз, когда он поворачивается на экране. Для статики это дает 100-кратную экономику ресурсов.

### 47. "Dirty Page" Updates (Экономия шины памяти)

Видеопамять — это самое узкое место. Не обновляй то, что не менялось.
Твое решение: Разбей все буферы (Shadow Maps, SVT, Voxel Grids) на страницы (например, 128x128).
Как: Введи флаг isDirty для каждой страницы. Если объект в кадре не двигался — страница не перерисовывается.
Итог: Шина памяти не забивается мусором. GPU обновляет только те 5% экрана, где что-то произошло.

### 48. Dynamic Performance Budgeting (Управление теплопакетом)

Если GPU нагревается, он начинает троттлить (сбрасывать частоты), и FPS падает. Твой движок должен быть "термо-умным".
Твое решение: Внедряй Adaptive Quality (на базе vkGetQueryPoolResults).
Как:
Движок смотрит не только на FPS, но и на GPU Temperature / Power Draw.
Если бюджет превышен — движок плавно уменьшает, например, разрешение теней, количество световых лучей (ReSTIR) или переключает VRS с 1x1 на 2x2.
Это происходит незаметно для игрока, но спасает от "фризов" из-за перегрева видеокарты.

### 49. Data-Oriented "Cold/Hot" Separation (Экономия кэша CPU)

CPU тормозит из-за того, что кэши забиты данными, которые не нужны в конкретный момент.
Твое решение: Раздели память на Hot Storage (то, что нужно каждый кадр: Transform, Velocity, MeshBounds) и Cold Storage (имя объекта, настройки, описание, параметры GUI).
Как: Hot Storage лежит в непрерывных массивах (SoA) для идеального доступа. Cold Storage лежит в отдельных кучах и достается только когда ты заходишь в меню или взаимодействуешь с объектом.
Итог: Кэш-линии процессора на 100% забиты только тем, что нужно для расчетов.

### 50. Zero-Copy I/O (Отказ от RAM)

Твое решение: Все данные (текстуры, модели) должны лежать в файлах в таком формате, чтобы их можно было "маппить" (mmap) напрямую в память GPU (через DirectStorage / VK_NV_memory_decompression).
Итог: Данные из SSD попадают в VRAM, минуя системную оперативную память. Это в 10 раз быстрее обычного способа загрузки.

### 51. C++26 (Standard Requirements)

- std::mdspan: Для работы с 3D-текстурами и вокселями без затрат на абстракции.
- std::generator: Для создания высокоэффективных итераторов (например, по ECS компонентам), которые не требуют аллокаций.
- Concepts: Для строгой проверки типов в коде (например, requires IsComponent).
- Compile-Time Contracts: [[expects: pointer != nullptr]] для проверки на этапе компиляции.

### 52. CORE RULES (ЗАКОНЫ ДВИЖКА)

**DOD ONLY:** Никакого ООП, иерархий классов, наследования или виртуальных функций. Используй только POD-структуры, SoA (Structure of Arrays) и C++ Concepts для статического полиморфизма.

**MEMORY PURITY:** Запрещены new, malloc, std::make_shared/unique, std::vector (в горячем цикле). Используй только кастомные аллокаторы (TLSF, Arena, Linear), пулы или Memory-mapped файлы. Все аллокации должны быть явными и предсказуемыми.

**GPU-DRIVEN:** CPU — только дирижер. Вся логика (culling, animation, particle logic) должна быть на GPU. Передача данных — через BDA (Buffer Device Address) и Persistent Mapped Memory.

**ZERO-COPY:** Данные не должны копироваться в рантайме. Если данные пришли с диска (DirectStorage), они должны сразу попадать в VRAM или быть доступными по указателю в RAM без промежуточных memcpy.

**THREADING:** Никаких std::mutex, std::lock_guard или std::thread. Только Lock-free MPMC очереди, Atomic с memory_order_relaxed и Fiber-based Job System.

**VULKAN 1.4+:** Запрещены VkDescriptorSet и VkPipeline. Используй VK_EXT_descriptor_heap (Bindless), VK_EXT_shader_object (Shader Objects) и DGC (Device Generated Commands).

**BRANCHLESS:** Игнорируй if/else, если их можно заменить на математику, маски (masking) или cmov.

### 53. Что запрещено использовать (Черный список)

Чтобы ИИ не «халтурил», запрети ему следующее:
- <iostream>, printf, std::cout — используй свой FastLogger, который пишет в кольцевой буфер.
- std::string, std::stringstream, std::format (в игровом цикле) — всё должно быть хэшировано (MurmurHash3) или представлено std::string_view.
- Виртуальные деструкторы и dynamic_cast — это вызывает RTTI и добавляет оверхед.
- Recursive-вызовы в Hot-Path — это убивает стек и кэш.
- std::vector::push_back (в горячем цикле) — движок должен знать размер массива заранее.
- vkCmdPipelineBarrier (монолитные) — используй vkCmdPipelineBarrier2 с VkDependencyInfo, разделяй барьеры на Execution и Memory.
- Any-Hit Shaders в Ray Tracing — это медленно. Используй только Opaque геометрию.

### 54. Что нужно требовать от ИИ (Обязательные паттерны)

Требуй, чтобы код соответствовал этим паттернам:
- std::mdspan (C++26): Все массивы данных (текстуры, воксели, партиклы) должны быть обернуты в std::mdspan. Это позволяет ИИ писать чистый код view[x, y, z], который компилируется в эффективный адрес base + x + y*w + z*w*h.
- SIMD Intrinsics / AVX-512: Если ИИ пишет математику (матрицы, нормали, физика), требуй использование _mm512_ инструкций или явно проси "Auto-vectorization friendly" код.
- Cache-line padding: При создании структур, которые будут обновляться атомарно, заставляй ИИ добавлять alignas(64) и padding до 64 байт.
- Implicit Data Flow: Требуй от ИИ объяснения: "Откуда приходят данные для этого кода?". Если он говорит "из памяти", требуй ответа: "Как мы гарантируем, что эта память в кэше?".

---

**КОНЕЦ ДОПОЛНИТЕЛЬНЫХ АРХИТЕКТУРНЫХ ТРЕБОВАНИЙ 2026**