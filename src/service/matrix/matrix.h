#ifndef __MATRIX_H_
#define __MATRIX_H_

#include "service/tools/common_def.h"

typedef struct
{
    uint32_t rows;
    uint32_t cols;
} matrix_t;

exit_code_t matrix_init(matrix_t* matrix);

exit_code_t matrix_deinit(void);

exit_code_t matrix_write_async();

exit_code_t matrix_get_buffer(matrix_t* matrix);

exit_code_t matrix_start(uint32_t refresh_rate_hz);

#endif
