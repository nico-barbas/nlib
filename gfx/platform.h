#ifndef CORE_APP_H
#define CORE_APP_H

#include "core/allocator.h"
#include "core/array.h"
#include "core/log.h"
#include "core/math.h"
#include "core/strings.h"
#include "core/types.h"

#define APP_KEYBOARD_KEY_CAP 512
#define APP_MOUSE_BUTTON_CAP 5
#define APP_CHAR_BUFFER_CAP 512
#define APP_PEN_SAMPLE_BUFFER_CAP 512

/////////////////////////////
// Forward declarations
/////////////////////////////

// SDL
typedef struct SDL_Window SDL_Window;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUCommandBuffer SDL_GPUCommandBuffer;
typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUCopyPass SDL_GPUCopyPass;
typedef struct SDL_GPURenderPass SDL_GPURenderPass;
typedef struct SDL_GPUComputePass SDL_GPUComputePass;
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer;
typedef struct SDL_GPUBuffer SDL_GPUBuffer;
typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPUGraphicsPipeline SDL_GPUGraphicsPipeline;
typedef struct SDL_GPUComputePipeline SDL_GPUComputePipeline;

// typedef struct GPU_Texture GPU_Texture;
// typedef struct GPU_Render_Pass_Create_Info GPU_Render_Pass_Create_Info;

/////////////////////////////
// App
/////////////////////////////
// NOTE(nico): Might merge App_Error and GPU_Error
typedef enum App_Error {
  App_Error_None,
  App_Error_Failed_To_Init_Allocator,
  App_Error_Failed_To_Init_Backend,
  App_Error_Failed_To_Init_User_Resources,
  App_Error_Failed_To_Acquire_Render_Command_Buffer,
  App_Error_Failed_To_Acquire_Copy_Command_Buffer,
  App_Error_Failed_To_Acquire_Swapchain,
  App_Error_Failed_To_Submit_Render_Command_Buffer,
  App_Error_Failed_To_Submit_Copy_Command_Buffer,
  App_Error_Failed_To_Begin_Copy_Pass,
  App_Error_Failed_To_End_Copy_Pass,
  App_Error_Failed_To_Begin_Render_Pass,
  App_Error_Failed_To_End_Render_Pass,
  App_Error_Failed_To_Begin_Compute_Pass,
  App_Error_Failed_To_End_Compute_Pass,
  App_Error_Failed_To_Push_Vertex_Uniform,
} App_Error;

typedef enum App_Window_Backend {
  App_Window_Backend_Auto,
  App_Window_Backend_X11,
  App_Window_Backend_Wayland,
} App_Window_Backend;

typedef enum App_GPU_Backend {
  App_GPU_Backend_Auto,
  App_GPU_Backend_Vulkan,
  APP_GPU_Backend_DX12,
  APP_GPU_Backend_METAL,
} App_GPU_Backend;

typedef struct App_Pen_Sample {
  u64 timestamp;
  Vec2 position;
  f32 pressure;
  bool8 pressed;
} App_Pen_Sample;

typedef Array(App_Pen_Sample) App_Pen_Sample_Buffer;

