/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: recover.c,v 11.1 2012/07/06 16:19:20 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>

#define _KERNEL		/* XXX: timespec macros may be protected. */
#include <sys/time.h>
#undef _KERNEL

#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * SF_SYNC is known to identify the existence of the FreeBSD sendfile(2);
 * if you don't have it, we simulate it with mmap(2).
 */
#ifndef SF_SYNC
#include <sys/mman.h>
#endif

/*
 * We include <sys/file.h>, because the open #defines were found there
 * on historical systems.  We also include <fcntl.h> because the open(2)
 * #defines are found there on newer systems.
 */
#include <sys/file.h>

#include <bitstring.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <netdb.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../ex/version.h"
#include "common.h"
#include "pathnames.h"

/*
 * Recovery code.
 *
 * The basic scheme is as follows.  In the EXF structure, we maintain full
 * paths of a b+tree file and a mail recovery file.  The former is the file
 * used as backing store by the DB package.  The latter is the file that
 * contains an email message to be sent to the user if we crash.  The two
 * simple states of recovery are:
 *
 *	+ first starting the edit session:
 *		the b+tree file exists and is mode 700, the mail recovery
 *		file doesn't exist.
 *	+ after the file has been modified:
 *		the b+tree file exists and is mode 600, the mail recovery
 *		file exists, and is exclusively locked.
 *
 * In the EXF structure we maintain a file descriptor that is the locked
 * file descriptor for the mail recovery file.
 *
 * To find out if a recovery file/backing file pair are in use, try to get
 * a lock on the recovery file.
 *
 * To find out if a backing file can be deleted at boot time, check for an
 * owner execute bit.  (Yes, I know it's ugly, but it's either that or put
 * special stuff into the backing file itself, or correlate the files at
 * boot time, neither of which looks like fun.)  Note also that there's a
 * window between when the file is created and the X bit is set.  It's small,
 * but it's there.  To fix the window, check for 0 length files as well.
 *
 * To find out if a file can be recovered, check the F_RCV_ON bit.  Note,
 * this DOES NOT mean that any initialization has been done, only that we
 * haven't yet failed at setting up or doing recovery.
 *
 * To preserve a recovery file/backing file pair, set the F_RCV_NORM bit.
 * If that bit is not set when ending a file session:
 *	If the EXF structure paths (rcv_path and rcv_mpath) are not NULL,
 *	they are unlink(2)'d, and free(3)'d.
 *	If the EXF file descriptor (rcv_fd) is not -1, it is closed.
 *
 * The backing b+tree file is set up when a file is first edited, so that
 * the DB package can use it for on-disk caching and/or to snapshot the
 * file.  When the file is first modified, the mail recovery file is created,
 * the backing file permissions are updated, the file is sync(2)'d to disk,
 * and the timer is started.  Then, at RCV_PERIOD second intervals, the
 * b+tree file is synced to disk.  RCV_PERIOD is measured using SIGALRM, which
 * means that the data structures (SCR, EXF, the underlying tree structures)
 * must be consistent when the signal arrives.
 *
 * The recovery mail file contains normal mail headers, with two additional
 *
 *	X-vi-data: <file|path>;<base64 encoded path>
 *
 * MIME headers; the folding character is limited to ' '.
 *
 * Btree files are named "vi.XXXXXX" and recovery files are named
 * "recover.XXXXXX".
 */

#define	VI_DHEADER	"X-vi-data:"

static int	 rcv_copy __P((SCR *, int, char *));
static void	 rcv_email __P((SCR *, char *));
static int	 rcv_mailfile __P((SCR *, int, char *));
static int	 rcv_mktemp __P((SCR *, char *, char *));
static int	 rcv_sendfile __P((int, char *));
static int	 rcv_dlnwrite __P((SCR *, const char *, const char *, FILE *));
static int	 rcv_dlnread __P((SCR *, char **, char **, FILE *));

