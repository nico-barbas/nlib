#include "core/platform.h"

#include "core/allocator.h"
#include "core/log.h"
#include "core/math.h"
#include "core/runtime.h"
#include "core/strings.h"
#include "core/types.h"
#include "stb_image.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pen.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

// libc crap
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
  FIXME(nico):
  - Render-target capacity assertion is reversed.
  - Bind-group handles and derived texture views leak.
  - Depth24Plus has an outdated enum value.
  - Buffer slices retain pointers to movable wrappers.
  - Texture channel accounting is incorrect after forced RGBA decoding.
  - Pipeline entry points and shader visibility are hardcoded.
  - Several partial failures leak resources.
  - GPU parent resources are released before children.
*/

static_assert(
    APP_KEYBOARD_KEY_CAP == SDL_SCANCODE_COUNT,
    "App keyboard buffer cap must be equal to SDL scancode count"
);

static_assert(
    (u32)GPU_Texture_Format_R8Unorm == (u32)SDL_GPU_TEXTUREFORMAT_R8_UNORM &&
        (u32)GPU_Texture_Format_R16Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_R16_UNORM &&
        (u32)GPU_Texture_Format_RG8Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_R8G8_UNORM &&
        (u32)GPU_Texture_Format_RG16Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_R16G16_UNORM &&
        (u32)GPU_Texture_Format_RGBA8Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM &&
        (u32)GPU_Texture_Format_RGBA8UnormSrgb ==
            (u32)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB &&
        (u32)GPU_Texture_Format_RGBA16Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM &&
        (u32)GPU_Texture_Format_BGRA8Unorm ==
            (u32)SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM &&
        (u32)GPU_Texture_Format_BGRA8UnormSrgb ==
            (u32)SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB &&
        (u32)GPU_Texture_Format_Depth24 ==
            (u32)SDL_GPU_TEXTUREFORMAT_D24_UNORM &&
        (u32)GPU_Texture_Format_Depth24_Stencil8 ==
            (u32)SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
    "GPU_Texture_Format drift from SDL declaration, please fix."
);

#define FRAME_ARENA_SIZE (50 * MEGABYTE)

static App *_app = nullptr;

static void platform_log_error(Logger *logger) {
  const char *msg = SDL_GetError();
  if (msg != nullptr) {
    log_error(logger, msg);
  }
}

static void app_publish_pen_sample(App *app) {
  assert(app->pen.current_timestamp.some);
  assert(app->pen_samples_buffer_len < APP_PEN_SAMPLE_BUFFER_CAP);

  // FIXME(nico): need to be careful of frame boundaries

  // FIXME(nico): do not drop samples. It is very unlikely that we reach the cap
  // of this buffer
  if (app->pen_samples_buffer_len < APP_PEN_SAMPLE_BUFFER_CAP) {
    app->pen_samples_buffer[app->pen_samples_buffer_len++] = (App_Pen_Sample){
      .timestamp = app->pen.current_timestamp.value,
      .position = app->pen.position,
      .pressure = app->pen.pressure,
      .pressed = (bool8)app->pen.pressed,
    };
  }
}

App_Error init_app(App_Create_Info *info, Allocator allocator) {
  errdefer_scope;

  // FIXME(nico): This is not the best. Fine for now
  assert(info->logger.log_proc != nullptr);

  App *app = info->app;
  *app = (App){0};

  app->allocator = allocator;
  app->logger = info->logger;
  app->backing_allocator = allocator;

  init_arena(
      &app->frame_arena,
      unwrap(alloc(allocator, FRAME_ARENA_SIZE)),
      FRAME_ARENA_SIZE
  );
  app->frame_allocator = arena_allocator(&app->frame_arena);

#if defined(_WIN32)
  if (info->window_backend != App_Window_Backend_Auto) {
    log_error(
        &app->logger,
        "Invalid Window backend selected. On Windows, only "
        "App_Window_Backend_Auto is supported"
    );
    return App_Error_Failed_To_Init_Backend;
  }

#elif defined(__linux__)
  switch (info->window_backend) {
  case App_Window_Backend_Auto:
    log_debug(&app->logger, "Window backend auto selected");
    break;
  case App_Window_Backend_X11:
    log_debug(&app->logger, "Window backend x11 selected");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    break;
  case App_Window_Backend_Wayland:
    log_debug(&app->logger, "Window backend wayland selected");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");
    break;
  default:
    log_error(&app->logger, "Invalid Window backend selected");
    return App_Error_Failed_To_Init_Backend;
  }
#endif

  SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_PEN_TOUCH_EVENTS, "0");

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    log_error(&app->logger, "Failed to initialize sdl");
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }

  app->window_backend = info->window_backend;
  app->gpu_backend = info->gpu_backend;

  SDL_PropertiesID window_props = SDL_CreateProperties();
  defer {
    SDL_DestroyProperties(window_props);
  };

  SDL_SetStringProperty(
      window_props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, info->window_title.data
  );
  SDL_SetNumberProperty(
      window_props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, info->window_width
  );
  SDL_SetNumberProperty(
      window_props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, info->window_height
  );
  SDL_SetNumberProperty(
      window_props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED
  );
  SDL_SetNumberProperty(
      window_props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED
  );
  SDL_SetNumberProperty(
      window_props,
      SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
  );
  app->window_handle = SDL_CreateWindowWithProperties(window_props);
  app->window_width = info->window_width;
  app->window_height = info->window_height;
  app->window_title = or_return(
      string_clone(info->window_title, allocator),
      App_Error_Failed_To_Init_Allocator
  );

  if (app->window_handle == nullptr) {
    log_error(&app->logger, "Failed to open a window");
    return App_Error_Failed_To_Init_Allocator;
  }
  errdefer {
    SDL_DestroyWindow(app->window_handle);
  };

  SDL_PropertiesID gpu_props = SDL_CreateProperties();
  SDL_GPUVulkanOptions vk_options = {
    // NOTE(nico): version written by hand to avoid including vulkan_core.h on
    // platform that do not support and use a different backend
    .vulkan_api_version = (1u << 22) | (1u << 12)
  };
  SDL_SetBooleanProperty(
      gpu_props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true
  );
  SDL_SetBooleanProperty(
      gpu_props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true
  );
  SDL_SetBooleanProperty(
      gpu_props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true
  );

#if defined(DEBUG)
  SDL_SetBooleanProperty(
      gpu_props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true
  );