typedef struct App {
  Allocator allocator;
  Logger logger;

  SDL_Window *window_handle;
  App_Window_Backend window_backend;
  App_GPU_Backend gpu_backend;
  i32 window_width;
  i32 window_height;
  String window_title;

  // GPU resources
  SDL_GPUDevice *gpu_device;
  SDL_GPUCommandBuffer *gpu_graphics_commands;
  SDL_GPUCommandBuffer *gpu_copy_commands;
  SDL_GPURenderPass *gpu_render_pass;   // Only one alive at the same time
  SDL_GPUCopyPass *gpu_copy_pass;       // Only one alive at the same time
  SDL_GPUComputePass *gpu_compute_pass; // Only one alive at the same time
  SDL_GPUTexture *gpu_swapchain;
  u32 gpu_composition;
  u32 gpu_present_mode;
  u32 gpu_swapchain_format;

  // Runtime states
  bool32 running;
  u64 last_time_ns;
  u64 current_time_ns;
  f32 elapsed_time;
  Allocator backing_allocator;
  Allocator frame_allocator;
  Arena_Data frame_arena;

  // Input states
  Vec2 mouse_position;
  Vec2 previous_mouse_position;
  Vec2 mouse_scoll;
  utf8_char char_buffer[APP_CHAR_BUFFER_CAP];
  usize char_buffer_len;
  struct {
    bool8 previous;
    bool8 current;
  } mouse[APP_MOUSE_BUTTON_CAP];
  struct {
    bool8 previous;
    bool8 current;
    u8 presses;
  } keys[APP_KEYBOARD_KEY_CAP];

  struct {
    u32 id;
    Option(u64) current_timestamp;
    bool32 pressed;
    Vec2 position;
    f32 pressure;
  } pen;
  App_Pen_Sample pen_samples_buffer[APP_PEN_SAMPLE_BUFFER_CAP];
  usize pen_samples_buffer_len;
} App;

typedef struct App_Create_Info {
  App *app;
  i32 window_width;
  i32 window_height;
  String window_title;

  App_Window_Backend window_backend;
  App_GPU_Backend gpu_backend;

  Logger logger;

  // init
  rawptr user_data;
  bool32 (*init_resources_proc)(rawptr data);
} App_Create_Info;

typedef enum Mouse_Button {
  Mouse_Button_Left = 1,
  Mouse_Button_Right = 2,
  Mouse_Button_Middle = 3,
} Mouse_Button;

