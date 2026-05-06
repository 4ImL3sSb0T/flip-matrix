#include "app_water_sim.h"
#include "service/cli/shell.h"
#include "service/cli/log/log.h"
#include <stdlib.h>

int wsim_gravity(int argc, char *argv[])
{
    if (argc < 2) {
        logWarning("usage: wsim_gravity <scale>");
        return -1;
    }
    float scale = (float)atof(argv[1]);
    app_water_sim_set_gravity(scale);
    logInfo("gravity_scale = %.2f", scale);
    return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
wsim_gravity, wsim_gravity, set gravity scale\r\nwsim_gravity <scale>);

int wsim_solver(int argc, char *argv[])
{
    if (argc < 4) {
        logWarning("usage: wsim_solver <push_iters> <pressure_iters> <flip_ratio>");
        return -1;
    }
    int push = atoi(argv[1]);
    int pressure = atoi(argv[2]);
    float flip = (float)atof(argv[3]);
    app_water_sim_set_solver(push, pressure, flip);
    logInfo("solver: push=%d pressure=%d flip=%.2f", push, pressure, flip);
    return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
wsim_solver, wsim_solver, set solver quality\r\nwsim_solver <push> <pressure> <flip_ratio>);

int wsim_color(int argc, char *argv[])
{
    if (argc < 2) {
        logWarning("usage: wsim_color <0|1|2>");
        logWarning("  0=blue gradient  1=rainbow  2=grayscale");
        return -1;
    }
    int scheme = atoi(argv[1]);
    app_water_sim_set_color_scheme(scheme);
    static const char *names[] = {"blue gradient", "rainbow", "grayscale"};
    if (scheme >= 0 && scheme <= 2) {
        logInfo("color: %s", names[scheme]);
    } else {
        logWarning("invalid scheme %d, use 0-2", scheme);
    }
    return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
wsim_color, wsim_color, set color scheme\r\nwsim_color <0=blue|1=rainbow|2=gray>);

int wsim_dt(int argc, char *argv[])
{
    if (argc < 2) {
        logWarning("usage: wsim_dt <seconds>");
        return -1;
    }
    float dt = (float)atof(argv[1]);
    app_water_sim_set_dt(dt);
    logInfo("dt = %.4f s (%.0f Hz)", dt, 1.0f / dt);
    return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
wsim_dt, wsim_dt, set simulation timestep\r\nwsim_dt <seconds>);

int wsim_status(int argc, char *argv[])
{
    (void)argc; (void)argv;
    app_water_sim_status();
    return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
wsim_status, wsim_status, show water sim parameters);