#endif

  switch (app->gpu_backend) {
  case App_GPU_Backend_Auto:
    break;
  case App_GPU_Backend_Vulkan:
    SDL_SetStringProperty(
        gpu_props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan"
    );
    SDL_SetPointerProperty(
        gpu_props,
        SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER,
        &vk_options
    );
    break;
  case APP_GPU_Backend_DX12:
    SDL_SetStringProperty(
        gpu_props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "direct3d12"
    );
    break;
  case APP_GPU_Backend_METAL:
    SDL_SetStringProperty(
        gpu_props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "metal"
    );
    break;
  }

  app->gpu_device = SDL_CreateGPUDeviceWithProperties(gpu_props);
  if (app->gpu_device == nullptr) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }
  errdefer {
    SDL_DestroyGPUDevice(app->gpu_device);
  };

  if (!SDL_ClaimWindowForGPUDevice(app->gpu_device, app->window_handle)) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }

  static const SDL_GPUPresentMode preferred_present_modes[] = {
    SDL_GPU_PRESENTMODE_MAILBOX,
    SDL_GPU_PRESENTMODE_IMMEDIATE,
    SDL_GPU_PRESENTMODE_VSYNC,
  };
  SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_VSYNC;
  for (usize i = 0; i < countof(preferred_present_modes); i += 1) {
    if (SDL_WindowSupportsGPUPresentMode(
            app->gpu_device, app->window_handle, preferred_present_modes[i]
        )) {
      present_mode = preferred_present_modes[i];
    }
  }

  SDL_GPUSwapchainComposition composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
  if (SDL_WindowSupportsGPUSwapchainComposition(
          app->gpu_device,
          app->window_handle,
          SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR
      )) {
    composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
  }

  if (!SDL_SetGPUSwapchainParameters(
          app->gpu_device, app->window_handle, composition, present_mode
      )) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }
  app->gpu_composition = (u32)composition;
  app->gpu_present_mode = (u32)present_mode;
  app->gpu_swapchain_format = (u32)SDL_GetGPUSwapchainTextureFormat(
      app->gpu_device, app->window_handle
  );

  if (!SDL_SetGPUAllowedFramesInFlight(app->gpu_device, 1)) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }

  if (!SDL_ShowWindow(app->window_handle)) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Init_Backend;
  }

  app->running = true;
  app->current_time_ns = SDL_GetTicksNS();
  app->last_time_ns = app->current_time_ns;
  SDL_GetMouseState(&app->mouse_position.x, &app->mouse_position.y);

  _app = app;
  if (info->init_resources_proc != nullptr) {
    _app->gpu_copy_commands = SDL_AcquireGPUCommandBuffer(_app->gpu_device);

    app_begin_copy_pass();
    bool32 init_resources_ok = info->init_resources_proc(info->user_data);
    app_end_copy_pass();

    if (!init_resources_ok) {
      SDL_CancelGPUCommandBuffer(_app->gpu_copy_commands);
      return App_Error_Failed_To_Init_User_Resources;
    }
    SDL_SubmitGPUCommandBuffer(_app->gpu_copy_commands);
    _app->gpu_copy_commands = nullptr;
  }

  commit();
  return App_Error_None;
}

void close_app(App *app) {
  delete_string(app->window_title, app->allocator);

  SDL_DestroyGPUDevice(app->gpu_device);
  SDL_DestroyWindow(app->window_handle);
}

bool32 app_update(App *app) {
  app->pen_samples_buffer_len = 0;
  app->pen.pressed = false;
  app->pen.position = (Vec2){0};
  app->pen.current_timestamp.some = false;
  app->pen.current_timestamp.value = 0;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      app->running = false;
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      app->mouse_scoll.x += event.wheel.x;
      app->mouse_scoll.y += event.wheel.y;
      break;
    case SDL_EVENT_PEN_DOWN: {
      u64 timestamp = event.ptouch.timestamp;
      if (app->pen.current_timestamp.some &&
          app->pen.current_timestamp.value != timestamp) {
        app_publish_pen_sample(app);
      }

      app->pen.current_timestamp.some = true;
      app->pen.current_timestamp.value = timestamp;
      app->pen.position = (Vec2){.x = event.ptouch.x, .y = event.ptouch.y};
      app->pen.pressed = true;
    } break;
    case SDL_EVENT_PEN_UP: {
      u64 timestamp = event.ptouch.timestamp;
      if (app->pen.current_timestamp.some &&
          app->pen.current_timestamp.value != timestamp) {
        app_publish_pen_sample(app);
      }

      app->pen.current_timestamp.some = true;
      app->pen.current_timestamp.value = timestamp;
      app->pen.position = (Vec2){.x = event.ptouch.x, .y = event.ptouch.y};
      app->pen.pressed = false;
    } break;
    case SDL_EVENT_PEN_MOTION: {
      u64 timestamp = event.pmotion.timestamp;
      if (app->pen.current_timestamp.some &&
          app->pen.current_timestamp.value != timestamp) {
        app_publish_pen_sample(app);
      }

      app->pen.current_timestamp.some = true;
      app->pen.current_timestamp.value = timestamp;
      app->pen.position = (Vec2){.x = event.ptouch.x, .y = event.ptouch.y};
      app->pen.pressed = (event.pmotion.pen_state & SDL_PEN_INPUT_DOWN) != 0;
    } break;
    case SDL_EVENT_PEN_AXIS: {
      u64 timestamp = event.paxis.timestamp;
      if (app->pen.current_timestamp.some &&
          app->pen.current_timestamp.value != timestamp) {
        app_publish_pen_sample(app);
      }

      app->pen.current_timestamp.some = true;
      app->pen.current_timestamp.value = timestamp;
      app->pen.position = (Vec2){.x = event.ptouch.x, .y = event.ptouch.y};
      app->pen.pressed = (event.paxis.pen_state & SDL_PEN_INPUT_DOWN) != 0;

      switch (event.paxis.axis) {
      case SDL_PEN_AXIS_PRESSURE:
        app->pen.pressure = event.paxis.value;
        break;
      case SDL_PEN_AXIS_XTILT:
      case SDL_PEN_AXIS_YTILT:
      case SDL_PEN_AXIS_ROTATION:
      case SDL_PEN_AXIS_DISTANCE:
      case SDL_PEN_AXIS_SLIDER:
      case SDL_PEN_AXIS_TANGENTIAL_PRESSURE:
      case SDL_PEN_AXIS_COUNT:
        break;
      }
    } break;
    }
  }

  // NOTE(nico): publish the last event possible
  if (app->pen.current_timestamp.some) {
    app_publish_pen_sample(app);
  }

  const bool *keys = SDL_GetKeyboardState(nullptr);
  for (usize i = 0; i < APP_KEYBOARD_KEY_CAP; i += 1) {
    app->keys[i].presses = app->keys[i].current;
    if (keys[i] == true) {
      app->keys[i].current = true;
    }
  }

  SDL_MouseButtonFlags mouse_buttons =
      SDL_GetMouseState(&app->mouse_position.x, &app->mouse_position.y);
  for (usize i = 1; i < APP_MOUSE_BUTTON_CAP; i += 1) {
    app->mouse[i].previous = app->mouse[i].current;
    app->mouse[i].current = (bool8)(mouse_buttons & SDL_BUTTON_MASK(i));
  }

  app->last_time_ns = app->current_time_ns;
  app->current_time_ns = SDL_GetTicksNS();

  f64 freq = (f64)SDL_GetPerformanceFrequency();
  app->elapsed_time =
      (f32)((f64)(app->current_time_ns - app->last_time_ns) / freq);

  return app->running;
}