// Values are SDL scancodes (USB HID usages, named by their US-layout caps), so
// SDL_KeyboardEvent.scancode indexes App.keys directly. APP_KEYBOARD_KEY_CAP
// matches SDL_SCANCODE_COUNT for the same reason.
typedef enum Keyboard_Key {
  Keyboard_Key_Null = 0, // SDL_SCANCODE_UNKNOWN, used for no key pressed
  // Alphanumeric keys
  Keyboard_Key_A = 4,              // Key: A | a
  Keyboard_Key_B = 5,              // Key: B | b
  Keyboard_Key_C = 6,              // Key: C | c
  Keyboard_Key_D = 7,              // Key: D | d
  Keyboard_Key_E = 8,              // Key: E | e
  Keyboard_Key_F = 9,              // Key: F | f
  Keyboard_Key_G = 10,             // Key: G | g
  Keyboard_Key_H = 11,             // Key: H | h
  Keyboard_Key_I = 12,             // Key: I | i
  Keyboard_Key_J = 13,             // Key: J | j
  Keyboard_Key_K = 14,             // Key: K | k
  Keyboard_Key_L = 15,             // Key: L | l
  Keyboard_Key_M = 16,             // Key: M | m
  Keyboard_Key_N = 17,             // Key: N | n
  Keyboard_Key_O = 18,             // Key: O | o
  Keyboard_Key_P = 19,             // Key: P | p
  Keyboard_Key_Q = 20,             // Key: Q | q
  Keyboard_Key_R = 21,             // Key: R | r
  Keyboard_Key_S = 22,             // Key: S | s
  Keyboard_Key_T = 23,             // Key: T | t
  Keyboard_Key_U = 24,             // Key: U | u
  Keyboard_Key_V = 25,             // Key: V | v
  Keyboard_Key_W = 26,             // Key: W | w
  Keyboard_Key_X = 27,             // Key: X | x
  Keyboard_Key_Y = 28,             // Key: Y | y
  Keyboard_Key_Z = 29,             // Key: Z | z
  Keyboard_Key_One = 30,           // Key: 1
  Keyboard_Key_Two = 31,           // Key: 2
  Keyboard_Key_Three = 32,         // Key: 3
  Keyboard_Key_Four = 33,          // Key: 4
  Keyboard_Key_Five = 34,          // Key: 5
  Keyboard_Key_Six = 35,           // Key: 6
  Keyboard_Key_Seven = 36,         // Key: 7
  Keyboard_Key_Eight = 37,         // Key: 8
  Keyboard_Key_Nine = 38,          // Key: 9
  Keyboard_Key_Zero = 39,          // Key: 0
  Keyboard_Key_Minus = 45,         // Key: -
  Keyboard_Key_Equal = 46,         // Key: =
  Keyboard_Key_Left_Bracket = 47,  // Key: [
  Keyboard_Key_Right_Bracket = 48, // Key: ]
  Keyboard_Key_Backslash = 49,     // Key: '\'
  Keyboard_Key_Semicolon = 51,     // Key: ;
  Keyboard_Key_Apostrophe = 52,    // Key: '
  Keyboard_Key_Grave = 53,         // Key: `
  Keyboard_Key_Comma = 54,         // Key: ,
  Keyboard_Key_Period = 55,        // Key: .
  Keyboard_Key_Slash = 56,         // Key: /
  // Function keys
  Keyboard_Key_Enter = 40,          // Key: Enter
  Keyboard_Key_Escape = 41,         // Key: Esc
  Keyboard_Key_Backspace = 42,      // Key: Backspace
  Keyboard_Key_Tab = 43,            // Key: Tab
  Keyboard_Key_Space = 44,          // Key: Space
  Keyboard_Key_Caps_Lock = 57,      // Key: Caps lock
  Keyboard_Key_F1 = 58,             // Key: F1
  Keyboard_Key_F2 = 59,             // Key: F2
  Keyboard_Key_F3 = 60,             // Key: F3
  Keyboard_Key_F4 = 61,             // Key: F4
  Keyboard_Key_F5 = 62,             // Key: F5
  Keyboard_Key_F6 = 63,             // Key: F6
  Keyboard_Key_F7 = 64,             // Key: F7
  Keyboard_Key_F8 = 65,             // Key: F8
  Keyboard_Key_F9 = 66,             // Key: F9
  Keyboard_Key_F10 = 67,            // Key: F10
  Keyboard_Key_F11 = 68,            // Key: F11
  Keyboard_Key_F12 = 69,            // Key: F12
  Keyboard_Key_Print_Screen = 70,   // Key: Print screen
  Keyboard_Key_Scroll_Lock = 71,    // Key: Scroll lock
  Keyboard_Key_Pause = 72,          // Key: Pause
  Keyboard_Key_Insert = 73,         // Key: Ins
  Keyboard_Key_Home = 74,           // Key: Home
  Keyboard_Key_Page_Up = 75,        // Key: Page up
  Keyboard_Key_Delete = 76,         // Key: Del
  Keyboard_Key_End = 77,            // Key: End
  Keyboard_Key_Page_Down = 78,      // Key: Page down
  Keyboard_Key_Right = 79,          // Key: Cursor right
  Keyboard_Key_Left = 80,           // Key: Cursor left
  Keyboard_Key_Down = 81,           // Key: Cursor down
  Keyboard_Key_Up = 82,             // Key: Cursor up
  Keyboard_Key_Num_Lock = 83,       // Key: Num lock
  Keyboard_Key_Kb_Menu = 101,       // Key: KB menu (SDL_SCANCODE_APPLICATION)
  Keyboard_Key_Left_Control = 224,  // Key: Control left
  Keyboard_Key_Left_Shift = 225,    // Key: Shift left
  Keyboard_Key_Left_Alt = 226,      // Key: Alt left
  Keyboard_Key_Left_Super = 227,    // Key: Super left
  Keyboard_Key_Right_Control = 228, // Key: Control right
  Keyboard_Key_Right_Shift = 229,   // Key: Shift right
  Keyboard_Key_Right_Alt = 230,     // Key: Alt right
  Keyboard_Key_Right_Super = 231,   // Key: Super right
  // Keypad keys
  Keyboard_Key_Kp_Divide = 84,   // Key: Keypad /
  Keyboard_Key_Kp_Multiply = 85, // Key: Keypad *
  Keyboard_Key_Kp_Subtract = 86, // Key: Keypad -
  Keyboard_Key_Kp_Add = 87,      // Key: Keypad +
  Keyboard_Key_Kp_Enter = 88,    // Key: Keypad Enter
  Keyboard_Key_Kp_1 = 89,        // Key: Keypad 1
  Keyboard_Key_Kp_2 = 90,        // Key: Keypad 2
  Keyboard_Key_Kp_3 = 91,        // Key: Keypad 3
  Keyboard_Key_Kp_4 = 92,        // Key: Keypad 4
  Keyboard_Key_Kp_5 = 93,        // Key: Keypad 5
  Keyboard_Key_Kp_6 = 94,        // Key: Keypad 6
  Keyboard_Key_Kp_7 = 95,        // Key: Keypad 7
  Keyboard_Key_Kp_8 = 96,        // Key: Keypad 8
  Keyboard_Key_Kp_9 = 97,        // Key: Keypad 9
  Keyboard_Key_Kp_0 = 98,        // Key: Keypad 0
  Keyboard_Key_Kp_Decimal = 99,  // Key: Keypad .
  Keyboard_Key_Kp_Equal = 103,   // Key: Keypad =
  // Android key buttons
  Keyboard_Key_Back = 282, // Key: Android back button (SDL_SCANCODE_AC_BACK)
  Keyboard_Key_Menu = 118, // Key: Android menu button
  Keyboard_Key_Volume_Up = 128,   // Key: Android volume up button
  Keyboard_Key_Volume_Down = 129, // Key: Android volume down button
} Keyboard_Key;

