#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

typedef int fixed;

#define FIXED_COEF 16384
#define NEAR_COEF 8192

#define FIXED_CONVERT(n) n * FIXED_COEF 
#define INT_CONVERT(x) x / FIXED_COEF
#define NEAR_CONVERT(in) in >= 0 ? (in + NEAR_COEF) / FIXED_COEF \
                                 : (in - NEAR_COEF) / FIXED_COEF;
#define FIXED_ADD(x, y) x + y
#define FIXED_SUB(x, y) x - y
#define INT_ADD(x, n) x + (n*FIXED_COEF)
#define INT_SUB(x, n) x - (n*FIXED_COEF)
#define FIXED_MUL(x, y) ((int64_t) x) * y / FIXED_COEF
#define FIXED_DIV(x, y) ((int64_t) x ) * FIXED_COEF / y
#define INT_MUL(x, n) x * n
#define INT_DIV(x, n) x / n

#endif /* threads/fixed_point.h */
