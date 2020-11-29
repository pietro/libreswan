/* logging declarations
 *
 * Copyright (C) 1998-2001,2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2004 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2012-2013 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2017-2019 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef _LSWLOG_H_
#define _LSWLOG_H_

#include <stdarg.h>
#include <stdio.h>		/* for FILE */
#include <stddef.h>		/* for size_t */

#include "lset.h"
#include "lswcdefs.h"
#include "jambuf.h"
#include "passert.h"
#include "constants.h"		/* for DBG_... */
#include "where.h"		/* used by macros */
#include "fd.h"			/* for null_fd */
#include "impair.h"

#define LOG_WIDTH	((size_t)1024)	/* roof of number of chars in log line */

extern bool log_to_stderr;          /* should log go to stderr? */


/*
 * Codes for status messages returned to whack.
 *
 * These are 3 digit decimal numerals.  The structure is inspired by
 * section 4.2 of RFC959 (FTP).  Since these will end up as the exit
 * status of whack, they must be less than 256.
 *
 * NOTE: ipsec_auto(8) knows about some of these numbers -- change
 * carefully.
 */

enum rc_type {
	RC_COMMENT,		/* non-commital utterance with 000 prefix(does not affect exit status) */
	RC_RAW,			/* ditto, but also suppresses the 000 prefix */
	RC_LOG,			/* message aimed at log (does not affect exit status) */
	RC_LOG_SERIOUS,		/* serious message aimed at log (does not affect exit status) */
	RC_SUCCESS,		/* success (exit status 0) */
	RC_INFORMATIONAL,	/* should get relayed to user - if there is one */
	RC_INFORMATIONAL_TRAFFIC, /* status of an established IPSEC (aka Phase 2) state */

	/* failure, but not definitive */
	RC_RETRANSMISSION = 10,

	/* improper request */
	RC_EXIT_FLOOR = 20,
	RC_DUPNAME = RC_EXIT_FLOOR,	/* attempt to reuse a connection name */
	RC_UNKNOWN_NAME,	/* connection name unknown or state number */
	RC_ORIENT,		/* cannot orient connection: neither end is us */
	RC_CLASH,		/* clash between two Road Warrior connections OVERLOADED */
	RC_DEAF,		/* need --listen before --initiate */
	RC_ROUTE,		/* cannot route */
	RC_RTBUSY,		/* cannot unroute: route busy */
	RC_BADID,		/* malformed --id */
	RC_NOKEY,		/* no key found through DNS */
	RC_NOPEERIP,		/* cannot initiate when peer IP is unknown */
	RC_INITSHUNT,		/* cannot initiate a shunt-oly connection */
	RC_WILDCARD,		/* cannot initiate when ID has wildcards */
	RC_CRLERROR,		/* CRL fetching disabled or obsolete reread cmd */
	RC_WHACK_PROBLEM,	/* whack-detected problem */

	/* permanent failure */
	RC_BADWHACKMESSAGE = 30,
	RC_NORETRANSMISSION,
	RC_INTERNALERR,
	RC_OPPOFAILURE,		/* Opportunism failed */
	RC_CRYPTOFAILED,	/* system too busy to perform required
				* cryptographic operations */
	RC_AGGRALGO,		/* multiple algorithms requested in phase 1 aggressive */
	RC_FATAL,		/* fatal error encountered, and negotiation aborted */

	/* entry of secrets */
	RC_ENTERSECRET = 40,
	RC_USERPROMPT = 41,

	RC_EXIT_ROOF = 100,

	/*
	 * progress: start of range for successful state transition.
	 * Actual value is RC_NEW_V[12]_STATE plus the new state code.
	 */
	RC_NEW_V1_STATE = RC_EXIT_ROOF,
	RC_NEW_V2_STATE = 150,

	/*
	 * Start of range for notification.
	 *
	 * Actual value is RC_NOTIFICATION plus code for notification
	 * that should be generated by this Pluto.  RC_NOTIFICATION.
	 * Since notifications are two octets, that's 64435+200 which
	 * overflows the 3-digit prefix, oops.
	 */
	RC_NOTIFICATION = 200,	/* as per IKE notification messages */
};


