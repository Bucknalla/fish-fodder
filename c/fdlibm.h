// Deterministic maths for fishdraw: fdlibm's sin, cos, atan, atan2, acos,
// exp and pow (plus V8's hypot), identical on every platform (see fdlibm.c).
#ifndef FDLIBM_H
#define FDLIBM_H

double fd_sin(double x);
double fd_cos(double x);
double fd_atan(double x);
double fd_atan2(double y, double x);
double fd_acos(double x);
double fd_exp(double x);
double fd_pow(double x, double y);

// Math.hypot(a, b) exactly as V8 computes it.
double fd_hypot(double a, double b);

#endif