App_Error app_begin_frame(App *app) {
  errdefer_scope;

  app->gpu_graphics_commands = SDL_AcquireGPUCommandBuffer(app->gpu_device);
  if (app->gpu_graphics_commands == nullptr) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Acquire_Render_Command_Buffer;
  }
  errdefer {
    SDL_CancelGPUCommandBuffer(app->gpu_graphics_commands);
    app->gpu_graphics_commands = nullptr;
  };

  app->gpu_copy_commands = SDL_AcquireGPUCommandBuffer(app->gpu_device);
  if (app->gpu_copy_commands == nullptr) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Acquire_Copy_Command_Buffer;
  }
  errdefer {
    SDL_CancelGPUCommandBuffer(app->gpu_copy_commands);
    app->gpu_copy_commands = nullptr;
  };

  bool32 swapchain_ok = SDL_WaitAndAcquireGPUSwapchainTexture(
      app->gpu_graphics_commands,
      app->window_handle,
      &app->gpu_swapchain,
      nullptr,
      nullptr
  );
  if (!swapchain_ok) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Acquire_Swapchain;
  }

  commit();
  return App_Error_None;
}

App_Error app_end_frame(App *app) {
  if (!SDL_SubmitGPUCommandBuffer(app->gpu_copy_commands)) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Submit_Copy_Command_Buffer;
  }

  if (!SDL_SubmitGPUCommandBuffer(app->gpu_graphics_commands)) {
    platform_log_error(&app->logger);
    return App_Error_Failed_To_Submit_Render_Command_Buffer;
  }

  app->gpu_graphics_commands = nullptr;
  app->gpu_copy_commands = nullptr;
  app->gpu_swapchain = nullptr;
  free_all(app->frame_allocator);
  return App_Error_None;
}

Allocator app_get_frame_allocator(void) {
  return _app->frame_allocator;
}

u64 app_get_current_time_ns() {
  return _app->current_time_ns;
}

u64 app_get_last_time_ns() {
  return _app->last_time_ns;
}

f32 app_get_total_time(void) {
  return (f32)((f64)SDL_GetTicksNS() * 1e-9);
}

f32 app_get_elapsed_time() {
  return (f32)_app->elapsed_time;
}

// String app_get_clipboard_content() {
//   const char *raw_str = glfwGetClipboardString(_app->window_handle);
//   return from_cstring(raw_str);
// }

Vec2 app_mouse_position() {
  return _app->mouse_position;
}

Vec2 app_mouse_delta() {
  return (Vec2){
    .x = _app->previous_mouse_position.x - _app->mouse_position.x,
    .y = _app->previous_mouse_position.y - _app->mouse_position.y,
  };
}

Vec2 app_mouse_scroll() {
  return _app->mouse_scoll;
}

bool8 app_mouse_pressed(Mouse_Button button) {
  return _app->mouse[button].current;
}

bool8 app_mouse_just_pressed(Mouse_Button button) {
  return _app->mouse[button].current && !_app->mouse[button].previous;
}

bool8 app_mouse_just_released(Mouse_Button button) {
  return !_app->mouse[button].current && _app->mouse[button].previous;
}

bool8 app_key_pressed(Keyboard_Key key) {
  return _app->keys[key].current;
}

u32 app_key_press_count(Keyboard_Key key) {
  return (u32)_app->keys[key].presses;
}

bool8 app_key_just_pressed(Keyboard_Key key) {
  return _app->keys[key].current && !_app->keys[key].previous;
}

Text_Array app_chars_pressed(void) {
  return (Text_Array){
    .items = _app->char_buffer,
    .len = _app->char_buffer_len,
  };
}

App_Pen_Sample_Buffer app_pen_samples(void) {
  return (App_Pen_Sample_Buffer){
    .items = _app->pen_samples_buffer,
    .len = _app->pen_samples_buffer_len,
  };
}

/////////////////////////////
// GPU Buffer management
/////////////////////////////
GPU_Streaming_Buffer_Create_Result
make_gpu_streaming_buffer(GPU_Streaming_Buffer_Create_Info *info) {
  static const SDL_GPUTransferBufferUsage usage_lookup[] = {
    [GPU_Streaming_Buffer_Kind_Write] = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
    [GPU_Streaming_Buffer_Kind_Read] = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
  };

  if (_app == nullptr || _app->gpu_device == nullptr) {
    return err(
        GPU_Streaming_Buffer_Create_Result, GPU_Error_Uninitialized_Backend
    );
  }

  GPU_Streaming_Buffer stream = {
    .kind = info->kind,
    .size = info->size,
  };

  stream.handle = SDL_CreateGPUTransferBuffer(
      _app->gpu_device,
      &(SDL_GPUTransferBufferCreateInfo){
        .size = (u32)info->size,
        .usage = usage_lookup[info->kind],
      }
  );
  if (stream.handle == nullptr) {
    return err(
        GPU_Streaming_Buffer_Create_Result, GPU_Error_Failed_To_Create_Stream
    );
  }

  return ok(GPU_Streaming_Buffer_Create_Result, stream);
}

GPU_Error destroy_gpu_streaming_buffer(GPU_Streaming_Buffer *stream) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  GPU_Error error = GPU_Error_None;

  if (stream->handle != nullptr) {
    SDL_ReleaseGPUTransferBuffer(_app->gpu_device, stream->handle);
    stream->handle = nullptr;
  } else {
    error = GPU_Error_Invalid_Streaming_Buffer;
  }

  return error;
}

static bool32
gpu_streaming_buffer_align_up(GPU_Streaming_Buffer *stream, usize align) {
  usize offset = stream->current_offset;
  if (align > 0) {
    usize remainder = offset % align;
    if (remainder > 0) {
      offset += align - remainder;
    }
  }

  if (offset < stream->size) {
    stream->current_offset = offset;
  }

  return offset < stream->size;
}

GPU_Buffer_Create_Result make_gpu_buffer(GPU_Buffer_Usage usage, usize size) {
  GPU_Buffer buffer = {
    .usage = usage,
    .size = size,
  };

  buffer.handle = SDL_CreateGPUBuffer(
      _app->gpu_device,
      &(SDL_GPUBufferCreateInfo){.usage = usage, .size = (u32)size}
  );

  if (buffer.handle == nullptr) {
    return err(GPU_Buffer_Create_Result, GPU_Error_Failed_To_Create_Buffer);
  }

  return ok(GPU_Buffer_Create_Result, buffer);
}

GPU_Error destroy_gpu_buffer(GPU_Buffer *buffer) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (buffer->handle == nullptr) {
    return GPU_Error_Invalid_Buffer;
  }

  SDL_ReleaseGPUBuffer(_app->gpu_device, buffer->handle);
  buffer->handle = nullptr;
  return GPU_Error_None;
}

static bool32 gpu_buffer_is_valid(GPU_Buffer *buffer) {
  return buffer != nullptr && buffer->handle != nullptr && buffer->size > 0;
}

