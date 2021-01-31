/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_right -- [count]' ', [count]l
 *	Move right by columns.
 *
 * PUBLIC: int v_right(SCR *, VICMD *);
 */
int
v_right(SCR *sp, VICMD *vp)
{
	recno_t cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	recno_t cno = vp->m_start.cno;
	recno_t lno = vp->m_start.lno;
	size_t len;

	if (db_get(sp, lno, 0, NULL, &len))
		return 1;

	if (cnt == 1 && cno + 1 == len && ISMOTION(vp)) {
		/* Historically, "[cdsy]l" worked at the end of a line. */
		vp->m_stop.cno = len - 1;
		vp->m_final = vp->m_start;
		return 0;
	}

	while (cnt) {
		size_t next_len;

		if (cno + cnt < len) {
			cno += cnt;
			break;
		}

		cnt -= len ? len - cno : 1;

		if (db_get(sp, lno + 1, 0, NULL, &next_len)) {
			cno = len ? len - 1 : 0;
			break;
		}

		len = next_len;
		lno++;
		cno = 0;
	}

	vp->m_stop.lno = lno;
	vp->m_stop.cno = cno;

	/*
	 * Non-motion commands move to the end of the range.  Delete and
	 * yank stay at the start.  Ignore others.  Adjust the end of the
	 * range for motion commands.
	 */
	if (ISMOTION(vp)) {
		--vp->m_stop.cno;
		vp->m_final = vp->m_start;
	} else
		vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_dollar -- [count]$
 *	Move to the last column.
 *
 * PUBLIC: int v_dollar(SCR *, VICMD *);
 */
int
v_dollar(SCR *sp, VICMD *vp)
{
	size_t len;
	int isempty;

	/*
	 * !!!
	 * A count moves down count - 1 rows, so, "3$" is the same as "2j$".
	 */
	if ((F_ISSET(vp, VC_C1SET) ? vp->count : 1) != 1) {
		/*
		 * !!!
		 * Historically, if the $ is a motion, and deleting from
		 * at or before the first non-blank of the line, it's a
		 * line motion, and the line motion flag is set.
		 */
		vp->m_stop.cno = 0;
		if (nonblank(sp, vp->m_start.lno, &vp->m_stop.cno))
			return (1);
		if (ISMOTION(vp) && vp->m_start.cno <= vp->m_stop.cno)
			F_SET(vp, VM_LMODE);

		--vp->count;
		if (v_down(sp, vp))
			return (1);
	}

	/*
	 * !!!
	 * Historically, it was illegal to use $ as a motion command on
	 * an empty line.  Unfortunately, even though C was historically
	 * aliased to c$, it (and not c$) was special cased to work on
	 * empty lines.  Since we alias C to c$ too, we have a problem.
	 * To fix it, we let c$ go through, on the assumption that it's
	 * not a problem for it to work.
	 */
	if (db_eget(sp, vp->m_stop.lno, NULL, &len, &isempty)) {
		if (!isempty)
			return (1);
		len = 0;
	}

	if (len == 0) {
		if (ISMOTION(vp) && !ISCMD(vp->rkp, 'c')) {
			v_eol(sp, NULL);
			return (1);
		}
		return (0);
	}

	/*
	 * Non-motion commands move to the end of the range.  Delete
	 * and yank stay at the start.  Ignore others.
	 */
	vp->m_stop.cno = len ? len - 1 : 0;
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}