typedef Array(utf8_char) Text_Array;

App_Error init_app(App_Create_Info *info, Allocator allocator);
void close_app(App *app);

bool32 app_update(App *app);
App_Error app_begin_frame(App *app);
App_Error app_end_frame(App *app);

Allocator app_get_frame_allocator(void);

u64 app_get_current_time_ns(void);
u64 app_get_last_time_ns(void);
f32 app_get_total_time(void);
f32 app_get_elapsed_time(void);
Vec2 app_get_window_size(void);
String app_get_clipboard_content(void);

Vec2 app_mouse_position(void);
Vec2 app_mouse_delta(void);
Vec2 app_mouse_scroll(void);
void app_capture_mouse(bool32 on);

bool8 app_mouse_pressed(Mouse_Button button);
bool8 app_mouse_just_pressed(Mouse_Button button);
bool8 app_mouse_just_released(Mouse_Button button);
bool8 app_key_pressed(Keyboard_Key key);
bool8 app_key_just_pressed(Keyboard_Key key);
u32 app_key_press_count(Keyboard_Key key);
Text_Array app_chars_pressed(void);
App_Pen_Sample_Buffer app_pen_samples(void);

/////////////////////////////
/////////////////////////////
// GPU abstraction
/////////////////////////////
/////////////////////////////
typedef enum GPU_Error {
  GPU_Error_None,
  GPU_Error_Uninitialized_Backend,
  GPU_Error_Uninitialized_Copy_Command_Buffer,
  GPU_Error_Uninitialized_Render_Pass,
  GPU_Error_Uninitialized_Compute_Pass,
  GPU_Error_Invalid_Streaming_Buffer,
  GPU_Error_Invalid_Buffer,
  GPU_Error_Invalid_Texture,
  GPU_Error_Invalid_Sampler,
  GPU_Error_Invalid_Pipeline,
  GPU_Error_Failed_To_Create_Stream,
  GPU_Error_Failed_To_Create_Buffer,
  GPU_Error_Failed_To_Create_Texture,
  GPU_Error_Failed_To_Create_Sampler,
  GPU_Error_Failed_To_Create_Shader,
  GPU_Error_Failed_To_Create_Pipeline,
  GPU_Error_Failed_To_Write_Memory,
  GPU_Error_Failed_To_Write_Texture,
  GPU_Failed_To_Bind_Sampled_Texture,
} GPU_Error;

typedef enum GPU_Blend_Mode {
  GPU_Blend_Mode_None,
  GPU_Blend_Mode_Straight_Alpha,
  GPU_Blend_Mode_Premultiplied_Alpha,
} GPU_Blend_Mode;