static usize gpu_buffer_required_alignment(GPU_Buffer_Usage usage) {
  usize align = 0;
  if (usage & GPU_Buffer_Usage_Kind_Vertex ||
      usage & GPU_Buffer_Usage_Kind_Index ||
      usage & GPU_Buffer_Usage_Kind_Indirect) {
    align = 4;
  }

  if (usage & GPU_Buffer_Usage_Kind_Graphics_Read ||
      usage & GPU_Buffer_Usage_Kind_Compute_Read ||
      usage & GPU_Buffer_Usage_Kind_Compute_Write) {
    align = 16;
  }

  assert(align != 0);
  return align;
}

static GPU_Allocation_Result
gpu_arena_alloc(GPU_Allocator allocator, usize size) {
  if (!gpu_buffer_is_valid(allocator.buffer)) {
    // NOTE(nico): Not the most appropriate but this will do for now
    return err(GPU_Allocation_Result, Allocation_Error_Out_Of_Memory);
  }

  GPU_Buffer *buffer = allocator.buffer;
  GPU_Arena_Data *arena = (GPU_Arena_Data *)allocator.ptr;

  usize offset = arena->offset;
  if (allocator.align > 0) {
    usize remainder = arena->offset % allocator.align;
    if (remainder > 0) {
      offset += allocator.align - remainder;
    }
  }

  if (offset > buffer->size || size > buffer->size - offset) {
    return err(GPU_Allocation_Result, Allocation_Error_Out_Of_Memory);
  }

  GPU_Memory memory = {
    .buffer = buffer,
    .offset = offset,
    .size = size,
  };

  arena->offset = offset + size;

  return ok(GPU_Allocation_Result, memory);
}

static Allocation_Error
gpu_arena_free(GPU_Allocator allocator, GPU_Memory memory) {
  (void)allocator;
  (void)memory;
  return Allocation_Error_Op_Not_Implemented;
}

static Allocation_Error gpu_arena_free_all(GPU_Allocator allocator) {
  GPU_Arena_Data *arena = (GPU_Arena_Data *)allocator.ptr;
  arena->offset = 0;

  return Allocation_Error_None;
}

GPU_Allocator gpu_arena_allocator(GPU_Arena_Data *arena, GPU_Buffer *buffer) {
  return (GPU_Allocator){
    .buffer = buffer,
    .ptr = arena,
    .align =
        buffer != nullptr ? gpu_buffer_required_alignment(buffer->usage) : 0,
    .alloc_proc = gpu_arena_alloc,
    .free_proc = gpu_arena_free,
    .free_all_proc = gpu_arena_free_all,
  };
}

// Data manipulation operations
GPU_Error gpu_streaming_buffer_begin(GPU_Streaming_Buffer *stream) {
  if (stream == nullptr || stream->handle == nullptr) {
    return GPU_Error_Invalid_Streaming_Buffer;
  }

  stream->current_offset = 0;
  return GPU_Error_None;
}

GPU_Error gpu_streaming_buffer_end(GPU_Streaming_Buffer *stream) {
  if (stream == nullptr || stream->handle == nullptr) {
    return GPU_Error_Invalid_Streaming_Buffer;
  }

  stream->current_offset = 0;
  return GPU_Error_None;
}

// NOTE(nico): Currently missing
// - Offset into the cpu memory being passed
// - Offset into the gpu memory being passed
// Those two are just nice to have because:
// - For cpu memory, you can just do ptr + offset in the info
// - For gpu memory, you can recreate a memory struct. This is arguably the
// least appealing solution since some of the fields are internals
// NOTE(nico): cycle is off for now since having 1 frame-in-flight guarantees
// that there is no overlap. Will need to revisit once we have fancier streaming
// strategies
GPU_Error
gpu_memory_write(GPU_Streaming_Buffer *stream, GPU_Buffer_Write_Info *info) {
  if (_app == nullptr || _app->gpu_copy_pass == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (_app->gpu_copy_commands == nullptr) {
    return GPU_Error_Uninitialized_Copy_Command_Buffer;
  }

  if (stream->kind != GPU_Streaming_Buffer_Kind_Write) {
    return GPU_Error_Invalid_Streaming_Buffer;
  }

  if (stream->current_offset + info->size > stream->size ||
      info->size > info->memory->size) {
    return GPU_Error_Failed_To_Write_Memory;
  }

  // FIXME(nico): cycle false for now since the goal is to get the 2d rendering
  // up and running. Will be needed later on.
  // NOTE(nico): For uploading part of a resource - DO NOT use cycle.
  byte *ptr =
      (byte *)SDL_MapGPUTransferBuffer(_app->gpu_device, stream->handle, false);
  if (ptr == nullptr) {
    return GPU_Error_Failed_To_Write_Memory;
  }

  memcpy(ptr + stream->current_offset, info->data, info->size);
  SDL_UnmapGPUTransferBuffer(_app->gpu_device, stream->handle);

  SDL_UploadToGPUBuffer(
      _app->gpu_copy_pass,
      &(SDL_GPUTransferBufferLocation){
        .transfer_buffer = stream->handle,
        .offset = (u32)stream->current_offset,
      },
      &(SDL_GPUBufferRegion){
        .buffer = info->memory->buffer->handle,
        .size = (u32)info->size,
        .offset = (u32)info->memory->offset
      },
      false
  );

  stream->current_offset += info->size;
  return GPU_Error_None;
}

/////////////////////////////
// GPU Texture management
/////////////////////////////

GPU_Texture_Create_Result make_gpu_texture(GPU_Texture_Create_Info *info) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return err(GPU_Texture_Create_Result, GPU_Error_Uninitialized_Backend);
  }

  GPU_Texture texture = {
    .usage = info->usage,
    .format = info->format,
    .width = info->width,
    .height = info->height,
    .mip_level = 1,
    .byte_size = (usize)SDL_CalculateGPUTextureFormatSize(
        (SDL_GPUTextureFormat)info->format, info->width, info->height, 1
    ),
  };

  // NOTE(nico): we can pass it directly to SDL because our enum maps to theirs.
  // Need to be careful with that
  texture.handle = SDL_CreateGPUTexture(
      _app->gpu_device,
      &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = (SDL_GPUTextureFormat)info->format,
        .usage = info->usage,
        .width = info->width,
        .height = info->height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
      }
  );

  if (texture.handle == nullptr) {
    return err(GPU_Texture_Create_Result, GPU_Error_Failed_To_Create_Texture);
  }

  return ok(GPU_Texture_Create_Result, texture);
}

GPU_Error destroy_gpu_texture(GPU_Texture *texture) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (texture == nullptr || texture->handle == nullptr) {
    return GPU_Error_Invalid_Texture;
  }

  SDL_ReleaseGPUTexture(_app->gpu_device, texture->handle);
  texture->handle = nullptr;
  return GPU_Error_None;
}

