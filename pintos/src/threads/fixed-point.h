/*
 * fixed-point.h
 *
 *  Created on: Oct 14, 2015
 *      Author: chunyuch
 */
#ifndef SRC_THREADS_FIXED_POINT_H_
#define SRC_THREADS_FIXED_POINT_H_

#include <stdint.h>

/* Using p.q fixed-point represents to represent
 * a real number using 32 bit signed integer. */
#define INT_BIT 17        /* Integer part, where p=17. */
#define FRAC_BIT 14       /* Integer part, where q=14. */

typedef int32_t fp_real;
static const fp_real f = (1 << FRAC_BIT);        /* Support value for fix-point calculation. */

#define int_to_fp_real(INTEGER)\
  (INTEGER * f)

#define fp_real_to_int_zero(REAL)\
  (REAL / f)

#define fp_real_to_nearest_int(REAL)\
  ({fp_real bias = (REAL>0)? f/2 : -f/2; (REAL + bias) / f ;})

#define fp_real_add_fp_real(NUM1, NUM2)\
  (NUM1 + NUM2)

#define fp_real_sub_fp_real(NUM1, NUM2)\
  (NUM1 - NUM2)

#define fp_real_add_int(FP_NUM, INT_NUM)\
  (FP_NUM + INT_NUM * f)

#define fp_real_sub_int(FP_NUM, INT_NUM)\
  (FP_NUM - INT_NUM * f)

#define fp_real_mul_fp_real(NUM1, NUM2)\
  ((fp_real)((int64_t)NUM1 *  NUM2 / f))

#define fp_real_mul_int(FP_NUM, INT_NUM)\
  (FP_NUM * INT_NUM)

#define fp_real_div_fp_real(NUM1, NUM2)\
  ((fp_real)((int64_t)NUM1 *  f / NUM2))

#define fp_real_div_int(FP_NUM, INT_NUM)\
  (FP_NUM / INT_NUM)

#endif /* SRC_THREADS_FIXED_POINT_H_ */
