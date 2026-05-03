#include "matrix.h"
#include "service/cli/shell.h"
#include "service/cli/log/log.h"
#include <stdlib.h>

int mtrx_init(int argc, char *argv[])
{
  if (argc < 3) {
    logWarning("usage: mtrx_init <rows> <cols> [topo=0|1]");
    return -1;
  }

  matrix_config_t cfg = {
    .rows = (uint32_t)atoi(argv[1]),
    .cols = (uint32_t)atoi(argv[2]),
    .topology = (argc > 3) ? (matrix_topo_t)atoi(argv[3]) : MATRIX_TOPO_PROGRESSIVE,
  };

  exit_code_t ret = matrix_init(&cfg);
  if (ret != EXIT_OK) {
    logError("init failed: %d", ret);
    return (int)ret;
  }
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_init, mtrx_init, init matrix\r\nmtrx_init <rows> <cols> [topo]);


int mtrx_deinit(int argc, char *argv[])
{
  (void)argc; (void)argv;
  exit_code_t ret = matrix_deinit();
  if (ret != EXIT_OK) {
    logError("deinit failed: %d", ret);
    return (int)ret;
  }
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_deinit, mtrx_deinit, deinit matrix);


int mtrx_set(int argc, char *argv[])
{
  if (argc < 6) {
    logWarning("usage: mtrx_set <row> <col> <r> <g> <b>");
    return -1;
  }

  uint32_t row = (uint32_t)atoi(argv[1]);
  uint32_t col = (uint32_t)atoi(argv[2]);
  uint8_t r = (uint8_t)atoi(argv[3]);
  uint8_t g = (uint8_t)atoi(argv[4]);
  uint8_t b = (uint8_t)atoi(argv[5]);

  exit_code_t ret = matrix_set_pixel(row, col, matrix_rgb(r, g, b));
  if (ret != EXIT_OK) {
    logError("set_pixel(%lu,%lu) failed: %d", row, col, ret);
    return (int)ret;
  }
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_set, mtrx_set, set pixel color\r\nmtrx_set <row> <col> <r> <g> <b>);


int mtrx_fill(int argc, char *argv[])
{
  if (argc < 4) {
    logWarning("usage: mtrx_fill <r> <g> <b>");
    return -1;
  }

  uint8_t r = (uint8_t)atoi(argv[1]);
  uint8_t g = (uint8_t)atoi(argv[2]);
  uint8_t b = (uint8_t)atoi(argv[3]);

  exit_code_t ret = matrix_fill(matrix_rgb(r, g, b));
  if (ret != EXIT_OK) {
    logError("fill failed: %d", ret);
    return (int)ret;
  }
  logInfo("fill #%02X%02X%02X", r, g, b);
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_fill, mtrx_fill, fill matrix with color\r\nmtrx_fill <r> <g> <b>);


int mtrx_clear(int argc, char *argv[])
{
  (void)argc; (void)argv;
  exit_code_t ret = matrix_clear();
  if (ret != EXIT_OK) {
    logError("clear failed: %d", ret);
    return (int)ret;
  }
  logInfo("matrix cleared");
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_clear, mtrx_clear, clear all pixels);


int mtrx_show(int argc, char *argv[])
{
  (void)argc; (void)argv;
  exit_code_t ret = matrix_write_async();
  if (ret != EXIT_OK) {
    logError("show failed: %d", ret);
    return (int)ret;
  }
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_show, mtrx_show, flush buffer to LEDs);


int mtrx_info(int argc, char *argv[])
{
  (void)argc; (void)argv;
  logInfo("rows=%lu cols=%lu pixels=%lu",
          matrix_rows(), matrix_cols(), matrix_pixel_count());
  return 0;
}
SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
mtrx_info, mtrx_info, show matrix info);