/*
 * rcv_tmp --
 *	Build a file name that will be used as the recovery file.
 *
 * PUBLIC: int rcv_tmp __P((SCR *, EXF *, char *));
 */
int
rcv_tmp(
	SCR *sp,
	EXF *ep,
	char *name)
{
	struct stat sb;
	int fd;
	char *dp, *path;

	/*
	 * !!!
	 * ep MAY NOT BE THE SAME AS sp->ep, DON'T USE THE LATTER.
	 *
	 *
	 * If the recovery directory doesn't exist, try and create it.  As
	 * the recovery files are themselves protected from reading/writing
	 * by other than the owner, the worst that can happen is that a user
	 * would have permission to remove other user's recovery files.  If
	 * the sticky bit has the BSD semantics, that too will be impossible.
	 */
	if (opts_empty(sp, O_RECDIR, 0))
		goto err;
	dp = O_STR(sp, O_RECDIR);
	if (stat(dp, &sb)) {
		if (errno != ENOENT || mkdir(dp, 0)) {
			msgq(sp, M_SYSERR, "%s", dp);
			goto err;
		}
		(void)chmod(dp, S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX);
	}

	if ((path = join(dp, "vi.XXXXXX")) == NULL)
		goto err;
	if ((fd = rcv_mktemp(sp, path, dp)) == -1) {
		free(path);
		goto err;
	}
	(void)fchmod(fd, S_IRWXU);
	(void)close(fd);

	ep->rcv_path = path;
	if (0) {
err:		msgq(sp, M_ERR,
		    "056|Modifications not recoverable if the session fails");
		return (1);
	}

	/* We believe the file is recoverable. */
	F_SET(ep, F_RCV_ON);
	return (0);
}

/*
 * rcv_init --
 *	Force the file to be snapshotted for recovery.
 *
 * PUBLIC: int rcv_init __P((SCR *));
 */
int
rcv_init(SCR *sp)
{
	EXF *ep;
	recno_t lno;

	ep = sp->ep;

	/* Only do this once. */
	F_CLR(ep, F_FIRSTMODIFY);

	/* If we already know the file isn't recoverable, we're done. */
	if (!F_ISSET(ep, F_RCV_ON))
		return (0);

	/* Turn off recoverability until we figure out if this will work. */
	F_CLR(ep, F_RCV_ON);

	/* Test if we're recovering a file, not editing one. */
	if (ep->rcv_mpath == NULL) {
		/* Build a file to mail to the user. */
		if (rcv_mailfile(sp, 0, NULL))
			goto err;

		/* Force a read of the entire file. */
		if (db_last(sp, &lno))
			goto err;

		/* Turn on a busy message, and sync it to backing store. */
		sp->gp->scr_busy(sp,
		    "057|Copying file for recovery...", BUSY_ON);
		if (ep->db->sync(ep->db, R_RECNOSYNC)) {
			msgq_str(sp, M_SYSERR, ep->rcv_path,
			    "058|Preservation failed: %s");
			sp->gp->scr_busy(sp, NULL, BUSY_OFF);
			goto err;
		}
		sp->gp->scr_busy(sp, NULL, BUSY_OFF);
	}

	/* Turn off the owner execute bit. */
	(void)chmod(ep->rcv_path, S_IRUSR | S_IWUSR);

	/* We believe the file is recoverable. */
	F_SET(ep, F_RCV_ON);
	return (0);

err:	msgq(sp, M_ERR,
	    "059|Modifications not recoverable if the session fails");
	return (1);
}

/*
 * rcv_sync --
 *	Sync the file, optionally:
 *		flagging the backup file to be preserved
 *		snapshotting the backup file and send email to the user
 *		sending email to the user if the file was modified
 *		ending the file session
 *
 * PUBLIC: int rcv_sync __P((SCR *, u_int));
 */
