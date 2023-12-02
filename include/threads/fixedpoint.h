// 고정 소수점 산술을 위한 것

#include <stdint.h>

typedef int32_t fixedpoint;

fixedpoint convert_itof(int);
fixedpoint convert_ftoi(fixedpoint); //음수 부분 버림
fixedpoint convert_ftoi_rounding(fixedpoint);

fixedpoint fp_add(fixedpoint, fixedpoint);
fixedpoint fp_subtract(fixedpoint, fixedpoint);
fixedpoint fp_add_complex(fixedpoint, int);
fixedpoint fp_subtract_complex(fixedpoint, int);
fixedpoint fp_multiply(fixedpoint,fixedpoint);
fixedpoint fp_multiply_complex(fixedpoint, int);
fixedpoint fp_divide(fixedpoint, fixedpoint);
fixedpoint fp_divide_complex(fixedpoint, int);