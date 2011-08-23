/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: conv.h,v 1.28 2011/07/16 14:40:38 zy Exp $ (Berkeley) $Date: 2011/07/16 14:40:38 $
 */

#define KEY_COL(sp, ch)							\
	(INTISWIDE(ch) ? CHAR_WIDTH(sp, ch) > 0 ? CHAR_WIDTH(sp, ch) : 	\
					      1 : /* extra space */	\
			 KEY_LEN(sp,ch))

struct _conv_win {
	union {
		char 	*c;
		CHAR_T	*wc;
	}	bp1;
    size_t   blen1;
};

typedef int (*char2wchar_t) 
    (SCR *, const char *, ssize_t, struct _conv_win *, size_t *, CHAR_T **);
typedef int (*wchar2char_t) 
    (SCR *, const CHAR_T *, ssize_t, struct _conv_win *, size_t *, char **);

struct _conv {
	char2wchar_t	sys2int;
	wchar2char_t	int2sys;
	char2wchar_t	file2int;
	wchar2char_t	int2file;
	char2wchar_t	input2int;
	wchar2char_t	int2disp;
};
