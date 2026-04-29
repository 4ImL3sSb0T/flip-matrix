#ifndef __MATRIX_H_
#define __MATRIX_H_

#include "service/tools/common_def.h"

typedef enum {
  MATRIX_TOPO_PROGRESSIVE = 0,
  MATRIX_TOPO_SNAKE      = 1
} matrix_topo_t;

typedef struct {
  uint32_t rows;
  uint32_t cols;
  matrix_topo_t topology;
} matrix_config_t;

exit_code_t matrix_init(const matrix_config_t *config);
exit_code_t matrix_deinit(void);

exit_code_t matrix_write_async(void);

exit_code_t matrix_write_buffer(const uint32_t *data, uint32_t len);
exit_code_t matrix_set_pixel(uint32_t row, uint32_t col, uint32_t rgb);
exit_code_t matrix_fill(uint32_t rgb);
exit_code_t matrix_clear(void);

uint32_t matrix_pixel_count(void);
uint32_t matrix_rows(void);
uint32_t matrix_cols(void);

#endif