typedef enum GPU_Depth_Mode {
  GPU_Depth_Mode_None,
  GPU_Depth_Mode_Read,
  GPU_Depth_Mode_Read_Write,
} GPU_Depth_Mode;

typedef enum GPU_Load_Op {
  GPU_Load_Op_Load,
} GPU_Load_Op;

typedef enum GPU_Store_Op {
  GPU_Store_Op_Store,
} GPU_Store_Op;

/////////////////////////////
// GPU Buffer management
/////////////////////////////
typedef u32 GPU_Buffer_Usage;
typedef enum GPU_Buffer_Usage_Kind {
  GPU_Buffer_Usage_Kind_Vertex = 1 << 0,
  GPU_Buffer_Usage_Kind_Index = 1 << 1,
  GPU_Buffer_Usage_Kind_Indirect = 1 << 2,
  GPU_Buffer_Usage_Kind_Graphics_Read = 1 << 3,
  GPU_Buffer_Usage_Kind_Compute_Read = 1 << 4,
  GPU_Buffer_Usage_Kind_Compute_Write = 1 << 5,
} GPU_Buffer_Usage_Kind;

typedef struct GPU_Buffer {
  SDL_GPUBuffer *handle;
  GPU_Buffer_Usage usage;
  usize size;
} GPU_Buffer;

typedef Result(GPU_Buffer, GPU_Error) GPU_Buffer_Create_Result;

GPU_Buffer_Create_Result make_gpu_buffer(GPU_Buffer_Usage usage, usize size);
GPU_Error destroy_gpu_buffer(GPU_Buffer *buffer);

typedef struct GPU_Memory {
  GPU_Buffer *buffer;
  usize offset;
  usize size;
} GPU_Memory;

typedef Result(GPU_Memory, Allocation_Error) GPU_Allocation_Result;

typedef struct GPU_Allocator GPU_Allocator;
struct GPU_Allocator {
  GPU_Buffer *buffer;
  rawptr ptr;
  usize align;
  GPU_Allocation_Result (*alloc_proc)(GPU_Allocator allocator, usize size);
  Allocation_Error (*free_proc)(GPU_Allocator allocator, GPU_Memory memory);
  Allocation_Error (*free_all_proc)(GPU_Allocator allocator);
};

#define gpu_alloc(allocator, size) ((allocator).alloc_proc((allocator), size))
#define gpu_free(allocator, mem) ((allocator).free_proc((allocator), mem))
#define gpu_free_all(allocator) ((allocator).free_all_proc((allocator)))

typedef struct GPU_Arena_Data {
  usize offset;
} GPU_Arena_Data;

GPU_Allocator gpu_arena_allocator(GPU_Arena_Data *arena, GPU_Buffer *buffer);

// Data manipulation operations
typedef enum GPU_Streaming_Buffer_Kind {
  GPU_Streaming_Buffer_Kind_Write,
  GPU_Streaming_Buffer_Kind_Read,
} GPU_Streaming_Buffer_Kind;

typedef struct GPU_Streaming_Buffer {
  GPU_Streaming_Buffer_Kind kind;
  SDL_GPUTransferBuffer *handle;
  usize size;
  usize current_offset;
} GPU_Streaming_Buffer;

typedef struct GPU_Streaming_Buffer_Create_Info {
  GPU_Streaming_Buffer_Kind kind;
  usize size;
} GPU_Streaming_Buffer_Create_Info;

typedef Result(
    GPU_Streaming_Buffer, GPU_Error
) GPU_Streaming_Buffer_Create_Result;

GPU_Streaming_Buffer_Create_Result
make_gpu_streaming_buffer(GPU_Streaming_Buffer_Create_Info *info);
GPU_Error destroy_gpu_streaming_buffer(GPU_Streaming_Buffer *stream);
GPU_Error gpu_streaming_buffer_begin(GPU_Streaming_Buffer *stream);
// NOTE(nico): end for completeness, it is a no-op if both begin and end are
// paired correctly
GPU_Error gpu_streaming_buffer_end(GPU_Streaming_Buffer *stream);