int
rcv_sync(
	SCR *sp,
	u_int flags)
{
	EXF *ep;
	int fd, rval;
	char *dp, *buf;

	/* Make sure that there's something to recover/sync. */
	ep = sp->ep;
	if (ep == NULL || !F_ISSET(ep, F_RCV_ON))
		return (0);

	/* Sync the file if it's been modified. */
	if (F_ISSET(ep, F_MODIFIED)) {
		SIGBLOCK;
		if (ep->db->sync(ep->db, R_RECNOSYNC)) {
			F_CLR(ep, F_RCV_ON | F_RCV_NORM);
			msgq_str(sp, M_SYSERR,
			    ep->rcv_path, "060|File backup failed: %s");
			SIGUNBLOCK;
			return (1);
		}
		SIGUNBLOCK;

		/* REQUEST: don't remove backing file on exit. */
		if (LF_ISSET(RCV_PRESERVE))
			F_SET(ep, F_RCV_NORM);

		/* REQUEST: send email. */
		if (LF_ISSET(RCV_EMAIL))
			rcv_email(sp, ep->rcv_mpath);
	}

	/*
	 * !!!
	 * Each time the user exec's :preserve, we have to snapshot all of
	 * the recovery information, i.e. it's like the user re-edited the
	 * file.  We copy the DB(3) backing file, and then create a new mail
	 * recovery file, it's simpler than exiting and reopening all of the
	 * underlying files.
	 *
	 * REQUEST: snapshot the file.
	 */
	rval = 0;
	if (LF_ISSET(RCV_SNAPSHOT)) {
		if (opts_empty(sp, O_RECDIR, 0))
			goto err;
		dp = O_STR(sp, O_RECDIR);
		if ((buf = join(dp, "vi.XXXXXX")) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			goto err;
		}
		if ((fd = rcv_mktemp(sp, buf, dp)) == -1) {
			free(buf);
			goto err;
		}
		sp->gp->scr_busy(sp,
		    "061|Copying file for recovery...", BUSY_ON);
		if (rcv_copy(sp, fd, ep->rcv_path) ||
		    close(fd) || rcv_mailfile(sp, 1, buf)) {
			(void)unlink(buf);
			(void)close(fd);
			rval = 1;
		}
		free(buf);
		sp->gp->scr_busy(sp, NULL, BUSY_OFF);
	}
	if (0) {
err:		rval = 1;
	}

	/* REQUEST: end the file session. */
	if (LF_ISSET(RCV_ENDSESSION) && file_end(sp, NULL, 1))
		rval = 1;

	return (rval);
}

/*
 * rcv_mailfile --
 *	Build the file to mail to the user.
 */
