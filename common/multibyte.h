/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: multibyte.h,v 1.28 2011/07/16 18:40:01 zy Exp $ (Berkeley) $Date: 2011/07/16 18:40:01 $
 */

#ifndef MULTIBYTE_H
#define MULTIBYTE_H

/*
 * Fundamental character types.
 *
 * CHAR_T	An integral type that can hold any character.
 * ARG_CHAR_T	The type of a CHAR_T when passed as an argument using
 *		traditional promotion rules.  It should also be able
 *		to be compared against any CHAR_T for equality without
 *		problems.
 * MAX_CHAR_T	The maximum value of any character.
 *
 * If no integral type can hold a character, don't even try the port.
 */
typedef	int		ARG_CHAR_T;

#ifdef USE_WIDECHAR
#include <wchar.h>
#include <wctype.h>

typedef	wchar_t		CHAR_T;
#define	MAX_CHAR_T	0x7fffffff
typedef	u_int		UCHAR_T;
typedef wchar_t 	RCHAR_T;
#define RCHAR_T_MAX	((1 << 24)-1)
#define RCHAR_BIT	24

#define STRLEN		wcslen
#define STRTOL		wcstol
#define STRTOUL		wcstoul
#define SPRINTF		swprintf
#define STRCMP		wcscmp
#define STRPBRK		wcspbrk
#define TOUPPER		towupper
#define STRSET		wmemset
#define VSPRINTF	vswprintf

#define L(ch)		L ## ch

#else
typedef	u_char		CHAR_T;
#define	MAX_CHAR_T	0xff
typedef	u_char		UCHAR_T;
typedef char		RCHAR_T;
#define RCHAR_T_MAX	CHAR_MAX
#define RCHAR_BIT 	CHAR_BIT

#define STRLEN		strlen
#define STRTOL(a,b,c)	(strtol(a,(char**)b,c))
#define STRTOUL(a,b,c)	(strtoul(a,(char**)b,c))
#define SPRINTF		snprintf
#define STRCMP		strcmp
#define STRPBRK		strpbrk
#define TOUPPER		toupper
#define STRSET		memset
#define VSPRINTF	vsnprintf

#define L(ch)		ch

#endif

#define MEMCMP(to, from, n) 						    \
	memcmp(to, from, (n) * sizeof(*(to)))
#define	MEMMOVE(p, t, len)	memmove(p, t, (len) * sizeof(*(p)))
#define	MEMCPY(p, t, len)	memcpy(p, t, (len) * sizeof(*(p)))
#define SIZE(w)		(sizeof(w)/sizeof(*w))

#endif
