#version 450

#include <RendererUniforms.h>

in vec4 positions;

#if !defined(normals_MISSING)
in vec4 normals;
#endif
#if !defined(texcoords_MISSING)
in vec2 texcoords;
#endif

out vec4 v_position;
out vec4 v_normal;
out vec2 v_texcoord;

#if !defined(pointsizes_MISSING)
in float pointsizes;
#endif
#if !defined(user0_MISSING)
in vec4 user0;
out vec4 v_user0;
#endif
#if !defined(user1_MISSING)
in vec4 user1;
out vec4 v_user1;
#endif
#if !defined(user2_MISSING)
in vec4 user2;
out vec4 v_user2;
#endif

//@side_effects
void main_vert(bool view_transform, mat4 model_matrix) {
  vec4 position = model_matrix * positions;
  v_position = position;

#if !defined(normals_MISSING)
  v_normal = model_matrix * normals;
#else
  v_normal = vec4(0,0,1,0);
#endif

#if !defined(texcoords_MISSING)
  v_texcoord = texcoords;
#else
  v_texcoord = vec2(0,0);
#endif

  if (view_transform)
    position = u_clip_matrix * u_projection_matrix * u_view_matrix * position;

  if (u_use_clip_planes) {
    gl_ClipDistance[0] = dot(position, u_clip_planes[0]);
    gl_ClipDistance[1] = dot(position, u_clip_planes[1]);
    gl_ClipDistance[2] = dot(position, u_clip_planes[2]);
    gl_ClipDistance[3] = dot(position, u_clip_planes[3]);
  }

  gl_Position = position;

#if !defined(pointsizes_MISSING)
  gl_PointSize = pointsizes;
#endif
#if !defined(user0_MISSING)
  v_user0 = user0;
#endif
#if !defined(user1_MISSING)
  v_user1 = user1;
#endif
#if !defined(user2_MISSING)
  v_user2 = user2;
#endif
}