static int
rcv_mailfile(
	SCR *sp,
	int issync,
	char *cp_path)
{
	EXF *ep;
	GS *gp;
	struct passwd *pw;
	int len;
	time_t now;
	uid_t uid;
	int fd;
	FILE *fp;
	char *dp, *p, *t, *qt, *buf, *mpath;
	char *t1, *t2, *t3;
	int st;

	/*
	 * XXX
	 * MAXHOSTNAMELEN/HOST_NAME_MAX are deprecated. We try sysconf(3)
	 * first, then fallback to _POSIX_HOST_NAME_MAX.
	 */
	char *host;
	long hostmax = sysconf(_SC_HOST_NAME_MAX);
	if (hostmax < 0)
		hostmax = _POSIX_HOST_NAME_MAX;

	gp = sp->gp;
	if ((pw = getpwuid(uid = getuid())) == NULL) {
		msgq(sp, M_ERR,
		    "062|Information on user id %u not found", uid);
		return (1);
	}

	if (opts_empty(sp, O_RECDIR, 0))
		return (1);
	dp = O_STR(sp, O_RECDIR);
	if ((mpath = join(dp, "recover.XXXXXX")) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}
	if ((fd = rcv_mktemp(sp, mpath, dp)) == -1) {
		free(mpath);
		return (1);
	}
	if ((fp = fdopen(fd, "w")) == NULL) {
		free(mpath);
		close(fd);
		return (1);
	}

	/*
	 * XXX
	 * We keep an open lock on the file so that the recover option can
	 * distinguish between files that are live and those that need to
	 * be recovered.  There's an obvious window between the mkstemp call
	 * and the lock, but it's pretty small.
	 */
	ep = sp->ep;
	if (file_lock(sp, NULL, fd, 1) != LOCK_SUCCESS)
		msgq(sp, M_SYSERR, "063|Unable to lock recovery file");
	if (!issync) {
		/* Save the recover file descriptor, and mail path. */
		ep->rcv_fd = dup(fd);
		ep->rcv_mpath = mpath;
		cp_path = ep->rcv_path;
	}

	t = sp->frp->name;
	if ((p = strrchr(t, '/')) == NULL)
		p = t;
	else
		++p;
	(void)time(&now);

	if ((st = rcv_dlnwrite(sp, "file", t, fp))) {
		if (st == 1)
			goto werr;
		goto err;
	}
	if ((st = rcv_dlnwrite(sp, "path", cp_path, fp))) {
		if (st == 1)
			goto werr;
		goto err;
	}

	MALLOC(sp, host, char *, hostmax + 1);
	if (host == NULL)
		goto err;
	(void)gethostname(host, hostmax + 1);

	len = fprintf(fp, "%s%s%s\n%s%s%s%s\n%s%.40s\n%s\n\n",
	    "From: root@", host, " (Nvi recovery program)",
	    "To: ", pw->pw_name, "@", host,
	    "Subject: Nvi saved the file ", p,
	    "Precedence: bulk");		/* For vacation(1). */
	if (len < 0) {
		free(host);
		goto werr;
	}

	if ((qt = quote(t)) == NULL) {
		free(host);
		msgq(sp, M_SYSERR, NULL);
		goto err;
	}
	len = asprintf(&buf, "%s%.24s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n\n",
	    "On ", ctime(&now), ", the user ", pw->pw_name,
	    " was editing a file named ", t, " on the machine ",
	    host, ", when it was saved for recovery. ",
	    "You can recover most, if not all, of the changes ",
	    "to this file using the -r option to ", gp->progname, ":\n\n\t",
	    gp->progname, " -r ", qt);
	free(qt);
	free(host);
	if (buf == NULL) {
		msgq(sp, M_SYSERR, NULL);
		goto err;
	}

	/*
	 * Format the message.  (Yes, I know it's silly.)
	 * Requires that the message end in a <newline>.
	 */
#define	FMTCOLS	60
	for (t1 = buf; len > 0; len -= t2 - t1, t1 = t2) {
		/* Check for a short length. */
		if (len <= FMTCOLS) {
			t2 = t1 + (len - 1);
			goto wout;
		}

		/* Check for a required <newline>. */
		t2 = strchr(t1, '\n');
		if (t2 - t1 <= FMTCOLS)
			goto wout;

		/* Find the closest space, if any. */
		for (t3 = t2; t2 > t1; --t2)
			if (*t2 == ' ') {
				if (t2 - t1 <= FMTCOLS)
					goto wout;
				t3 = t2;
			}
		t2 = t3;

		/* t2 points to the last character to display. */
wout:		*t2++ = '\n';

		/* t2 points one after the last character to display. */
		if (fwrite(t1, 1, t2 - t1, fp) != t2 - t1) {
			free(buf);
			goto werr;
		}
	}

	if (issync) {
		fflush(fp);
		rcv_email(sp, mpath);
		free(mpath);
	}
	if (fclose(fp)) {
		free(buf);
werr:		msgq(sp, M_SYSERR, "065|Recovery file");
		goto err;
	}
	free(buf);
	return (0);

err:	if (!issync)
		ep->rcv_fd = -1;
	if (fp != NULL)
		(void)fclose(fp);
	return (1);
}

/*
 *	people making love
 *	never exactly the same
 *	just like a snowflake
 *
 * rcv_list --
 *	List the files that can be recovered by this user.
 *
 * PUBLIC: int rcv_list __P((SCR *));
 */