GPU_Error
gpu_texture_write(GPU_Streaming_Buffer *stream, GPU_Texture_Write_Info *info) {
  if (_app == nullptr || _app->gpu_copy_pass == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (_app->gpu_copy_commands == nullptr) {
    return GPU_Error_Uninitialized_Copy_Command_Buffer;
  }

  if (stream == nullptr || stream->handle == nullptr) {
    return GPU_Error_Invalid_Streaming_Buffer;
  }

  if (info == nullptr || info->texture == nullptr ||
      info->texture->handle == nullptr) {
    return GPU_Error_Invalid_Texture;
  }

  GPU_Texture *texture = info->texture;
  // NOTE(nico): We only support full texture write for now. This will change
  // soon

  bool32 align_ok = gpu_streaming_buffer_align_up(
      stream,
      SDL_GPUTextureFormatTexelBlockSize((SDL_GPUTextureFormat)texture->format)
  );
  if (!align_ok || stream->current_offset + info->size > stream->size ||
      info->size != texture->byte_size) {
    return GPU_Error_Failed_To_Write_Texture;
  }

  byte *ptr =
      (byte *)SDL_MapGPUTransferBuffer(_app->gpu_device, stream->handle, false);
  if (ptr == nullptr) {
    return GPU_Error_Failed_To_Write_Memory;
  }

  memcpy(ptr + stream->current_offset, info->data, info->size);
  SDL_UnmapGPUTransferBuffer(_app->gpu_device, stream->handle);

  // NOTE(nico): Same disclaimer as for buffer writes. No cycle for now
  SDL_UploadToGPUTexture(
      _app->gpu_copy_pass,
      &(SDL_GPUTextureTransferInfo){
        .transfer_buffer = stream->handle,
        .offset = (u32)stream->current_offset,
        // NOTE(nico): Only tightly packed textures for now
        .pixels_per_row = 0,
        .rows_per_layer = 0,
      },
      &(SDL_GPUTextureRegion){
        .texture = texture->handle,
        .w = texture->width,
        .h = texture->height,
        // NOTE(nico): only support 2d textures
        .d = 1,
      },
      false
  );

  stream->current_offset += info->size;

  return GPU_Error_None;
}

GPU_Sampler_Create_Result make_gpu_sampler(GPU_Sampler_Create_Info *info) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return err(GPU_Sampler_Create_Result, GPU_Error_Uninitialized_Backend);
  }

  if (info == nullptr) {
    return err(GPU_Sampler_Create_Result, GPU_Error_Failed_To_Create_Sampler);
  }

  GPU_Sampler sampler = {
    .min_filter = info->min_filter,
    .mag_filter = info->mag_filter,
    .wrap_u = info->wrap_u,
    .wrap_v = info->wrap_v,
    .wrap_w = info->wrap_w,
  };
  sampler.handle = SDL_CreateGPUSampler(
      _app->gpu_device,
      &(SDL_GPUSamplerCreateInfo){
        .min_filter = (SDL_GPUFilter)info->min_filter,
        .mag_filter = (SDL_GPUFilter)info->mag_filter,
        .address_mode_u = (SDL_GPUSamplerAddressMode)info->wrap_u,
        .address_mode_v = (SDL_GPUSamplerAddressMode)info->wrap_v,
        .address_mode_w = (SDL_GPUSamplerAddressMode)info->wrap_w,

        // All the hardcoded crap for now
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .mip_lod_bias = 0,
        .min_lod = 0,
        .max_lod = 1,
        .max_anisotropy = 0,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
      }
  );

  if (sampler.handle == nullptr) {
    platform_log_error(&_app->logger);
    return err(GPU_Sampler_Create_Result, GPU_Error_Failed_To_Create_Sampler);
  }

  return ok(GPU_Sampler_Create_Result, sampler);
}

GPU_Error destroy_gpu_sampler(GPU_Sampler *sampler) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (sampler == nullptr || sampler->handle == nullptr) {
    return GPU_Error_Invalid_Sampler;
  }

  SDL_ReleaseGPUSampler(_app->gpu_device, sampler->handle);
  sampler->handle = nullptr;
  return GPU_Error_None;
}