typedef struct GPU_Buffer_Write_Info {
  GPU_Memory *memory;
  rawptr data;
  usize size;
} GPU_Buffer_Write_Info;

GPU_Error
gpu_memory_write(GPU_Streaming_Buffer *stream, GPU_Buffer_Write_Info *info);

/////////////////////////////
// GPU Texture management
/////////////////////////////
typedef enum GPU_Texture_Space {
  GPU_Texture_Space_Linear,
  GPU_Texture_Space_sRGB,
} GPU_Texture_Space;

// NOTE(nico): values mirror SDL_GPUTextureFormat so they can be cast directly
typedef enum GPU_Texture_Format {
  GPU_Texture_Format_R8Unorm = 0x00000002,
  GPU_Texture_Format_R16Unorm = 0x00000005,
  GPU_Texture_Format_RG8Unorm = 0x00000003,
  GPU_Texture_Format_RG16Unorm = 0x00000006,
  GPU_Texture_Format_RGBA8Unorm = 0x00000004,
  GPU_Texture_Format_RGBA8UnormSrgb = 0x00000034,
  GPU_Texture_Format_RGBA16Unorm = 0x00000007,
  GPU_Texture_Format_RGBA16Float = 0x0000001D,
  GPU_Texture_Format_BGRA8Unorm = 0x0000000C,
  GPU_Texture_Format_BGRA8UnormSrgb = 0x00000035,
  GPU_Texture_Format_Depth24 = 0x0000003B,
  GPU_Texture_Format_Depth24_Stencil8 = 0x0000003D,
} GPU_Texture_Format;

// NOTE(nico): For this application, idk if we need simultaneous reade/write.
// Need to check what the difference is with read | write, but SDL states that
// they are not the same
typedef u32 GPU_Texture_Usage;
typedef enum GPU_Texture_Usage_Kind {
  GPU_Texture_Usage_Kind_Sample = 1 << 0,
  GPU_Texture_Usage_Kind_Color_Target = 1 << 1,
  GPU_Texture_Usage_Kind_Depth_Target = 1 << 2,
  GPU_Texture_Usage_Kind_Graphics_Read = 1 << 3,
  GPU_Texture_Usage_Kind_Compute_Read = 1 << 4,
  GPU_Texture_Usage_Kind_Compute_Write = 1 << 5,
  GPU_Texture_Usage_Kind_Compute_Read_Write = 1 << 6,
} GPU_Texture_Usage_Kind;

// NOTE(nico): Let's only handle 2d textures for now. No texture arrays no 3d
// textures
typedef struct GPU_Texture {
  SDL_GPUTexture *handle;
  GPU_Texture_Usage usage;
  GPU_Texture_Format format;
  u32 width;
  u32 height;
  u32 mip_level;
  usize byte_size;
} GPU_Texture;

typedef struct GPU_Texture_Create_Info {
  GPU_Texture_Usage usage;
  GPU_Texture_Format format;
  u32 width;
  u32 height;
  // Let's not support mip levels for now
} GPU_Texture_Create_Info;

// NOTE(nico): we need to support region write pretty soon
typedef struct GPU_Texture_Write_Info {
  GPU_Texture *texture;
  rawptr data;
  usize size;
} GPU_Texture_Write_Info;

typedef Result(GPU_Texture, GPU_Error) GPU_Texture_Create_Result;

GPU_Texture_Create_Result make_gpu_texture(GPU_Texture_Create_Info *info);
GPU_Error destroy_gpu_texture(GPU_Texture *texture);

GPU_Error
gpu_texture_write(GPU_Streaming_Buffer *stream, GPU_Texture_Write_Info *info);

typedef enum GPU_Sampler_Filter {
  GPU_Sampler_Filter_Nearest,
  GPU_Sampler_Filter_Linear,
} GPU_Sampler_Filter;