int
rcv_list(SCR *sp)
{
	struct dirent *dp;
	struct stat sb;
	DIR *dirp;
	FILE *fp;
	int found;
	char *p, *file, *path;
	char *dtype, *data;
	int st;

	/* Open the recovery directory for reading. */
	if (opts_empty(sp, O_RECDIR, 0))
		return (1);
	p = O_STR(sp, O_RECDIR);
	if (chdir(p) || (dirp = opendir(".")) == NULL) {
		msgq_str(sp, M_SYSERR, p, "recdir: %s");
		return (1);
	}

	/* Read the directory. */
	for (found = 0; (dp = readdir(dirp)) != NULL;) {
		if (strncmp(dp->d_name, "recover.", 8))
			continue;

		/* If it's readable, it's recoverable. */
		if ((fp = fopen(dp->d_name, "r")) == NULL)
			continue;

		switch (file_lock(sp, NULL, fileno(fp), 1)) {
		case LOCK_FAILED:
			/*
			 * XXX
			 * Assume that a lock can't be acquired, but that we
			 * should permit recovery anyway.  If this is wrong,
			 * and someone else is using the file, we're going to
			 * die horribly.
			 */
			break;
		case LOCK_SUCCESS:
			break;
		case LOCK_UNAVAIL:
			/* If it's locked, it's live. */
			(void)fclose(fp);
			continue;
		}

		/* Check the headers. */
		for (file = NULL, path = NULL;
		    file == NULL || path == NULL;) {
			if ((st = rcv_dlnread(sp, &dtype, &data, fp))) {
				if (st == 1)
					msgq_str(sp, M_ERR, dp->d_name,
					    "066|%s: malformed recovery file");
				goto next;
			}
			if (dtype == NULL)
				continue;
			if (!strcmp(dtype, "file"))
				file = data;
			else if (!strcmp(dtype, "path"))
				path = data;
			else
				free(data);
		}

		/*
		 * If the file doesn't exist, it's an orphaned recovery file,
		 * toss it.
		 *
		 * XXX
		 * This can occur if the backup file was deleted and we crashed
		 * before deleting the email file.
		 */
		errno = 0;
		if (stat(path, &sb) &&
		    errno == ENOENT) {
			(void)unlink(dp->d_name);
			goto next;
		}

		/* Get the last modification time and display. */
		(void)fstat(fileno(fp), &sb);
		(void)printf("%.24s: %s\n",
		    ctime(&sb.st_mtime), file);
		found = 1;

		/* Close, discarding lock. */
next:		(void)fclose(fp);
		if (file != NULL)
			free(file);
		if (path != NULL)
			free(path);
	}
	if (found == 0)
		(void)printf("%s: No files to recover\n", sp->gp->progname);
	(void)closedir(dirp);
	return (0);
}

/*
 * rcv_read --
 *	Start a recovered file as the file to edit.
 *
 * PUBLIC: int rcv_read __P((SCR *, FREF *));
 */