/*
 * A generic buffer for accumulating unbounded output.
 *
 * The buffer's contents can be directed to various logging streams.
 */

struct jambuf;

/*
 * The logging streams used by libreswan.
 *
 * So far three^D^D^D^D^D four^D^D^D^D five^D^D^D^D six^D^D^D
 * seven^D^D^D^D^D five.five streams have been identified; and let's
 * not forget that code writes to STDOUT and STDERR directly.
 *
 * The streams differ in the syslog severity and what PREFIX is
 * assumed to be present and the tool being run.
 *
 *                           PLUTO
 *              SEVERITY  WHACK  PREFIX    TOOLS    PREFIX
 *   default    WARNING    yes    state     -v
 *   log        WARNING     -     state     -v
 *   debug      DEBUG       -     "| "     debug?
 *   error      ERR         -    ERROR     STDERR  PROG:_...
 *   whack         -       yes    NNN      STDOUT  ...
 *   file          -        -      -         -
 *
 * The streams will then add additional prefixes as required.  For
 * instance, the log_whack stream will prefix a timestamp when sending
 * to a file (optional), and will prefix NNN(RC) when sending to
 * whack.
 *
 * For tools, the default and log streams go to STDERR when enabled;
 * and the debug stream goes to STDERR conditional on debug flags.
 * Should the whack stream go to stdout?
 *
 * As needed, return size_t - the number of bytes written - so that
 * implementations have somewhere to send values that should not be
 * ignored; for instance fwrite() :-/
 */

/*
 * By default messages are broadcast (to both log files and whack),
 * mix-in one of these options to limit this.
 */

enum stream {
	/*
	 * This means that a simple RC_* code will go to both whack
	 * and and the log files.
	 */
	/* Mask the whack RC; max value is 64435+200 */
	RC_MASK      = 0x0fffff,
	STREAM_MASK  = 0xf00000,
	/*                         syslog()           Prefixes    */
	/*                         Severity  Whack  Object  Debug */
	ALL_STREAMS  = 0x000000, /* WARNING   yes    yes          */
	LOG_STREAM   = 0x100000, /* WARNING   no     yes          */
	DEBUG_STREAM = 0x200000, /*  DEBUG    no     <*>     "| " */
	WHACK_STREAM = 0x300000, /*   N/A     yes    yes          */
	ERROR_STREAM = 0x400000, /*   ERR     no     yes          */
	NO_STREAM    = 0xf00000, /*   N/A     N/A                 */
	/* <1> when enabled */
#define ERROR_FLAGS (ERROR_STREAM|RC_LOG_SERIOUS)
};

/*
 * Broadcast a log message.
 *
 * By default send it to the log file and any attached whacks (both
 * globally and the object).
 *
 * If any *_STREAM flag is specified then only send the message to
 * that stream.
 *
 * llog() is a catch-all for code that may or may not have ST.
 * For instance a responder decoding a message may not yet have
 * created the state.  It will will use ST, MD, or nothing as the
 * prefix, and logs to ST's whackfd when possible.
 */

struct logger_object_vec {
	const char *name;
	bool free_object;
	size_t (*jam_object_prefix)(struct jambuf *buf, const void *object);
#define jam_logger_prefix(BUF, LOGGER) (LOGGER)->object_vec->jam_object_prefix(BUF, (LOGGER)->object)
#define jam_logger_prefix_rc(BUF, LOGGER, RC_FLAGS)			\
	({								\
		if (((RC_FLAGS) & STREAM_MASK) != DEBUG_STREAM ||	\
		    DBGP(DBG_ADD_PREFIX)) {				\
			jam_logger_prefix(BUF, LOGGER);			\
		}							\
	})
	/*
	 * When opportunistic encryption or the initial responder, for
	 * instance, some logging is suppressed.
	 */
	bool (*suppress_object_log)(const void *object);
#define suppress_log(LOGGER) (LOGGER)->object_vec->suppress_object_log((LOGGER)->object)
};

