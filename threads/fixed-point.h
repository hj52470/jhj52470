#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* Fixed-Point Real Number Representation: 17.14 format.
 * F = 2^14.
 *
 * fixed_t is a 32-bit integer.
 * 1 bit for sign, 17 bits for integer part (p), 14 bits for fractional part (q).
 */
typedef int32_t fixed_t;

/* Q = 14 (Number of bits in the fractional part) */
#define Q 14

/* F = 2^Q (Fixed-Point constant, 16384) */
#define F (1 << Q)

/* ----------------------------------------------------------------------
 * A. Integer <-> Fixed-Point Conversion Macros
 * ---------------------------------------------------------------------- */

/* Convert integer N to Fixed-Point (N * F) */
#define INT_TO_FIXED(N) ((fixed_t)(N) * F)

/* Convert Fixed-Point X to integer (Round to zero) (X / F) */
#define FIXED_TO_INT(X) ((X) / F)

/* Convert Fixed-Point X to the nearest integer (Round to nearest) */
#define FIXED_TO_INT_ROUND(X) (((X) >= 0) ? (((X) + F / 2) / F) : (((X) - F / 2) / F))

/* ----------------------------------------------------------------------
 * B. Fixed-Point Arithmetic Operations
 * ---------------------------------------------------------------------- */

/* Fixed-Point X + Y */
#define FIXED_ADD(X, Y) ((X) + (Y))

/* Fixed-Point X - Y */
#define FIXED_SUB(X, Y) ((X) - (Y))

/* Fixed-Point X + integer N (X + N*F) */
#define FIXED_ADD_INT(X, N) ((X) + INT_TO_FIXED(N))

/* Fixed-Point X - integer N (X - N*F) */
#define FIXED_SUB_INT(X, N) ((X) - INT_TO_FIXED(N))

/* Fixed-Point X * Y (X * Y / F). Uses 64-bit to prevent overflow. */
#define FIXED_MUL(X, Y) ((fixed_t)(((int64_t)(X)) * (Y) / F))

/* Fixed-Point X * integer N (X * N) */
#define FIXED_MUL_INT(X, N) ((X) * (N))

/* Fixed-Point X / Y (X * F / Y). Uses 64-bit for precision. */
#define FIXED_DIV(X, Y) ((fixed_t)(((int64_t)(X)) * F / (Y)))

/* Fixed-Point X / integer N (X / N) */
#define FIXED_DIV_INT(X, N) ((X) / (N))


#endif /* threads/fixed-point.h */
