#include <stdint.h>
#define F (1<<14)    //fixed point 1
#define INT_MAX ((1<<31)-1)
#define INT_MIN (-(1<<31))
// x and y denote fixed_point numbers in 17.14 format
// n is an integer

typedef int32_t fixedpoint;

fixedpoint convert_itof(int);       // integer를 fixed point로 전환
fixedpoint convert_ftoi(fixedpoint);    // fp를 int로 전환 / 음수 부분 버림
fixedpoint convert_ftoi_rounding(fixedpoint);   // fp를 int로 전환(반올림)

fixedpoint fp_add(fixedpoint, fixedpoint);      // fp의 덧셈
fixedpoint fp_subtract(fixedpoint, fixedpoint); // fp의 뺄셈
fixedpoint fp_add_complex(int, fixedpoint);     // fp와 int의 덧셈
fixedpoint fp_subtract_complex(int, fixedpoint);    // fp와 int의 뺄셈(x-n)     
fixedpoint fp_multiply(fixedpoint,fixedpoint);  // fp의 곱셈
fixedpoint fp_multiply_complex(int, fixedpoint);    // fp와 int의 곱셈
fixedpoint fp_divide(fixedpoint, fixedpoint);   // fp의 나눗셈
fixedpoint fp_divide_complex(int, fixedpoint);  // fp와 int 나눗셈