int
rcv_read(
	SCR *sp,
	FREF *frp)
{
	struct dirent *dp;
	struct stat sb;
	DIR *dirp;
	FILE *fp;
	EXF *ep;
	struct timespec rec_mtim = { 0, 0 };
	int found, locked = 0, requested, sv_fd;
	char *name, *p, *t, *rp, *recp, *pathp;
	char *file, *path, *recpath;
	char *dtype, *data;
	int st;

	if (opts_empty(sp, O_RECDIR, 0))
		return (1);
	rp = O_STR(sp, O_RECDIR);
	if ((dirp = opendir(rp)) == NULL) {
		msgq_str(sp, M_ERR, rp, "%s");
		return (1);
	}

	name = frp->name;
	sv_fd = -1;
	recp = pathp = NULL;
	for (found = requested = 0; (dp = readdir(dirp)) != NULL;) {
		if (strncmp(dp->d_name, "recover.", 8))
			continue;
		if ((recpath = join(rp, dp->d_name)) == NULL) {
			msgq(sp, M_SYSERR, NULL);
			continue;
		}

		/* If it's readable, it's recoverable. */
		if ((fp = fopen(recpath, "r")) == NULL) {
			free(recpath);
			continue;
		}

		switch (file_lock(sp, NULL, fileno(fp), 1)) {
		case LOCK_FAILED:
			/*
			 * XXX
			 * Assume that a lock can't be acquired, but that we
			 * should permit recovery anyway.  If this is wrong,
			 * and someone else is using the file, we're going to
			 * die horribly.
			 */
			locked = 0;
			break;
		case LOCK_SUCCESS:
			locked = 1;
			break;
		case LOCK_UNAVAIL:
			/* If it's locked, it's live. */
			(void)fclose(fp);
			continue;
		}

		/* Check the headers. */
		for (file = NULL, path = NULL;
		    file == NULL || path == NULL;) {
			if ((st = rcv_dlnread(sp, &dtype, &data, fp))) {
				if (st == 1)
					msgq_str(sp, M_ERR, dp->d_name,
					    "067|%s: malformed recovery file");
				goto next;
			}
			if (dtype == NULL)
				continue;
			if (!strcmp(dtype, "file"))
				file = data;
			else if (!strcmp(dtype, "path"))
				path = data;
			else
				free(data);
		}
		++found;

		/*
		 * If the file doesn't exist, it's an orphaned recovery file,
		 * toss it.
		 *
		 * XXX
		 * This can occur if the backup file was deleted and we crashed
		 * before deleting the email file.
		 */
		errno = 0;
		if (stat(path, &sb) &&
		    errno == ENOENT) {
			(void)unlink(dp->d_name);
			goto next;
		}

		/* Check the file name. */
		if (strcmp(file, name))
			goto next;

		++requested;

		/* If we've found more than one, take the most recent. */
		(void)fstat(fileno(fp), &sb);
		if (recp == NULL ||
		    timespeccmp(&rec_mtim, &sb.st_mtimespec, <)) {
			p = recp;
			t = pathp;
			recp = recpath;
			pathp = path;
			if (p != NULL) {
				free(p);
				free(t);
			}
			rec_mtim = sb.st_mtimespec;
			if (sv_fd != -1)
				(void)close(sv_fd);
			sv_fd = dup(fileno(fp));
		} else {
next:			free(recpath);
			if (path != NULL)
				free(path);
		}
		(void)fclose(fp);
		if (file != NULL)
			free(file);
	}
	(void)closedir(dirp);

	if (recp == NULL) {
		msgq_str(sp, M_INFO, name,
		    "068|No files named %s, readable by you, to recover");
		return (1);
	}
	if (found) {
		if (requested > 1)
			msgq(sp, M_INFO,
	    "069|There are older versions of this file for you to recover");
		if (found > requested)
			msgq(sp, M_INFO,
			    "070|There are other files for you to recover");
	}

	/*
	 * Create the FREF structure, start the btree file.
	 *
	 * XXX
	 * file_init() is going to set ep->rcv_path.
	 */
	if (file_init(sp, frp, pathp, 0)) {
		free(recp);
		free(pathp);
		(void)close(sv_fd);
		return (1);
	}
	free(pathp);

	/*
	 * We keep an open lock on the file so that the recover option can
	 * distinguish between files that are live and those that need to
	 * be recovered.  The lock is already acquired, just copy it.
	 */
	ep = sp->ep;
	ep->rcv_mpath = recp;
	ep->rcv_fd = sv_fd;
	if (!locked)
		F_SET(frp, FR_UNLOCKED);

	/* We believe the file is recoverable. */
	F_SET(ep, F_RCV_ON);
	return (0);
}

/*
 * rcv_copy --
 *	Copy a recovery file.
 */
static int
rcv_copy(
	SCR *sp,
	int wfd,
	char *fname)
{
	int nr, nw, off, rfd;
	char buf[8 * 1024];

	if ((rfd = open(fname, O_RDONLY, 0)) == -1)
		goto err;
	while ((nr = read(rfd, buf, sizeof(buf))) > 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, nr)) < 0)
				goto err;
	if (nr == 0)
		return (0);

