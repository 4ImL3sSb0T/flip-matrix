#ifndef FLIP_CORE_H
#define FLIP_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 配置常量                                                                     */
/* -------------------------------------------------------------------------- */

#define LED_VAL_MAX_F   20.0f
#define DENSITY_CLAMP_F 1.2f
#define GAMMA_F         0.6f

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* -------------------------------------------------------------------------- */
/* 流体仿真结构体                                                                */
/* -------------------------------------------------------------------------- */

typedef struct FlipFluid {
    float density;
    int f_num_x, f_num_y;
    float h;
    float f_inv_spacing;
    int f_num_cells;

    float *u, *v, *du, *dv, *prev_u, *prev_v, *p, *s;
    int32_t* cell_type;

    int max_particles;
    int num_particles;
    float* particle_pos;
    float* particle_vel;
    float* particle_density;
    float particle_rest_density;

    float particle_radius;
    float p_inv_spacing;
    int p_num_x, p_num_y, p_num_cells;
    int32_t* num_cell_particles;
    int32_t* first_cell_particle;
    int32_t* cell_particle_ids;

    int AIR_CELL, FLUID_CELL, SOLID_CELL;

    float gravity_scale;
    int push_iters;
    int pressure_iters;
    float flip_ratio;
} FlipFluid;

/* -------------------------------------------------------------------------- */
/* 内联工具函数                                                                  */
/* -------------------------------------------------------------------------- */

static inline int clamp_i(int x, int lo, int hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static inline float clamp_f(float x, float lo, float hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

/* -------------------------------------------------------------------------- */
/* 公开 API                                                                     */
/* -------------------------------------------------------------------------- */

FlipFluid* flip_create(float sim_w, float sim_h, int visible_res,
                        float fill_ratio);

void flip_destroy(FlipFluid* f);

void flip_step(FlipFluid* f, float dt, float gx, float gy);

void flip_set_gravity_scale(FlipFluid* f, float gravity_scale);

void flip_set_solver_quality(FlipFluid* f, int push_iters, int pressure_iters,
                              float flip_ratio);

void flip_get_led_grid(const FlipFluid* f, float* out_grid);

#ifdef __cplusplus
}
#endif

#endif /* FLIP_CORE_H */