////////////////////////////////
// Gpu pipeline
////////////////////////////////
static GPU_Pipeline_Create_Result
make_gpu_graphics_pipeline(GPU_Pipeline_Create_Info *info) {
  static const SDL_GPUVertexElementFormat vertex_format_lookup[] = {
    [GPU_Vertex_Attribute_Format_Vec2] = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
    [GPU_Vertex_Attribute_Format_Vec3] = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
    [GPU_Vertex_Attribute_Format_Vec4] = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
  };
  static const SDL_GPUColorTargetBlendState blend_mode_lookup[] = {
    [GPU_Blend_Mode_None] =
        {
          .enable_blend = false,
        },
    [GPU_Blend_Mode_Straight_Alpha] =
        {
          .enable_blend = true,
          .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
          .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .color_blend_op = SDL_GPU_BLENDOP_ADD,
          .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
          .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        },
    [GPU_Blend_Mode_Premultiplied_Alpha] = {
      .enable_blend = true,
      .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
      .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .color_blend_op = SDL_GPU_BLENDOP_ADD,
      .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
      .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
    },
  };

  if (info->graphics.targets.len == 0 &&
      info->graphics.depth_mode == GPU_Depth_Mode_None) {
    return err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline);
  }

  GPU_Graphics_Shader_Create_Info *vertex_info = &info->graphics.vertex;
  GPU_Graphics_Shader_Create_Info *fragment_info = &info->graphics.fragment;

  SDL_GPUShader *vert = SDL_CreateGPUShader(
      _app->gpu_device,
      &(SDL_GPUShaderCreateInfo){
        .code = (const byte *)vertex_info->source.data,
        .code_size = vertex_info->source.len,
        .entrypoint = vertex_info->entrypoint.data,
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_uniform_buffers = (u32)vertex_info->uniform_buffer_count,
        .num_storage_buffers = (u32)vertex_info->storage_buffer_count,
        .num_storage_textures = (u32)vertex_info->storage_texture_count,
        .num_samplers = (u32)vertex_info->sampler_count,
      }
  );
  defer {
    SDL_ReleaseGPUShader(_app->gpu_device, vert);
  };

  SDL_GPUShader *frag = SDL_CreateGPUShader(
      _app->gpu_device,
      &(SDL_GPUShaderCreateInfo){
        .code = (const byte *)fragment_info->source.data,
        .code_size = fragment_info->source.len,
        .entrypoint = fragment_info->entrypoint.data,
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_uniform_buffers = (u32)fragment_info->uniform_buffer_count,
        .num_storage_buffers = (u32)fragment_info->storage_buffer_count,
        .num_storage_textures = (u32)fragment_info->storage_texture_count,
        .num_samplers = (u32)fragment_info->sampler_count,
      }
  );
  defer {
    SDL_ReleaseGPUShader(_app->gpu_device, frag);
  };

  if (vert == nullptr || frag == nullptr) {
    return err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Shader);
  }

  u32 sdl_target_count = (u32)info->graphics.targets.len;
  SDL_GPUColorTargetDescription *sdl_targets = or_return(
      alloc(
          _app->frame_allocator,
          sizeof(SDL_GPUColorTargetDescription) * sdl_target_count
      ),
      err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline)
  );
  for (usize i = 0; i < sdl_target_count; i += 1) {
    // FIXME(nico): This is pretty static for now, can change on a per need
    // basis
    GPU_Blend_Mode blend = info->graphics.targets.items[i].blend;
    assert(blend >= 0 && blend < countof(blend_mode_lookup));

    sdl_targets[i] = (SDL_GPUColorTargetDescription){
      .format = (SDL_GPUTextureFormat)info->graphics.targets.items[i].format,
      .blend_state = blend_mode_lookup[blend],
    };
  }

  // TODO(nico): Change this when needed
  const u32 default_buffer_slot = 0;

  u32 sdl_attribute_count = (u32)info->graphics.attributes.len;
  SDL_GPUVertexAttribute *sdl_attributes = nullptr;
  if (sdl_attribute_count > 0) {
    sdl_attributes = or_return(
        alloc(
            _app->frame_allocator,
            sizeof(SDL_GPUVertexAttribute) * sdl_attribute_count
        ),
        err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline)
    );
    for (usize i = 0; i < sdl_attribute_count; i += 1) {
      GPU_Vertex_Attribute_Format format =
          info->graphics.attributes.items[i].format;
      if (format >= GPU_Vertex_Attribute_Format_MAX) {
        return err(
            GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline
        );
      }

      sdl_attributes[i] = (SDL_GPUVertexAttribute){
        .location = info->graphics.attributes.items[i].location,
        // NOTE(nico): fuck this shit for now. Only single buffer pipelines
        .buffer_slot = default_buffer_slot,
        .format =
            vertex_format_lookup[info->graphics.attributes.items[i].format],
        .offset = (u32)info->graphics.attributes.items[i].byte_offset,
      };
    }
  }

  GPU_Depth_Mode depth_mode = info->graphics.depth_mode;

  GPU_Pipeline pipeline = {
    .kind = GPU_Pipeline_Kind_Graphics,
    .graphics = {
      .handle = SDL_CreateGPUGraphicsPipeline(
          _app->gpu_device,
          &(SDL_GPUGraphicsPipelineCreateInfo){
            .vertex_shader = vert,
            .fragment_shader = frag,
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .vertex_input_state =
                {
                  .vertex_buffer_descriptions =
                      &(SDL_GPUVertexBufferDescription){
                        .slot = default_buffer_slot,
                        .pitch = (u32)info->graphics.vertex_byte_size,
                      },
                  .vertex_attributes = sdl_attributes,
                  .num_vertex_buffers = 1,
                  .num_vertex_attributes = sdl_attribute_count,
                },
            .target_info =
                {
                  .color_target_descriptions = sdl_targets,
                  .num_color_targets = sdl_target_count,
                  .has_depth_stencil_target = depth_mode != GPU_Depth_Mode_None,
                  .depth_stencil_format =
                      (SDL_GPUTextureFormat)GPU_Texture_Format_Depth24_Stencil8,
                },
            .rasterizer_state =
                {
                  .fill_mode = SDL_GPU_FILLMODE_FILL,
                  .cull_mode = SDL_GPU_CULLMODE_NONE,
                  .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
                  .enable_depth_clip = true,
                },
            .depth_stencil_state =
                {
                  .enable_depth_test = depth_mode != GPU_Depth_Mode_None,
                  .enable_depth_write = depth_mode == GPU_Depth_Mode_Read_Write,
                  .compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,
                },
          }
      ),
      .depth_mode = depth_mode,
    },
  };

  if (pipeline.graphics.handle == nullptr) {
    return err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline);
  }

  return ok(GPU_Pipeline_Create_Result, pipeline);
}

static GPU_Pipeline_Create_Result
make_gpu_compute_pipeline(GPU_Pipeline_Create_Info *info) {
  GPU_Compute_Shader_Create_Info *compute_info = &info->compute.shader;

  GPU_Pipeline pipeline = {
    .kind = GPU_Pipeline_Kind_Compute,
    .compute = {
      .handle = SDL_CreateGPUComputePipeline(
          _app->gpu_device,
          &(SDL_GPUComputePipelineCreateInfo){
            .code = (const byte *)compute_info->source.data,
            .code_size = compute_info->source.len,
            .entrypoint = compute_info->entrypoint.data,
            .format = SDL_GPU_SHADERFORMAT_SPIRV,

            .num_readonly_storage_textures =
                (u32)compute_info->storage_texture_read_only_count,
            .num_readonly_storage_buffers =
                (u32)compute_info->storage_buffer_read_only_count,
            .num_readwrite_storage_textures =
                (u32)compute_info->storage_texture_read_write_count,
            .num_readwrite_storage_buffers =
                (u32)compute_info->storage_buffer_read_write_count,
            .num_uniform_buffers = (u32)compute_info->uniform_buffer_count,
            .num_samplers = (u32)compute_info->sampler_count,

            .threadcount_x = (u32)compute_info->thread_count_x,
            .threadcount_y = (u32)compute_info->thread_count_y,
            .threadcount_z = (u32)compute_info->thread_count_z,
          }
      ),
    }
  };

  if (pipeline.compute.handle == nullptr) {
    return err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline);
  }

  return ok(GPU_Pipeline_Create_Result, pipeline);
}

GPU_Pipeline_Create_Result make_gpu_pipeline(GPU_Pipeline_Create_Info *info) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return err(GPU_Pipeline_Create_Result, GPU_Error_Uninitialized_Backend);
  }

  switch (info->kind) {
  case GPU_Pipeline_Kind_Graphics:
    return make_gpu_graphics_pipeline(info);
  case GPU_Pipeline_Kind_Compute:
    return make_gpu_compute_pipeline(info);
  default:
    return err(GPU_Pipeline_Create_Result, GPU_Error_Failed_To_Create_Pipeline);
  }
}

GPU_Error destroy_gpu_pipeline(GPU_Pipeline *pipeline) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (pipeline == nullptr) {
    return GPU_Error_Invalid_Pipeline;
  }

  switch (pipeline->kind) {
  case GPU_Pipeline_Kind_Graphics:
    if (pipeline->graphics.handle == nullptr) {
      return GPU_Error_Invalid_Pipeline;
    }
    SDL_ReleaseGPUGraphicsPipeline(_app->gpu_device, pipeline->graphics.handle);
    break;
  case GPU_Pipeline_Kind_Compute:
    if (pipeline->compute.handle == nullptr) {
      return GPU_Error_Invalid_Pipeline;
    }
    SDL_ReleaseGPUComputePipeline(_app->gpu_device, pipeline->compute.handle);
    break;
  default:
    assert(false);
    return GPU_Error_Invalid_Pipeline;
  }

  return GPU_Error_None;
}