err:	msgq_str(sp, M_SYSERR, fname, "%s");
	return (1);
}

/*
 * rcv_mktemp --
 *	Paranoid make temporary file routine.
 */
static int
rcv_mktemp(
	SCR *sp,
	char *path,
	char *dname)
{
	int fd;

	if ((fd = mkstemp(path)) == -1)
		msgq_str(sp, M_SYSERR, dname, "%s");
	return (fd);
}

/*
 * rcv_email --
 *	Send email.
 */
static void
rcv_email(
	SCR *sp,
	char *fname)
{
	struct stat sb;
	struct passwd *pw;
	FILE *fp = NULL;
	int fd = -1;
	char *host = NULL;
	long hostmax;
	int eno;
	struct addrinfo *res0 = NULL, *res;
	struct addrinfo hints = { 0, PF_UNSPEC,
				  SOCK_STREAM, IPPROTO_TCP };

	/* Prepare the the recipient. */
	if (stat(fname, &sb))
		goto err;
	if ((pw = getpwuid(sb.st_uid)) == NULL)
		goto err;

	/* Prepare the required socket(2) info. */
	hostmax = sysconf(_SC_HOST_NAME_MAX);
	if (hostmax < 0)
		hostmax = _POSIX_HOST_NAME_MAX;
	if ((host = malloc(hostmax + 1)) == NULL)
		goto err;
	(void)gethostname(host, hostmax + 1);
	if ((eno = getaddrinfo(host, "smtp", &hints, &res0)))
		goto aierr;

	/* Prepare a stream over socket(2). */
	for (res = res0; res != NULL; res = res->ai_next) {
		if ((fd = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol)) < 0)
			continue;
		if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
			(void)close(fd);
			fd = -1;
			continue;
		}
		break;
	}
	if (fd < 0)
		goto err;
	if ((fp = fdopen(fd, "w")) == NULL)
		goto err;

	/* Send the email. */
	if (fprintf(fp, "%s%s\r\n%s%s\r\n%s%s%s%s\r\n%s\r\n",
	    "HELO ", host,
	    "MAIL FROM: root@", host,
	    "RCPT TO: ", pw->pw_name, "@", host,
	    "DATA") < 0)
		goto err;
	if (fputs(
	    "User-Agent: nvi/" VI_VERSION "\n", fp) == EOF)
		goto err;
	(void)fflush(fp);
	if (rcv_sendfile(fd, fname))
		goto err;
	(void)fprintf(fp, ".\r\nQUIT\r\n");

	if (0)
err:		msgq_str(sp, M_ERR, strerror(errno),
		    "071|not sending email: %s");
	if (fp != NULL)
		(void)fclose(fp);
	if (res0 != NULL)
		freeaddrinfo(res0);
	if (0)
aierr:		msgq_str(sp, M_ERR, gai_strerror(eno),
		    "071|not sending email: %s");
	if (host != NULL)
		free(host);
}

/*
 * rcv_sendfile --
 *	Send a file out a stream socket.
 */
static int
rcv_sendfile(
	int fd,
	char *fname)
{
	int rval;
	int mfd;
#ifndef SF_SYNC
	char *s;
	struct stat sb;
#endif

	if ((mfd = open(fname, O_RDONLY)) == -1)
		return (-1);
#ifdef SF_SYNC
	rval = sendfile(mfd, fd, 0, 0, NULL, NULL, SF_SYNC);
#else
	if (stat(fname, &sb) ||
	    (s = mmap(NULL, sb.st_size,
	    PROT_READ, MAP_PRIVATE, mfd, 0)) == MAP_FAILED) {
		(void)close(mfd);
		return (-1);
	}
	rval = send(fd, s, sb.st_size, 0) == -1 ? -1 : 0;
	(void)munmap(s, sb.st_size);
#endif
	(void)close(mfd);
	return (rval);
}

