#include "core/camera.h"

// static Vec3 rotate_around_pivot_y(Vec3 point, Vec3 pivot, f32 angle_deg) {
//   f32 angle = to_radians_f32(angle_deg);
//   f32 _cos = cosf(angle);
//   f32 _sin = sinf(angle);

//   f32 ox = point.x - pivot.x;
//   f32 oz = point.z - pivot.z;

//   f32 rx = ox * _cos - oz * _sin;
//   f32 rz = ox * _sin + oz * _cos;

//   return vec3(pivot.x + rx, point.y, pivot.z + rz);
// }

Ray ray_from_screen(Raw_Camera *camera, Vec2 screen_pos) {
  f32 ndc_x = (2.f * screen_pos.x / camera->frame_width) - 1.f;
  f32 ndc_y = 1.f - (2.f * screen_pos.y / camera->frame_height);

  Mat4 inv_proj_view = mat4_inverse(camera->mat_proj_view);

  // Clip Z in [0, 1] (WebGPU convention): 0 = near plane, 1 = far plane
  Vec4 near_h = mat4_mul_vec4(inv_proj_view, vec4(ndc_x, ndc_y, 0.f, 1.f));
  Vec4 far_h = mat4_mul_vec4(inv_proj_view, vec4(ndc_x, ndc_y, 1.f, 1.f));

  Vec3 near_p = vec3_scale(vec3(near_h.x, near_h.y, near_h.z), 1.f / near_h.w);
  Vec3 far_p = vec3_scale(vec3(far_h.x, far_h.y, far_h.z), 1.f / far_h.w);

  Vec3 span = vec3_sub(far_p, near_p);
  f32 len = vec3_length(span);

  return (Ray){
    .origin = near_p,
    .dir = vec3_scale(span, 1.f / len),
    .length = len,
  };
}

void update_raw_camera(Raw_Camera *camera, f32 frame_w, f32 frame_h) {
  camera->frame_width = frame_w;
  camera->frame_height = frame_h;

  camera->mat_proj = mat4_persp(
      to_radians_f32(camera->fovy),
      frame_w / frame_h,
      camera->z_near,
      camera->z_far
  );
  camera->mat_view = mat4_look_at(camera->position, camera->target, camera->up);
  camera->mat_proj_view = mat4_mul(camera->mat_proj, camera->mat_view);
}

Raw_Camera lerp_raw_cameras(Raw_Camera *a, Raw_Camera *b, f32 t) {
  return (Raw_Camera){
    .position = vec3_lerp(a->position, b->position, t),
    .target = vec3_lerp(a->target, b->target, t),
    .up = vec3_normalize(vec3_lerp(a->up, b->up, t)),
    .fovy = lerp_f32(a->fovy, b->fovy, t),
    .z_near = b->z_near,
    .z_far = b->z_far,
  };
}