////////////////////////////////
// Application tied operations
////////////////////////////////

App_Error app_begin_copy_pass(void) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return App_Error_Failed_To_Begin_Copy_Pass;
  }

  if (_app->gpu_copy_pass != nullptr) {
    return App_Error_Failed_To_Begin_Copy_Pass;
  }

  _app->gpu_copy_pass = SDL_BeginGPUCopyPass(_app->gpu_copy_commands);

  return _app->gpu_copy_pass == nullptr ? App_Error_Failed_To_Begin_Copy_Pass
                                        : App_Error_None;
}

App_Error app_end_copy_pass(void) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return App_Error_Failed_To_End_Copy_Pass;
  }

  if (_app->gpu_copy_pass == nullptr) {
    return App_Error_Failed_To_End_Copy_Pass;
  }

  SDL_EndGPUCopyPass(_app->gpu_copy_pass);
  _app->gpu_copy_pass = nullptr;

  return App_Error_None;
}

App_Error app_begin_render_pass(GPU_Render_Pass_Create_Info *info) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return App_Error_Failed_To_Begin_Render_Pass;
  }

  if (_app->gpu_render_pass != nullptr) {
    return App_Error_Failed_To_Begin_Render_Pass;
  }

  Arena_Transient_Memory mem = arena_begin_transient_memory(&_app->frame_arena);

  Allocation_Result sdl_color_targets_alloc = alloc(
      _app->frame_allocator,
      sizeof(SDL_GPUColorTargetInfo) * info->color_targets.len
  );
  if (!sdl_color_targets_alloc.ok) {
    return App_Error_Failed_To_Begin_Render_Pass;
  }

  SDL_GPUColorTargetInfo *sdl_color_targets =
      (SDL_GPUColorTargetInfo *)sdl_color_targets_alloc.value;
  for (usize i = 0; i < info->color_targets.len; i += 1) {
    GPU_Render_Pass_Color_Target *target =
        array_get_ptr(info->color_targets, i);
    sdl_color_targets[i] = (SDL_GPUColorTargetInfo){
      .texture = target->texture->handle,
      .load_op = (SDL_GPULoadOp)target->load_op,
      .store_op = (SDL_GPUStoreOp)target->store_op,
      .clear_color =
          {
            .r = target->clear_color.r,
            .g = target->clear_color.g,
            .b = target->clear_color.b,
            .a = target->clear_color.a,
          },
      // FIXME(nico): we should not hardcode it and it should be parameterizable
      .cycle = false
    };
  }

  SDL_GPUDepthStencilTargetInfo sdl_depth_target = {0};
  if (info->depth_target.some) {
    GPU_Render_Pass_Depth_Target *target = &info->depth_target.value;
    sdl_depth_target.texture = target->texture->handle;
    sdl_depth_target.clear_depth = target->clear_value;
    sdl_depth_target.load_op = (SDL_GPULoadOp)target->load_op;
    sdl_depth_target.store_op = (SDL_GPUStoreOp)target->store_op;
  }

  _app->gpu_render_pass = SDL_BeginGPURenderPass(
      _app->gpu_graphics_commands,
      sdl_color_targets,
      (u32)info->color_targets.len,
      info->depth_target.some ? &sdl_depth_target : nullptr
  );

  arena_end_transient_memory(mem);

  return _app->gpu_render_pass != nullptr
             ? App_Error_None
             : App_Error_Failed_To_Begin_Render_Pass;
}

App_Error app_end_render_pass(void) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return App_Error_Failed_To_End_Render_Pass;
  }

  if (_app->gpu_render_pass == nullptr) {
    return App_Error_Failed_To_End_Render_Pass;
  }

  SDL_EndGPURenderPass(_app->gpu_render_pass);
  _app->gpu_render_pass = nullptr;

  return App_Error_None;
}

App_Error app_begin_compute_pass(GPU_Compute_Pass_Create_Info *info) {
  if (_app == nullptr || _app->gpu_graphics_commands == nullptr) {
    return App_Error_Failed_To_Begin_Compute_Pass;
  }

  if (_app->gpu_compute_pass != nullptr) {
    return App_Error_Failed_To_Begin_Compute_Pass;
  }

  Arena_Transient_Memory mem = arena_begin_transient_memory(&_app->frame_arena);

  u32 sdl_buffer_binding_count = (u32)info->read_write_storage_buffers.len;
  SDL_GPUStorageBufferReadWriteBinding *sdl_buffer_bindings = or_return(
      alloc(
          _app->frame_allocator,
          sizeof(SDL_GPUStorageBufferReadWriteBinding) *
              sdl_buffer_binding_count
      ),
      App_Error_Failed_To_Begin_Compute_Pass
  );
  for (usize i = 0; i < sdl_buffer_binding_count; i += 1) {
    sdl_buffer_bindings[i] = (SDL_GPUStorageBufferReadWriteBinding){
      .buffer = info->read_write_storage_buffers.items[i]->handle
    };
  }

  u32 sdl_texture_binding_count = (u32)info->read_write_storage_textures.len;
  SDL_GPUStorageTextureReadWriteBinding *sdl_texture_bindings = or_return(
      alloc(
          _app->frame_allocator,
          sizeof(SDL_GPUStorageTextureReadWriteBinding) *
              sdl_texture_binding_count
      ),
      App_Error_Failed_To_Begin_Compute_Pass
  );
  for (usize i = 0; i < sdl_texture_binding_count; i += 1) {
    sdl_texture_bindings[i] = (SDL_GPUStorageTextureReadWriteBinding){
      .texture = info->read_write_storage_textures.items[i]->handle,
    };
  }

  _app->gpu_compute_pass = SDL_BeginGPUComputePass(
      _app->gpu_graphics_commands,
      sdl_texture_bindings,
      sdl_texture_binding_count,
      sdl_buffer_bindings,
      sdl_buffer_binding_count
  );

  arena_end_transient_memory(mem);

  return _app->gpu_compute_pass != nullptr
             ? App_Error_None
             : App_Error_Failed_To_Begin_Compute_Pass;
}

App_Error app_end_compute_pass(void) {
  if (_app == nullptr || _app->gpu_device == nullptr) {
    return App_Error_Failed_To_End_Render_Pass;
  }

  if (_app->gpu_compute_pass == nullptr) {
    return App_Error_Failed_To_End_Render_Pass;
  }

  SDL_EndGPUComputePass(_app->gpu_compute_pass);
  _app->gpu_compute_pass = nullptr;

  return App_Error_None;
}

GPU_Swapchain_Texture_Result app_get_swapchain_texture(void) {
  if (_app == nullptr || _app->gpu_swapchain == nullptr) {
    return err(GPU_Swapchain_Texture_Result, GPU_Error_Uninitialized_Backend);
  }

  // FIXME(nico): incomplete.. It should at least populate the dimensions
  return ok(
      GPU_Swapchain_Texture_Result,
      ((GPU_Texture){
        .handle = _app->gpu_swapchain,
        .format = (GPU_Texture_Format)_app->gpu_swapchain_format,
      })
  );
}