/*
 * rcv_dlnwrite --
 *	Encode a string into an X-vi-data line and write it.
 */
static int
rcv_dlnwrite(
	SCR *sp,
	const char *dtype,
	const char *src,
	FILE *fp)
{
	char *bp = NULL, *p;
	size_t blen = 0;
	size_t dlen, len;
	int plen, xlen;

	len = strlen(src);
	dlen = strlen(dtype);
	GET_SPACE_GOTOC(sp, bp, blen, (len + 2) / 3 * 4 + dlen + 2);
	(void)memcpy(bp, dtype, dlen);
	bp[dlen] = ';';
	if ((xlen = b64_ntop((u_char *)src,
	    len, bp + dlen + 1, blen)) == -1)
		goto err;
	xlen += dlen + 1;

	/* Output as an MIME folding header. */
	if ((plen = fprintf(fp, VI_DHEADER " %.*s\n",
	    FMTCOLS - (int)sizeof(VI_DHEADER), bp)) < 0)
		goto err;
	plen -= (int)sizeof(VI_DHEADER) + 1;
	for (p = bp, xlen -= plen; xlen > 0; xlen -= plen) {
		p += plen;
		if ((plen = fprintf(fp, " %.*s\n", FMTCOLS - 1, p)) < 0)
			goto err;
		plen -= 2;
	}
	FREE_SPACE(sp, bp, blen);
	return (0);

err:	FREE_SPACE(sp, bp, blen);
	return (1);
alloc_err:
	msgq(sp, M_SYSERR, NULL);
	return (-1);
}

/*
 * rcv_dlnread --
 *	Read an X-vi-data line and decode it.
 */
static int
rcv_dlnread(
	SCR *sp,
	char **dtypep,
	char **datap,		/* free *datap if != NULL after use. */
	FILE *fp)
{
	int ch;
	char buf[1024];
	char *bp = NULL, *p, *src;
	size_t blen = 0;
	size_t len, off, dlen;
	char *dtype, *data;
	int xlen;

	if (fgets(buf, sizeof(buf), fp) == NULL)
		return (1);
	if (strncmp(buf, VI_DHEADER, sizeof(VI_DHEADER) - 1)) {
		*dtypep = NULL;
		*datap = NULL;
		return (0);
	}

	/* Fetch an MIME folding header. */
	len = strlen(buf) - sizeof(VI_DHEADER) + 1;
	GET_SPACE_GOTOC(sp, bp, blen, len);
	(void)memcpy(bp, buf + sizeof(VI_DHEADER) - 1, len);
	p = bp + len;
	while ((ch = fgetc(fp)) == ' ') {
		if (fgets(buf, sizeof(buf), fp) == NULL)
			goto err;
		off = strlen(buf);
		len += off;
		ADD_SPACE_GOTOC(sp, bp, blen, len);
		p = bp + len - off;
		(void)memcpy(p, buf, off);
	}
	bp[len] = '\0';
	(void)ungetc(ch, fp);

	for (p = bp; *p == ' ' || *p == '\n'; p++);
	if ((src = strchr(p, ';')) == NULL)
		goto err;
	dlen = src - p;
	src += 1;
	len -= src - bp;

	/* Memory looks like: "<data>\0<dtype>\0". */
	MALLOC(sp, data, char *, dlen + len / 4 * 3 + 2);
	if (data == NULL)
		goto err;
	if ((xlen = (b64_pton(p + dlen + 1,
	    (u_char *)data, len / 4 * 3 + 1))) == -1) {
		free(data);
		goto err;
	}
	data[xlen] = '\0';
	dtype = data + xlen + 1;
	(void)memcpy(dtype, p, dlen);
	dtype[dlen] = '\0';
	FREE_SPACE(sp, bp, blen);
	*dtypep = dtype;
	*datap = data;
	return (0);

err: 	FREE_SPACE(sp, bp, blen);
	return (1);
alloc_err:
	msgq(sp, M_SYSERR, NULL);
	return (-1);
}
