/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 * Copyright (c) 2011
 *	Zhihao Yuan.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: port.h,v 9.1 2011/08/21 03:41:06 zy Exp $ (Berkeley) $Date: 2011/08/21 03:41:06 $
 */

/*
 * XXX
 * 2.9BSD extension to create a new process without copying the address space
 */
#ifndef	HAVE_VFORK
#define	vfork	fork
#endif

/*
 * XXX
 * Some versions of System V changed the number of arguments to gettimeofday
 * without changing the name.
 */
#ifdef HAVE_BROKEN_GETTIMEOFDAY
#define	gettimeofday(tv, tz)	gettimeofday(tv)
#endif

/* 
 * XXX
 * If we don't have mmap, we fake it with read and write, but we'll
 * still need the header information.
 */
#ifndef HAVE_SYS_MMAN_H
#define	MAP_SHARED	1		/* share changes */
#define	MAP_PRIVATE	2		/* changes are private */
#define	PROT_READ	0x1		/* pages can be read */
#define	PROT_WRITE	0x2		/* pages can be written */
#define	PROT_EXEC	0x4		/* pages can be executed */
#endif

/*
 * XXX
 * 4.4BSD extension to only set the software termios bits.
 */
#ifndef	TCSASOFT			/* 4.4BSD extension. */
#define	TCSASOFT	0
#endif

/*
 * XXX
 * MIN, MAX, historically in <sys/param.h>
 */
#ifndef	MAX
#define	MAX(_a,_b)	((_a)<(_b)?(_b):(_a))
#endif
#ifndef	MIN
#define	MIN(_a,_b)	((_a)<(_b)?(_a):(_b))
#endif

/*
 * XXX
 * 4.4BSD extension to provide lock values in the open(2) call.
 */
#ifndef O_EXLOCK
#define	O_EXLOCK	0
#endif

#ifndef O_SHLOCK
#define	O_SHLOCK	0
#endif

/*
 * XXX
 * 4.4BSD extension to determine if a program dropped core from the exit
 * status.
 */
#ifndef	WCOREDUMP
#define	WCOREDUMP(a)	0
#endif
