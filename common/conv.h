/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 * Copyright (c) 2011, 2012
 *	Zhihao Yuan.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: conv.h,v 2.31 2011/12/14 14:58:15 zy Exp $
 */

#ifdef USE_ICONV
#include <iconv.h>

/*
 * from NetBSD's iconv(3):
 *
 * Historically, the definition of iconv has not been consistent across
 * operating systems.  This is due to an unfortunate historical mistake,
 * documented in this e-mail:
 *       https://www5.opengroup.org/sophocles2/show_mail.tpl?&source=L&listname=austin-group-l&id=7404.
 * The standards page for the header file <iconv.h> defined the second argu-
 * ment of iconv() as char **, but the standards page for the iconv() imple-
 * mentation defined it as const char **.  The standards committee later
 * chose to change the function definition to follow the header file defini-
 * tion (without const), even though the version with const is arguably more
 * correct.  NetBSD has always used the const form.  It was decided to
 * reject the committee's regression and become (technically) incompatible.
 * GNU libiconv has taken the same route:
 *       http://www.gnu.org/savannah-checkouts/gnu/libiconv/documentation/libiconv-1.14/.
 * Most third party software affected by this issue already handles it dur-
 * ing configuration.
 */
#if defined(__NetBSD__)
#define	iconv_src_t	const char **
#else
#define	iconv_src_t	char **
#endif
#else
typedef int	iconv_t;
#endif

/*
 * XXX
 * We can not use MB_CUR_MAX here, since UTF-8 may report it as 6, but
 * a sequence longer than 4 is deprecated by RFC 3629.
 */
#define KEY_NEEDSWIDE(sp, ch)						\
	(INTISWIDE(ch) && KEY_LEN(sp, ch) <= 4)
#define KEY_COL(sp, ch)							\
	(KEY_NEEDSWIDE(sp, ch) ? CHAR_WIDTH(sp, ch) : KEY_LEN(sp, ch))

enum { IC_FE_CHAR2INT, IC_FE_INT2CHAR, IC_IE_CHAR2INT, IC_IE_TO_UTF16 };

struct _conv_win {
	union {
		char 	*c;
		CHAR_T	*wc;
	}	bp1;
	size_t	blen1;
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
	iconv_t		id[IC_IE_TO_UTF16 + 1];
};