struct logger {
	struct fd *global_whackfd;
	struct fd *object_whackfd;
	const void *object;
	const struct logger_object_vec *object_vec;
	where_t where;
	/* used by timing to nest its logging output */
	int timing_level;
};

void llog(lset_t rc_flags,
		 const struct logger *log,
		 const char *format, ...) PRINTF_LIKE(3);

void llog_va_list(lset_t rc_flags, const struct logger *logger,
		 const char *message, va_list ap);

void jambuf_to_logger(struct jambuf *buf, const struct logger *logger, lset_t rc_flags);

#define LLOG_JAMBUF(RC_FLAGS, LOGGER, BUF)				\
	JAMBUF(BUF)							\
		for (jam_logger_prefix_rc(BUF, LOGGER, RC_FLAGS);	\
		     BUF != NULL;					\
		     jambuf_to_logger(BUF, (LOGGER), RC_FLAGS), BUF = NULL)

/*
 * Fallback for debug and panic cases where making a logger available
 * is a pain (for instance deep inside code that shouldn't panic).
 *
 * XXX: Currently the error code, when the main thread, writes to
 * whack when available.  Long term it may not (it can't work when on
 * a thread).
 */

void jambuf_to_error_stream(struct jambuf *buf);
void jambuf_to_debug_stream(struct jambuf *buf);

/*
 * Wrap <message> in a prefix and suffix where the suffix contains
 * errno and message.
 *
 * Notes:
 *
 * Because __VA_ARGS__ may contain function calls that modify ERRNO,
 * errno's value is first saved.
 *
 * While these common-case macros are implemented as wrapper functions
 * so that backtrace will include the below function call and that
 * _includes_ the MESSAGE parameter - makes debugging much easier.
 */

void libreswan_exit(enum pluto_exit_code rc) NEVER_RETURNS;

/*
 * XXX: The message format is:
 *   ERROR: <log-prefix><message...>
 * and not:
 *   <log-prefix>ERROR: <message...>
 */
