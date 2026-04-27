/**
 * @file    flip_core.h
 * @brief   FLIP 流体仿真核心数据结构与公开 API
 *
 * @details 基于 FLIP (Fluid-Implicit-Particle) 方法的二维流体仿真模块。
 *          本文件定义仿真配置常量、主结构体 FlipFluid 以及所有对外接口。
 */

#ifndef FLIP_CORE_H
#define FLIP_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 配置常量                                                                     */
/* -------------------------------------------------------------------------- */

/** @brief LED 网格输出的最大亮度值 */
#define LED_VAL_MAX_F   20.0f

/** @brief 密度场显示钳位上限 */
#define DENSITY_CLAMP_F 1.2f

/** @brief 密度→亮度的 Gamma 映射指数 */
#define GAMMA_F         0.6f

#ifndef MAX
/** @brief 返回两值中的较大者 */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
/** @brief 返回两值中的较小者 */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* -------------------------------------------------------------------------- */
/* 流体仿真结构体                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief FLIP 流体仿真主结构体
 *
 * 包含仿真网格、粒子系统和求解参数的完整状态。所有动态数组由 flip_create()
 * 统一分配，由 flip_destroy() 统一释放。
 */
typedef struct FlipFluid {
    float density;              /**< 背景密度系数 */

    /* ---- 网格参数 ---- */
    int f_num_x, f_num_y;       /**< 流体网格 x/y 方向格子数 */
    float h;                    /**< 网格单元边长 (m) */
    float f_inv_spacing;        /**< 网格间距倒数 (1/h) */
    int f_num_cells;            /**< 网格单元总数 */

    /* ---- 网格场量 ---- */
    float *u, *v;               /**< 速度场 (u=x, v=y) */
    float *du, *dv;             /**< 速度增量/临时场 */
    float *prev_u, *prev_v;     /**< 上一时间步的速度场 */
    float *p;                   /**< 压力场 */
    float *s;                   /**< 源项 / 右端项 */
    int32_t *cell_type;         /**< 单元类型标记 */

    /* ---- 粒子系统 ---- */
    int max_particles;          /**< 粒子容量上限 */
    int num_particles;          /**< 当前活跃粒子数 */
    float *particle_pos;        /**< 粒子位置数组 (交错 x,y) */
    float *particle_vel;        /**< 粒子速度数组 (交错 u,v) */
    float *particle_density;    /**< 每个粒子的密度值 */
    float particle_rest_density;/**< 粒子静止密度 */

    /* ---- 粒子空间哈希 ---- */
    float particle_radius;      /**< 粒子半径 (m) */
    float p_inv_spacing;        /**< 粒子网格间距倒数 */
    int p_num_x, p_num_y;       /**< 粒子网格 x/y 方向格子数 */
    int p_num_cells;            /**< 粒子网格单元总数 */
    int32_t *num_cell_particles;   /**< 每格粒子数 */
    int32_t *first_cell_particle;  /**< 每格首粒子索引 */
    int32_t *cell_particle_ids;    /**< 粒子排序 ID 数组 */

    /* ---- 单元类型枚举 ---- */
    int AIR_CELL;               /**< 空气单元 (0) */
    int FLUID_CELL;             /**< 流体单元 (1) */
    int SOLID_CELL;             /**< 固体/边界单元 (2) */

    /* ---- 求解参数 ---- */
    float gravity_scale;        /**< 重力强度倍率 */
    int push_iters;             /**< 速度外推迭代次数 */
    int pressure_iters;         /**< 压力投影迭代次数 */
    float flip_ratio;           /**< FLIP/PIC 混合比率 (0=纯PIC, 1=纯FLIP) */
} FlipFluid;

/* -------------------------------------------------------------------------- */
/* 内联工具函数                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief 将整数 x 钳制到 [lo, hi] 区间
 */
static inline int clamp_i(int x, int lo, int hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

/**
 * @brief 将浮点数 x 钳制到 [lo, hi] 区间
 */
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

/**
 * @brief 创建并初始化 FLIP 流体仿真实例
 *
 * @param sim_w        仿真域宽度 (m)
 * @param sim_h        仿真域高度 (m)
 * @param visible_res  可见网格分辨率 (格/米)
 * @param fill_ratio   初始流体填充比例 (0~1)
 * @return 成功返回 FlipFluid 指针，失败返回 NULL
 */
FlipFluid *flip_create(float sim_w, float sim_h, int visible_res,
                       float fill_ratio);

/**
 * @brief 销毁 FLIP 流体仿真实例，释放所有资源
 *
 * @param f 由 flip_create() 创建的实例指针
 */
void flip_destroy(FlipFluid *f);

/**
 * @brief 执行一个仿真时间步
 *
 * @param f   仿真实例
 * @param dt  时间步长 (s)
 * @param gx  重力 x 分量 (m/s²)
 * @param gy  重力 y 分量 (m/s²)
 */
void flip_step(FlipFluid *f, float dt, float gx, float gy);

/**
 * @brief 设置重力强度倍率
 *
 * @param f             仿真实例
 * @param gravity_scale 重力倍率 (默认 1.0)
 */
void flip_set_gravity_scale(FlipFluid *f, float gravity_scale);

/**
 * @brief 设置求解器质量参数
 *
 * @param f              仿真实例
 * @param push_iters     速度外推迭代次数
 * @param pressure_iters 压力投影迭代次数
 * @param flip_ratio     FLIP/PIC 混合比率 (0=纯PIC, 1=纯FLIP)
 */
void flip_set_solver_quality(FlipFluid *f, int push_iters,
                             int pressure_iters, float flip_ratio);

/**
 * @brief 提取密度场并映射为 LED 网格亮度值
 *
 * @param f        仿真实例
 * @param out_grid 输出缓冲区，长度 = 网格分辨率²，值范围 [0, LED_VAL_MAX_F]
 */
void flip_get_led_grid(const FlipFluid *f, float *out_grid);

#ifdef __cplusplus
}
#endif

#endif /* FLIP_CORE_H */