GPU_Swapchain_Format_Result app_get_swapchain_format(void) {
  if (_app == nullptr) {
    return err(GPU_Swapchain_Format_Result, GPU_Error_Uninitialized_Backend);
  }

  return ok(
      GPU_Swapchain_Format_Result,
      ((GPU_Texture_Format)_app->gpu_swapchain_format)
  );
}

GPU_Error app_bind_pipeline(GPU_Pipeline *pipeline) {
  if (_app == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  switch (pipeline->kind) {
  case GPU_Pipeline_Kind_Compute:
    if (_app->gpu_compute_pass == nullptr) {
      return GPU_Error_Uninitialized_Compute_Pass;
    }

    SDL_BindGPUComputePipeline(
        _app->gpu_compute_pass, pipeline->compute.handle
    );
    break;
  case GPU_Pipeline_Kind_Graphics:
    if (_app->gpu_render_pass == nullptr) {
      return GPU_Error_Uninitialized_Render_Pass;
    }

    SDL_BindGPUGraphicsPipeline(
        _app->gpu_render_pass, pipeline->graphics.handle
    );
    break;
  }
  return GPU_Error_None;
}

GPU_Error app_push_vertex_uniform(u32 slot, rawptr data, usize size) {
  if (_app == nullptr || _app->gpu_graphics_commands == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  SDL_PushGPUVertexUniformData(
      _app->gpu_graphics_commands, slot, data, (u32)size
  );
  return GPU_Error_None;
}

GPU_Error app_push_compute_uniform(u32 slot, rawptr data, usize size) {
  if (_app == nullptr || _app->gpu_graphics_commands == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  SDL_PushGPUComputeUniformData(
      _app->gpu_graphics_commands, slot, data, (u32)size
  );
  return GPU_Error_None;
}

GPU_Error app_bind_sampled_textures(GPU_Textures_Bind_Info *info) {
  if (_app == nullptr) {
    return GPU_Error_Uninitialized_Backend;
  }

  if ((info->target_pass == GPU_Pass_Kind_Compute &&
       _app->gpu_compute_pass == nullptr) ||
      (info->target_pass == GPU_Pass_Kind_Render &&
       _app->gpu_render_pass == nullptr)) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (info->textures.len != info->samplers.len) {
    return GPU_Failed_To_Bind_Sampled_Texture;
  }

  u32 count = (u32)info->textures.len;

  Arena_Transient_Memory mem = arena_begin_transient_memory(&_app->frame_arena);
  Allocation_Result sdl_bindings_alloc = alloc(
      _app->frame_allocator, sizeof(SDL_GPUTextureSamplerBinding) * count
  );
  if (!sdl_bindings_alloc.ok) {
    return GPU_Failed_To_Bind_Sampled_Texture;
  }

  SDL_GPUTextureSamplerBinding *sdl_bindings =
      (SDL_GPUTextureSamplerBinding *)sdl_bindings_alloc.value;
  for (usize i = 0; i < count; i += 1) {
    sdl_bindings[i] = (SDL_GPUTextureSamplerBinding){
      .texture = info->textures.items[i]->handle,
      .sampler = info->samplers.items[i]->handle,
    };
  }

  switch (info->target_pass) {
  case GPU_Pass_Kind_Compute:
    SDL_BindGPUComputeSamplers(
        _app->gpu_compute_pass, info->first_slot, sdl_bindings, count
    );
    break;
  case GPU_Pass_Kind_Render:
    SDL_BindGPUFragmentSamplers(
        _app->gpu_render_pass, info->first_slot, sdl_bindings, count
    );
    break;
  }

  arena_end_transient_memory(mem);
  return GPU_Error_None;
}

GPU_Error app_bind_storage_memory(GPU_Memory_Bind_Info *info) {
  if (_app == nullptr) {
    return GPU_Error_Uninitialized_Render_Pass;
  }

  if ((info->target_pass == GPU_Pass_Kind_Compute &&
       _app->gpu_compute_pass == nullptr) ||
      (info->target_pass == GPU_Pass_Kind_Render &&
       _app->gpu_render_pass == nullptr)) {
    return GPU_Error_Uninitialized_Backend;
  }

  GPU_Buffer *buffer = info->memory.buffer;
  if (buffer == nullptr || buffer->handle == nullptr) {
    return GPU_Error_Invalid_Buffer;
  }

  switch (info->target_pass) {
  case GPU_Pass_Kind_Compute:
    SDL_BindGPUComputeStorageBuffers(
        _app->gpu_compute_pass, info->slot, &buffer->handle, 1
    );
    break;
  case GPU_Pass_Kind_Render:
    SDL_BindGPUVertexStorageBuffers(
        _app->gpu_render_pass, info->slot, &buffer->handle, 1
    );
    break;
  }

  return GPU_Error_None;
}

GPU_Error
app_bind_storage_texture(GPU_Pass_Kind pass, GPU_Texture *texture, u32 slot) {
  if (_app == nullptr) {
    return GPU_Error_Uninitialized_Render_Pass;
  }

  if ((pass == GPU_Pass_Kind_Compute && _app->gpu_compute_pass == nullptr) ||
      (pass == GPU_Pass_Kind_Render && _app->gpu_render_pass == nullptr)) {
    return GPU_Error_Uninitialized_Backend;
  }

  if (texture == nullptr || texture->handle == nullptr) {
    return GPU_Error_Invalid_Texture;
  }

  // FIXME(nico): The slot index should be a parameter
  switch (pass) {
  case GPU_Pass_Kind_Compute:
    SDL_BindGPUComputeStorageTextures(
        _app->gpu_compute_pass, slot, &texture->handle, 1
    );
    break;
  case GPU_Pass_Kind_Render:
    SDL_BindGPUVertexStorageTextures(
        _app->gpu_render_pass, slot, &texture->handle, 1
    );
    break;
  }

  return GPU_Error_None;
}

GPU_Error app_draw_primitive(GPU_Primitive_Draw_Info *info) {
  if (_app == nullptr || _app->gpu_render_pass == nullptr) {
    return GPU_Error_Uninitialized_Render_Pass;
  }

  assert(info != nullptr);
  SDL_DrawGPUPrimitives(
      _app->gpu_render_pass, info->vertex_count, 1, info->first_vertex, 0
  );

  return GPU_Error_None;
}

GPU_Error app_dispatch_compute(GPU_Compute_Dispatch_Info *info) {
  if (_app == nullptr || _app->gpu_compute_pass == nullptr) {
    return GPU_Error_Uninitialized_Compute_Pass;
  }

  assert(info != nullptr);
  SDL_DispatchGPUCompute(
      _app->gpu_compute_pass,
      (u32)info->group_count_x,
      (u32)info->group_count_y,
      (u32)info->group_count_z
  );

  return GPU_Error_None;
}