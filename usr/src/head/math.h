/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 */

#ifndef _MATH_H
#define	_MATH_H
/*
 * IL-15209: Temporary sentinel used by iso/math_iso.h and iso/math_c99.h
 * to detect whether this (updated) math.h is in the include chain.
 * Remove once all supported build hosts carry the updated headers.
 * Without this, g++ compiles of end up with old math.h and new iso
 * headers that don't work together.
 */
#define	_ILLUMOS_MATH_H_2026_04	1

#include <iso/math_iso.h>
#include <iso/math_c99.h>

#if __cplusplus >= 199711L
using std::abs;
using std::acos;
using std::asin;
using std::atan2;
using std::atan;
using std::ceil;
using std::cos;
using std::cosh;
using std::exp;
using std::fabs;
using std::floor;
using std::fmod;
using std::frexp;
using std::ldexp;
using std::log10;
using std::log;
using std::modf;
using std::pow;
using std::sin;
using std::sinh;
using std::sqrt;
using std::tan;
using std::tanh;

#if defined(__EXTENSIONS__) || defined(_STDC_C99) || \
    (!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
    defined(__C99FEATURES__)

using std::acosh;
using std::acoshf;
using std::acoshl;
using std::acosf;
using std::acosl;
using std::asinhf;
using std::asinhl;
using std::asinh;
using std::asinf;
using std::asinl;
using std::atan2f;
using std::atan2l;
using std::atanf;
using std::atanl;
using std::atanhf;
using std::atanhl;
using std::atanh;
using std::cbrt;
using std::cbrtf;
using std::cbrtl;
using std::ceilf;
using std::ceill;
using std::copysign;
using std::copysignf;
using std::copysignl;
using std::cosf;
using std::cosl;
using std::coshf;
using std::coshl;
using std::erf;
using std::erff;
using std::erfl;
using std::erfc;
using std::erfcf;
using std::erfcl;
using std::exp2;
using std::exp2f;
using std::exp2l;
using std::expf;
using std::expl;
using std::expm1;
using std::expm1f;
using std::expm1l;
using std::fabsf;
using std::fabsl;
using std::fdim;
using std::fdimf;
using std::fdiml;
using std::floorf;
using std::floorl;
using std::fma;
using std::fmaf;
using std::fmal;
using std::fmax;
using std::fmaxf;
using std::fmaxl;
using std::fmin;
using std::fminf;
using std::fminl;
using std::fmodf;
using std::fmodl;
using std::frexpf;
using std::frexpl;
using std::hypot;
using std::hypotf;
using std::hypotl;
using std::ilogb;
using std::ilogbf;
using std::ilogbl;
using std::ldexpf;
using std::ldexpl;
using std::lgamma;
using std::lgammaf;
using std::lgammal;
using std::log10f;
using std::log10l;
using std::log1p;
using std::log1pf;
using std::log1pl;
using std::log2;
using std::log2f;
using std::log2l;
using std::logb;
using std::logbf;
using std::logbl;
using std::logf;
using std::logl;
using std::lrint;
using std::lrintf;
using std::lrintl;
using std::lround;
using std::lroundf;
using std::lroundl;
using std::modff;
using std::modfl;
using std::nan;
using std::nanf;
using std::nanl;
using std::nearbyint;
using std::nearbyintf;
using std::nearbyintl;
using std::nextafter;
using std::nextafterf;
using std::nextafterl;
using std::nexttoward;
using std::nexttowardf;
using std::nexttowardl;
using std::powf;
using std::powl;
using std::remainder;
using std::remainderf;
using std::remainderl;
using std::remquo;
using std::remquof;
using std::remquol;
using std::rint;
using std::rintf;
using std::rintl;
using std::round;
using std::roundf;
using std::roundl;
using std::scalbln;
using std::scalblnf;
using std::scalblnl;
using std::scalbn;
using std::scalbnf;
using std::scalbnl;
using std::sinf;
using std::sinl;
using std::sinhf;
using std::sinhl;
using std::sqrtf;
using std::sqrtl;
using std::tanf;
using std::tanl;
using std::tanhf;
using std::tanhl;
using std::tgamma;
using std::tgammaf;
using std::tgammal;
using std::trunc;
using std::truncf;
using std::truncl;

#if !defined(_STRICT_STDC) && !defined(_NO_LONGLONG) || \
    defined(_STDC_C99) || defined(__C99FEATURES__)

using std::llrint;
using std::llrintf;
using std::llrintl;
using std::llround;
using std::llroundf;
using std::llroundl;

#endif	/* !_STRICT_STDC ... C99 */
#endif  /* __EXTENSIONS__ || _STDC_C99 || ... */

#endif	/* __cplusplus >= 199711L */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus)
#define	exception	__math_exception
#endif

#if defined(__EXTENSIONS__) || defined(_XOPEN_SOURCE) || \
	!defined(_STRICT_STDC) && !defined(_POSIX_C_SOURCE)
/*
 * SVID & X/Open
 */
#define	M_E		2.7182818284590452354
#define	M_LOG2E		1.4426950408889634074
#define	M_LOG10E	0.43429448190325182765
#define	M_LN2		0.69314718055994530942
#define	M_LN10		2.30258509299404568402
#define	M_PI		3.14159265358979323846
#define	M_PI_2		1.57079632679489661923
#define	M_PI_4		0.78539816339744830962
#define	M_1_PI		0.31830988618379067154
#define	M_2_PI		0.63661977236758134308
#define	M_2_SQRTPI	1.12837916709551257390
#define	M_SQRT2		1.41421356237309504880
#define	M_SQRT1_2	0.70710678118654752440

extern int signgam;

#define	MAXFLOAT	((float)3.40282346638528860e+38)

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
/*
 * SVID
 */
enum version {libm_ieee = -1, c_issue_4, ansi_1, strict_ansi};

#ifdef __STDC__
extern const enum version _lib_version;
#else
extern enum version _lib_version;
#endif

struct exception {
	int type;
	char *name;
	double arg1;
	double arg2;
	double retval;
};

#define	HUGE		MAXFLOAT

#define	_ABS(x)		((x) < 0 ? -(x) : (x))

#define	_REDUCE(TYPE, X, XN, C1, C2)	{ \
	double x1 = (double)(TYPE)X, x2 = X - x1; \
	X = x1 - (XN) * (C1); X += x2; X -= (XN) * (C2); }