typedef enum GPU_Sampler_Wrap {
  GPU_Sampler_Wrap_Repeat,
  GPU_Sampler_Wrap_Mirrored_Repeat,
  GPU_Sampler_Wrap_Clamp,
} GPU_Sampler_Wrap;

typedef struct GPU_Sampler {
  SDL_GPUSampler *handle;
  GPU_Sampler_Filter min_filter;
  GPU_Sampler_Filter mag_filter;
  GPU_Sampler_Wrap wrap_u;
  GPU_Sampler_Wrap wrap_v;
  GPU_Sampler_Wrap wrap_w;
} GPU_Sampler;

typedef struct GPU_Sampler_Create_Info {
  GPU_Sampler_Filter min_filter;
  GPU_Sampler_Filter mag_filter;
  GPU_Sampler_Wrap wrap_u;
  GPU_Sampler_Wrap wrap_v;
  GPU_Sampler_Wrap wrap_w;
} GPU_Sampler_Create_Info;

typedef Result(GPU_Sampler, GPU_Error) GPU_Sampler_Create_Result;

GPU_Sampler_Create_Result make_gpu_sampler(GPU_Sampler_Create_Info *info);
GPU_Error destroy_gpu_sampler(GPU_Sampler *sampler);

////////////////////////////////
// Gpu pipeline
////////////////////////////////
typedef enum GPU_Pipeline_Kind {
  GPU_Pipeline_Kind_Graphics,
  GPU_Pipeline_Kind_Compute,
} GPU_Pipeline_Kind;

typedef enum GPU_Vertex_Attribute_Format {
  GPU_Vertex_Attribute_Format_Vec2,
  GPU_Vertex_Attribute_Format_Vec3,
  GPU_Vertex_Attribute_Format_Vec4,
  GPU_Vertex_Attribute_Format_MAX,
} GPU_Vertex_Attribute_Format;

typedef struct GPU_Vertex_Attribute {
  GPU_Vertex_Attribute_Format format;
  u32 location;
  u32 buffer_slot;
  usize byte_offset;
} GPU_Vertex_Attribute;

typedef struct GPU_Pipeline {
  GPU_Pipeline_Kind kind;
  union {
    struct {
      SDL_GPUGraphicsPipeline *handle;
      GPU_Depth_Mode depth_mode;
    } graphics;
    struct {
      SDL_GPUComputePipeline *handle;
    } compute;
  };
} GPU_Pipeline;

typedef struct GPU_Graphics_Shader_Create_Info {
  String source;
  String entrypoint;
  usize uniform_buffer_count;
  usize storage_texture_count;
  usize storage_buffer_count;
  usize sampler_count;
} GPU_Graphics_Shader_Create_Info;

typedef struct GPU_Compute_Shader_Create_Info {
  String source;
  String entrypoint;
  usize sampler_count;
  usize storage_texture_read_only_count;
  usize storage_buffer_read_only_count;
  usize storage_texture_read_write_count;
  usize storage_buffer_read_write_count;
  usize uniform_buffer_count;
  u32 thread_count_x;
  u32 thread_count_y;
  u32 thread_count_z;
} GPU_Compute_Shader_Create_Info;

typedef struct GPU_Pipeline_Target_Create_Info {
  GPU_Texture_Format format;
  GPU_Blend_Mode blend;
} GPU_Pipeline_Target_Create_Info;

typedef struct GPU_Pipeline_Create_Info {
  GPU_Pipeline_Kind kind;
  union {
    struct {
      GPU_Graphics_Shader_Create_Info vertex;
      GPU_Graphics_Shader_Create_Info fragment;
      usize vertex_byte_size;
      Array(GPU_Vertex_Attribute) attributes;
      Array(GPU_Pipeline_Target_Create_Info) targets;
      GPU_Depth_Mode depth_mode;
    } graphics;
    struct {
      GPU_Compute_Shader_Create_Info shader;
    } compute;
  };
} GPU_Pipeline_Create_Info;

typedef Result(GPU_Pipeline, GPU_Error) GPU_Pipeline_Create_Result;