void log_error(struct logger *logger, const char *message, ...) PRINTF_LIKE(2);
#define log_errno(LOGGER, ERRNO, FMT, ...)				\
	{								\
		int e_ = ERRNO; /* save value across va args */		\
		log_error(LOGGER, FMT". "PRI_ERRNO,			\
			  ##__VA_ARGS__, pri_errno(e_));		\
	}

/*
 * XXX: The message format is:
 *   FATAL ERROR: <log-prefix><message...>
 * and not:
 *   <log-prefix>FATAL ERROR: <message...>
 */
void fatal(enum pluto_exit_code rc, struct logger *logger,
	   const char *message, ...) PRINTF_LIKE(3) NEVER_RETURNS;
#define fatal_errno(RC, LOGGER, ERRNO, FMT, ...)			\
	{								\
		int e_ = ERRNO; /* save value across va args */		\
		fatal(RC, LOGGER, FMT". "PRI_ERRNO,			\
		      ##__VA_ARGS__, pri_errno(e_));			\
	}

/*
 * E must have been saved!  Assume it is used as "... "PRI_ERRNO.
 *
 *   _Errno E: <strerror(E)>
 */
#define PRI_ERRNO "Errno %d: %s"
#define pri_errno(E) (E), strerror(E)

/*
 * Log debug messages to the main log stream, but not the WHACK log
 * stream.
 *
 * NOTE: DBG's action can be a { } block, but that block must not
 * contain commas that are outside quotes or parenthesis.
 * If it does, they will be interpreted by the C preprocesser
 * as macro argument separators.  This happens accidentally if
 * multiple variables are declared in one declaration.
 *
 * Naming: All DBG_*() prefixed functions send stuff to the debug
 * stream unconditionally.  Hence they should be wrapped in DBGP().
 */

extern lset_t cur_debugging;	/* current debugging level */

#define DBGP(cond)	(cur_debugging & (cond))

#define DEBUG_PREFIX "| "

#define DBG(cond, action)	{ if (DBGP(cond)) { action; } }
#define DBGF(COND, MESSAGE, ...) { if (DBGP(COND)) { DBG_log(MESSAGE,##__VA_ARGS__); } }
#define dbg(MESSAGE, ...) { if (DBGP(DBG_BASE)) { DBG_log(MESSAGE,##__VA_ARGS__); } }

/* DBG_*() are unconditional */
void DBG_log(const char *message, ...) PRINTF_LIKE(1);
void DBG_dump(const char *label, const void *p, size_t len);
#define DBG_dump_hunk(LABEL, HUNK)				\
	{							\
		typeof(HUNK) hunk_ = HUNK; /* evaluate once */	\
		DBG_dump(LABEL, hunk_.ptr, hunk_.len);		\
	}
#define DBG_dump_thing(LABEL, THING) DBG_dump(LABEL, &(THING), sizeof(THING))

/*
 * Code wrappers that cover up the details of allocating,
 * initializing, de-allocating (and possibly logging) a 'struct
 * lswlog' buffer.
 *
 * BUF (a C variable name) is declared locally as a pointer to a
 * per-thread 'struct jambuf' buffer.
 *
 * Implementation notes:
 *
 * This implementation stores the output in an array on the thread's
 * stack.  It could just as easily use the heap (but that would
 * involve memory overheads) or even a per-thread static variable.
 * Since the BUF variable is a pointer the specifics of the
 * implementation are hidden.
 *
 * This implementation, unlike DBG(), does not have a code block
 * parameter.  Instead it uses a sequence of for-loops to set things
 * up for a code block.  This avoids problems with "," within macro
 * parameters confusing the parser.  It also permits a simple
 * consistent indentation style.
 *
 * The stack array is left largely uninitialized (just a few strategic
 * entries are set).  This avoids the need to zero LOG_WITH bytes.
 *
 * Apparently chaining void function calls using a comma is valid C?
 */

/*
 * Scratch buffer for accumulating extra output.
 *
 * XXX: case should be expanded to illustrate how to stuff a truncated
 * version of the output into the LOG buffer.
 *
 * For instance:
 */

#if 0
void lswbuf(struct jambuf *log)
{
	LSWBUF(buf) {
		jam(buf, "written to buf");
		lswlogl(log, buf); /* add to calling array */
	}
}
#endif

/*
 * Log an expectation failure message to the error streams.  That is
 * the main log (level LOG_ERR) and whack log (level RC_LOG_SERIOUS).
 *
 * When evaluating ASSERTION, do not wrap it in parentheses as it will
 * suppress the warning for 'foo = bar'.
 *
 * Because static analizer tools are easily confused, explicitly
 * return the assertion result.
 */

#ifdef EXAMPLE
void lswlog_pexpect_example(void *p)
{
	if (!pexpect(p != NULL)) {
		return;
	}
}
#endif

#define pexpect(ASSERTION)						\
	({								\
		bool assertion__ = ASSERTION;				\
		if (!assertion__) {					\
			log_pexpect(HERE, "%s", #ASSERTION);		\
		}							\
		assertion__; /* result */				\
	})

void pexpect_fail(struct logger *logger, where_t where, const char *message, ...) PRINTF_LIKE(3);

void log_pexpect(where_t where, const char *message, ...) PRINTF_LIKE(2);

/* for a switch statement */

void libreswan_bad_case(const char *expression, long value, where_t where) NEVER_RETURNS;

#define bad_case(N)	libreswan_bad_case(#N, (N), HERE)

#define impaired_passert(BEHAVIOUR, LOGGER, ASSERTION)			\
	{								\
		if (impair.BEHAVIOUR) {					\
			bool assertion_ = ASSERTION;			\
			if (!assertion_) {				\
				llog(RC_LOG, LOGGER,		\
					    "IMPAIR: assertion '%s' failed", \
					    #ASSERTION);		\
			}						\
		} else {						\
			passert(ASSERTION);				\
		}							\
	}

#endif /* _LSWLOG_H_ */