#define	DOMAIN		1
#define	SING		2
#define	OVERFLOW	3
#define	UNDERFLOW	4
#define	TLOSS		5
#define	PLOSS		6

#define	_POLY1(x, c)	((c)[0] * (x) + (c)[1])
#define	_POLY2(x, c)	(_POLY1((x), (c)) * (x) + (c)[2])
#define	_POLY3(x, c)	(_POLY2((x), (c)) * (x) + (c)[3])
#define	_POLY4(x, c)	(_POLY3((x), (c)) * (x) + (c)[4])
#define	_POLY5(x, c)	(_POLY4((x), (c)) * (x) + (c)[5])
#define	_POLY6(x, c)	(_POLY5((x), (c)) * (x) + (c)[6])
#define	_POLY7(x, c)	(_POLY6((x), (c)) * (x) + (c)[7])
#define	_POLY8(x, c)	(_POLY7((x), (c)) * (x) + (c)[8])
#define	_POLY9(x, c)	(_POLY8((x), (c)) * (x) + (c)[9])
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

/*
 * SVID & X/Open
 */
/* BEGIN adopted by C99 */
extern double erf(double);
extern double erfc(double);
extern double hypot(double, double);
extern double lgamma(double);

#if !defined(_STDC_C99) && _XOPEN_SOURCE - 0 < 600 && !defined(__C99FEATURES__)
extern int isnan(double);
#endif
/* END adopted by C99 */

#if defined(__EXTENSIONS__) || _XOPEN_SOURCE - 0 < 600
extern double gamma(double);		/* deprecated; use lgamma */
#endif
extern double j0(double);
extern double j1(double);
extern double jn(int, double);
extern double y0(double);
extern double y1(double);
extern double yn(int, double);

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) || \
	_XOPEN_SOURCE - 0 >= 500 || \
	defined(_XOPEN_SOURCE) && _XOPEN_SOURCE_EXTENDED - 0 == 1
/*
 * SVID & XPG 4.2/5 - removed from XPG7.
 */
#if !defined(_STRICT_SYMBOLS) || !defined(_XPG7)
extern double scalb(double, double);
#endif

/* BEGIN adopted by C99 */
extern double acosh(double);
extern double asinh(double);
extern double atanh(double);
extern double cbrt(double);
extern double logb(double);
extern double nextafter(double, double);
extern double remainder(double, double);

/*
 * XPG 4.2/5
 */
extern double expm1(double);
extern int ilogb(double);
extern double log1p(double);
extern double rint(double);

/* END adopted by C99 */
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) || ... */

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
/*
 * SVID
 */
extern int matherr(struct exception *);

/*
 * IEEE Test Vector
 */
extern double significand(double);

extern int signgamf;				/* deprecated; use signgam */
extern int signgaml;				/* deprecated; use signgam */

extern int isnanf(float);
extern int isnanl(long double);
extern float gammaf(float);		/* deprecated; use lgammaf */
extern float gammaf_r(float, int *);	/* deprecated; use lgammaf_r */
extern float j0f(float);
extern float j1f(float);
extern float jnf(int, float);
extern float lgammaf_r(float, int *);
extern float scalbf(float, float);
extern float significandf(float);
extern float y0f(float);
extern float y1f(float);
extern float ynf(int, float);
extern long double gammal(long double);	/* deprecated; use lgammal */
extern long double gammal_r(long double, int *);	/* deprecated */
extern long double j0l(long double);
extern long double j1l(long double);
extern long double jnl(int, long double);
extern long double lgammal_r(long double, int *);
extern long double scalbl(long double, long double);
extern long double significandl(long double);
extern long double y0l(long double);
extern long double y1l(long double);
extern long double ynl(int, long double);

/*
 * for sin+cos->sincos transformation
 */
extern void sincos(double, double *, double *);
extern void sincosf(float, float *, float *);
extern void sincosl(long double, long double *, long double *);

/* BEGIN adopted by C99 */
/*
 * Functions callable from C, intended to support IEEE arithmetic.
 */
extern double copysign(double, double);
extern double scalbn(double, int);
/* END adopted by C99 */

/*
 * Reentrant version of gamma & lgamma; passes signgam back by reference
 * as the second argument; user must allocate space for signgam.
 */
extern double gamma_r(double, int *);	/* deprecated; use lgamma_r */
extern double lgamma_r(double, int *);

/* BEGIN adopted by C99 */
extern float modff(float, float *);
/* END adopted by C99 */

#if defined(__EXTENSIONS__) || !defined(__cplusplus)
#include <floatingpoint.h>
#endif
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */
#endif	/* defined(__EXTENSIONS__) || defined(_XOPEN_SOURCE) || ... */

#if defined(__cplusplus) && defined(__GNUC__)
#undef	exception
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _MATH_H */