GPU_Pipeline_Create_Result make_gpu_pipeline(GPU_Pipeline_Create_Info *info);
GPU_Error destroy_gpu_pipeline(GPU_Pipeline *pipeline);

////////////////////////////////
// Gpu pass
////////////////////////////////
typedef enum GPU_Pass_Kind {
  // Missing copy for completeness but fuck it
  GPU_Pass_Kind_Compute,
  GPU_Pass_Kind_Render,
} GPU_Pass_Kind;

typedef struct GPU_Render_Pass_Color_Target {
  GPU_Texture *texture;
  Color clear_color;
  GPU_Load_Op load_op;
  GPU_Store_Op store_op;
} GPU_Render_Pass_Color_Target;

typedef struct GPU_Render_Pass_Depth_Target {
  GPU_Texture *texture;
  f32 clear_value;
  GPU_Load_Op load_op;
  GPU_Store_Op store_op;
} GPU_Render_Pass_Depth_Target;

// NOTE(nico): Let's drop the cycle on this feature too for now. We might need
// it later with all the compute planned
typedef struct GPU_Render_Pass_Create_Info {
  Array(GPU_Render_Pass_Color_Target) color_targets;
  Option(GPU_Render_Pass_Depth_Target) depth_target;
} GPU_Render_Pass_Create_Info;

typedef struct GPU_Compute_Pass_Create_Info {
  // NOTE(nico): Let's see if we need to pass an offset.
  // We probably do and this will be in the push constant

  // NOTE(nico): We drop cycles here as well..
  Array(GPU_Buffer *) read_write_storage_buffers;
  Array(GPU_Texture *) read_write_storage_textures;
} GPU_Compute_Pass_Create_Info;

////////////////////////////////
// Application tied operations
////////////////////////////////
typedef struct GPU_Textures_Bind_Info {
  GPU_Pass_Kind target_pass;
  Array(GPU_Texture *) textures;
  Array(GPU_Sampler *) samplers;
  u32 first_slot;
} GPU_Textures_Bind_Info;

typedef struct GPU_Memory_Bind_Info {
  GPU_Pass_Kind target_pass;
  GPU_Memory memory;
  u32 slot;
} GPU_Memory_Bind_Info;

typedef struct GPU_Primitive_Draw_Info {
  u32 vertex_count;
  u32 first_vertex;
} GPU_Primitive_Draw_Info;

typedef struct GPU_Compute_Dispatch_Info {
  usize group_count_x;
  usize group_count_y;
  usize group_count_z;
} GPU_Compute_Dispatch_Info;

typedef Result(GPU_Texture, GPU_Error) GPU_Swapchain_Texture_Result;
typedef Result(GPU_Texture_Format, GPU_Error) GPU_Swapchain_Format_Result;

App_Error app_begin_copy_pass(void);
App_Error app_end_copy_pass(void);
App_Error app_begin_render_pass(GPU_Render_Pass_Create_Info *info);
App_Error app_end_render_pass(void);
App_Error app_begin_compute_pass(GPU_Compute_Pass_Create_Info *info);
App_Error app_end_compute_pass(void);

GPU_Swapchain_Texture_Result app_get_swapchain_texture(void);
GPU_Swapchain_Format_Result app_get_swapchain_format(void);
GPU_Error app_bind_pipeline(GPU_Pipeline *pipeline);
GPU_Error app_push_vertex_uniform(u32 slot, rawptr data, usize size);
GPU_Error app_push_compute_uniform(u32 slot, rawptr data, usize size);
GPU_Error app_bind_sampled_textures(GPU_Textures_Bind_Info *info);
GPU_Error app_bind_storage_memory(GPU_Memory_Bind_Info *info);
GPU_Error
app_bind_storage_texture(GPU_Pass_Kind pass, GPU_Texture *texture, u32 slot);

GPU_Error app_draw_primitive(GPU_Primitive_Draw_Info *info);
GPU_Error app_dispatch_compute(GPU_Compute_Dispatch_Info *info);

#endif
