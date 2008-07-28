/*
 *  Copyright (c) 1993, Intergraph Corporation
 *
 *  You may distribute under the terms of either the GNU General Public
 *  License or the Artistic License, as specified in the perl README file.
 *
 *  Various Unix compatibility functions and NT specific functions.
 *
 *  Some of this code was derived from the MSDOS port(s) and the OS/2 port.
 *
 */

#include "ruby/ruby.h"
#include "ruby/signal.h"
#include "dln.h"
#include <fcntl.h>
#include <process.h>
#include <sys/stat.h>
/* #include <sys/wait.h> */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include <windows.h>
#include <winbase.h>
#include <wincon.h>
#include <share.h>
#include <shlobj.h>
#include <mbstring.h>
#ifdef __MINGW32__
#include <mswsock.h>
#endif
#include "ruby/win32.h"
#include "win32/dir.h"
#ifdef _WIN32_WCE
#include "wince.h"
#endif
#ifndef index
#define index(x, y) strchr((x), (y))
#endif
#define isdirsep(x) ((x) == '/' || (x) == '\\')

#undef stat
#undef fclose
#undef close
#undef setsockopt

#if defined __BORLANDC__ || defined _WIN32_WCE
#  define _filbuf _fgetc
#  define _flsbuf _fputc
#  define enough_to_get(n) (--(n) >= 0)
#  define enough_to_put(n) (++(n) < 0)
#else
#  define enough_to_get(n) (--(n) >= 0)
#  define enough_to_put(n) (--(n) >= 0)
#endif

#ifdef WIN32_DEBUG
#define Debug(something) something
#else
#define Debug(something) /* nothing */
#endif

#define TO_SOCKET(x)	_get_osfhandle(x)

static struct ChildRecord *CreateChild(const char *, const char *, SECURITY_ATTRIBUTES *, HANDLE, HANDLE, HANDLE);
static int has_redirection(const char *);
int rb_w32_wait_events(HANDLE *events, int num, DWORD timeout);
#if !defined(_WIN32_WCE)
static int rb_w32_open_osfhandle(intptr_t osfhandle, int flags);
#else
#define rb_w32_open_osfhandle(osfhandle, flags) _open_osfhandle(osfhandle, flags)
#endif

/* errno mapping */
static struct {
    DWORD winerr;
    int err;
} errmap[] = {
    {	ERROR_INVALID_FUNCTION,		EINVAL		},
    {	ERROR_FILE_NOT_FOUND,		ENOENT		},
    {	ERROR_PATH_NOT_FOUND,		ENOENT		},
    {	ERROR_TOO_MANY_OPEN_FILES,	EMFILE		},
    {	ERROR_ACCESS_DENIED,		EACCES		},
    {	ERROR_INVALID_HANDLE,		EBADF		},
    {	ERROR_ARENA_TRASHED,		ENOMEM		},
    {	ERROR_NOT_ENOUGH_MEMORY,	ENOMEM		},
    {	ERROR_INVALID_BLOCK,		ENOMEM		},
    {	ERROR_BAD_ENVIRONMENT,		E2BIG		},
    {	ERROR_BAD_FORMAT,		ENOEXEC		},
    {	ERROR_INVALID_ACCESS,		EINVAL		},
    {	ERROR_INVALID_DATA,		EINVAL		},
    {	ERROR_INVALID_DRIVE,		ENOENT		},
    {	ERROR_CURRENT_DIRECTORY,	EACCES		},
    {	ERROR_NOT_SAME_DEVICE,		EXDEV		},
    {	ERROR_NO_MORE_FILES,		ENOENT		},
    {	ERROR_WRITE_PROTECT,		EROFS		},
    {	ERROR_BAD_UNIT,			ENODEV		},
    {	ERROR_NOT_READY,		ENXIO		},
    {	ERROR_BAD_COMMAND,		EACCES		},
    {	ERROR_CRC,			EACCES		},
    {	ERROR_BAD_LENGTH,		EACCES		},
    {	ERROR_SEEK,			EIO		},
    {	ERROR_NOT_DOS_DISK,		EACCES		},
    {	ERROR_SECTOR_NOT_FOUND,		EACCES		},
    {	ERROR_OUT_OF_PAPER,		EACCES		},
    {	ERROR_WRITE_FAULT,		EIO		},
    {	ERROR_READ_FAULT,		EIO		},
    {	ERROR_GEN_FAILURE,		EACCES		},
    {	ERROR_LOCK_VIOLATION,		EACCES		},
    {	ERROR_SHARING_VIOLATION,	EACCES		},
    {	ERROR_WRONG_DISK,		EACCES		},
    {	ERROR_SHARING_BUFFER_EXCEEDED,	EACCES		},
    {	ERROR_BAD_NETPATH,		ENOENT		},
    {	ERROR_NETWORK_ACCESS_DENIED,	EACCES		},
    {	ERROR_BAD_NET_NAME,		ENOENT		},
    {	ERROR_FILE_EXISTS,		EEXIST		},
    {	ERROR_CANNOT_MAKE,		EACCES		},
    {	ERROR_FAIL_I24,			EACCES		},
    {	ERROR_INVALID_PARAMETER,	EINVAL		},
    {	ERROR_NO_PROC_SLOTS,		EAGAIN		},
    {	ERROR_DRIVE_LOCKED,		EACCES		},
    {	ERROR_BROKEN_PIPE,		EPIPE		},
    {	ERROR_DISK_FULL,		ENOSPC		},
    {	ERROR_INVALID_TARGET_HANDLE,	EBADF		},
    {	ERROR_INVALID_HANDLE,		EINVAL		},
    {	ERROR_WAIT_NO_CHILDREN,		ECHILD		},
    {	ERROR_CHILD_NOT_COMPLETE,	ECHILD		},
    {	ERROR_DIRECT_ACCESS_HANDLE,	EBADF		},
    {	ERROR_NEGATIVE_SEEK,		EINVAL		},
    {	ERROR_SEEK_ON_DEVICE,		EACCES		},
    {	ERROR_DIR_NOT_EMPTY,		ENOTEMPTY	},
    {	ERROR_DIRECTORY,		ENOTDIR		},
    {	ERROR_NOT_LOCKED,		EACCES		},
    {	ERROR_BAD_PATHNAME,		ENOENT		},
    {	ERROR_MAX_THRDS_REACHED,	EAGAIN		},
    {	ERROR_LOCK_FAILED,		EACCES		},
    {	ERROR_ALREADY_EXISTS,		EEXIST		},
    {	ERROR_INVALID_STARTING_CODESEG,	ENOEXEC		},
    {	ERROR_INVALID_STACKSEG,		ENOEXEC		},
    {	ERROR_INVALID_MODULETYPE,	ENOEXEC		},
    {	ERROR_INVALID_EXE_SIGNATURE,	ENOEXEC		},
    {	ERROR_EXE_MARKED_INVALID,	ENOEXEC		},
    {	ERROR_BAD_EXE_FORMAT,		ENOEXEC		},
    {	ERROR_ITERATED_DATA_EXCEEDS_64k,ENOEXEC		},
    {	ERROR_INVALID_MINALLOCSIZE,	ENOEXEC		},
    {	ERROR_DYNLINK_FROM_INVALID_RING,ENOEXEC		},
    {	ERROR_IOPL_NOT_ENABLED,		ENOEXEC		},
    {	ERROR_INVALID_SEGDPL,		ENOEXEC		},
    {	ERROR_AUTODATASEG_EXCEEDS_64k,	ENOEXEC		},
    {	ERROR_RING2SEG_MUST_BE_MOVABLE,	ENOEXEC		},
    {	ERROR_RELOC_CHAIN_XEEDS_SEGLIM,	ENOEXEC		},
    {	ERROR_INFLOOP_IN_RELOC_CHAIN,	ENOEXEC		},
    {	ERROR_FILENAME_EXCED_RANGE,	ENOENT		},
    {	ERROR_NESTING_NOT_ALLOWED,	EAGAIN		},
#ifndef ERROR_PIPE_LOCAL
#define ERROR_PIPE_LOCAL	229L
#endif
    {	ERROR_PIPE_LOCAL,		EPIPE		},
    {	ERROR_BAD_PIPE,			EPIPE		},
    {	ERROR_PIPE_BUSY,		EAGAIN		},
    {	ERROR_NO_DATA,			EPIPE		},
    {	ERROR_PIPE_NOT_CONNECTED,	EPIPE		},
    {	ERROR_OPERATION_ABORTED,	EINTR		},
    {	ERROR_NOT_ENOUGH_QUOTA,		ENOMEM		},
    {	WSAENAMETOOLONG,		ENAMETOOLONG	},
    {	WSAENOTEMPTY,			ENOTEMPTY	},
    {	WSAEINTR,			EINTR		},
    {	WSAEBADF,			EBADF		},
    {	WSAEACCES,			EACCES		},
    {	WSAEFAULT,			EFAULT		},
    {	WSAEINVAL,			EINVAL		},
    {	WSAEMFILE,			EMFILE		},
};

int
rb_w32_map_errno(DWORD winerr)
{
    int i;

    if (winerr == 0) {
	return 0;
    }

    for (i = 0; i < sizeof(errmap) / sizeof(*errmap); i++) {
	if (errmap[i].winerr == winerr) {
	    return errmap[i].err;
	}
    }

    if (winerr >= WSABASEERR) {
	return winerr;
    }
    return EINVAL;
}

#define map_errno rb_w32_map_errno

static const char *NTLoginName;

static OSVERSIONINFO osver;
#ifdef WIN95
static DWORD Win32System = (DWORD)-1;

DWORD
rb_w32_osid(void)
{
    if (osver.dwPlatformId != Win32System) {
	memset(&osver, 0, sizeof(OSVERSIONINFO));
	osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osver);
	Win32System = osver.dwPlatformId;
    }
    return (Win32System);
}
#endif
static DWORD Win32Version = (DWORD)-1;

static DWORD
rb_w32_osver(void)
{
    if (osver.dwMajorVersion != Win32Version) {
	memset(&osver, 0, sizeof(OSVERSIONINFO));
	osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osver);
	Win32Version = osver.dwMajorVersion;
    }
    return (Win32Version);
}

#define IsWinNT() rb_w32_iswinnt()
#define IsWin95() rb_w32_iswin95()

HANDLE
GetCurrentThreadHandle(void)
{
    static HANDLE current_process_handle = NULL;
    HANDLE h;

    if (!current_process_handle)
	current_process_handle = GetCurrentProcess();
    if (!DuplicateHandle(current_process_handle, GetCurrentThread(),
			 current_process_handle, &h,
			 0, FALSE, DUPLICATE_SAME_ACCESS))
	return NULL;
    return h;
}

/* simulate flock by locking a range on the file */


#define LK_ERR(f,i) \
    do {								\
	if (f)								\
	    i = 0;							\
	else {								\
	    DWORD err = GetLastError();					\
	    if (err == ERROR_LOCK_VIOLATION)				\
		errno = EWOULDBLOCK;					\
	    else if (err == ERROR_NOT_LOCKED)				\
		i = 0;							\
	    else							\
		errno = map_errno(err);					\
	}								\
    } while (0)
#define LK_LEN      ULONG_MAX

static uintptr_t
flock_winnt(uintptr_t self, int argc, uintptr_t* argv)
{
    OVERLAPPED o;
    int i = -1;
    const HANDLE fh = (HANDLE)self;
    const int oper = argc;

    memset(&o, 0, sizeof(o));

    switch(oper) {
      case LOCK_SH:		/* shared lock */
	LK_ERR(LockFileEx(fh, 0, 0, LK_LEN, LK_LEN, &o), i);
	break;
      case LOCK_EX:		/* exclusive lock */
	LK_ERR(LockFileEx(fh, LOCKFILE_EXCLUSIVE_LOCK, 0, LK_LEN, LK_LEN, &o), i);
	break;
      case LOCK_SH|LOCK_NB:	/* non-blocking shared lock */
	LK_ERR(LockFileEx(fh, LOCKFILE_FAIL_IMMEDIATELY, 0, LK_LEN, LK_LEN, &o), i);
	break;
      case LOCK_EX|LOCK_NB:	/* non-blocking exclusive lock */
	LK_ERR(LockFileEx(fh,
			  LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,
			  0, LK_LEN, LK_LEN, &o), i);
	break;
      case LOCK_UN:		/* unlock lock */
      case LOCK_UN|LOCK_NB:	/* unlock is always non-blocking, I hope */
	LK_ERR(UnlockFileEx(fh, 0, LK_LEN, LK_LEN, &o), i);
	break;
      default:            /* unknown */
	errno = EINVAL;
	break;
    }
    return i;
}

#ifdef WIN95
static uintptr_t
flock_win95(uintptr_t self, int argc, uintptr_t* argv)
{
    int i = -1;
    const HANDLE fh = (HANDLE)self;
    const int oper = argc;

    switch(oper) {
      case LOCK_EX:
	do {
	    LK_ERR(LockFile(fh, 0, 0, LK_LEN, LK_LEN), i);
	} while (i && errno == EWOULDBLOCK);
	break;
      case LOCK_EX|LOCK_NB:
	LK_ERR(LockFile(fh, 0, 0, LK_LEN, LK_LEN), i);
	break;
      case LOCK_UN:
      case LOCK_UN|LOCK_NB:
	LK_ERR(UnlockFile(fh, 0, 0, LK_LEN, LK_LEN), i);
	break;
      default:
	errno = EINVAL;
	break;
    }
    return i;
}
#endif

#undef LK_ERR

int
flock(int fd, int oper)
{
#ifdef WIN95
    static asynchronous_func_t locker = NULL;

    if (!locker) {
	if (IsWinNT())
	    locker = flock_winnt;
	else
	    locker = flock_win95;
    }
#else
    const asynchronous_func_t locker = flock_winnt;
#endif

    return rb_w32_asynchronize(locker,
			      (VALUE)_get_osfhandle(fd), oper, NULL,
			      (DWORD)-1);
}

static void
init_env(void)
{
    char env[_MAX_PATH];
    DWORD len;
    BOOL f;
    LPITEMIDLIST pidl;

    if (!GetEnvironmentVariable("HOME", env, sizeof(env))) {
	f = FALSE;
	if (GetEnvironmentVariable("HOMEDRIVE", env, sizeof(env)))
	    len = strlen(env);
	else
	    len = 0;
	if (GetEnvironmentVariable("HOMEPATH", env + len, sizeof(env) - len) || len) {
	    f = TRUE;
	}
	else if (GetEnvironmentVariable("USERPROFILE", env, sizeof(env))) {
	    f = TRUE;
	}
	else if (SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &pidl) == 0) {
	    LPMALLOC alloc;
	    f = SHGetPathFromIDList(pidl, env);
	    SHGetMalloc(&alloc);
	    alloc->lpVtbl->Free(alloc, pidl);
	    alloc->lpVtbl->Release(alloc);
	}
	if (f) {
	    char *p = env;
	    while (*p) {
		if (*p == '\\') *p = '/';
		p = CharNext(p);
	    }
	    if (p - env == 2 && env[1] == ':') {
		*p++ = '/';
		*p = 0;
	    }
	    SetEnvironmentVariable("HOME", env);
	}
    }

    if (!GetEnvironmentVariable("USER", env, sizeof env)) {
	if (GetEnvironmentVariable("USERNAME", env, sizeof env)) {
	    SetEnvironmentVariable("USER", env);
	}
	else if (!GetUserName(env, (len = sizeof env, &len))) {
	    NTLoginName = "<Unknown>";
	    return;
	}
    }
    NTLoginName = strdup(env);
}


typedef BOOL (WINAPI *cancel_io_t)(HANDLE);
static cancel_io_t cancel_io = NULL;

static void
init_func(void)
{
    if (!cancel_io)
	cancel_io = (cancel_io_t)GetProcAddress(GetModuleHandle("kernel32"),
						"CancelIo");
}

static void init_stdhandle(void);

#if _MSC_VER >= 1400
static void invalid_parameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file, unsigned int line, uintptr_t dummy)
{
    // nothing to do
}
#endif

static CRITICAL_SECTION select_mutex;
static int NtSocketsInitialized = 0;
static st_table *socklist = NULL;
static char *envarea;

static void
exit_handler(void)
{
    if (NtSocketsInitialized) {
	WSACleanup();
	st_free_table(socklist);
	socklist = NULL;
	NtSocketsInitialized = 0;
    }
    if (envarea) {
	FreeEnvironmentStrings(envarea);
	envarea = NULL;
    }
    DeleteCriticalSection(&select_mutex);
}

static void
StartSockets(void)
{
    WORD version;
    WSADATA retdata;

    //
    // initalize the winsock interface and insure that it's
    // cleaned up at exit.
    //
    version = MAKEWORD(2, 0);
    if (WSAStartup(version, &retdata))
	rb_fatal ("Unable to locate winsock library!\n");
    if (LOBYTE(retdata.wVersion) != 2)
	rb_fatal("could not find version 2 of winsock dll\n");

    socklist = st_init_numtable();

    NtSocketsInitialized = 1;
}

//
// Initialization stuff
//
void
rb_w32_sysinit(int *argc, char ***argv)
{
#if _MSC_VER >= 1400
    static void set_pioinfo_extra(void);

    _set_invalid_parameter_handler(invalid_parameter);
    set_pioinfo_extra();
#endif

    //
    // subvert cmd.exe's feeble attempt at command line parsing
    //
    *argc = rb_w32_cmdvector(GetCommandLine(), argv);

    //
    // Now set up the correct time stuff
    //

    tzset();

    init_env();

    init_func();

    init_stdhandle();

    InitializeCriticalSection(&select_mutex);

    atexit(exit_handler);

    // Initialize Winsock
    StartSockets();

#ifdef _WIN32_WCE
    // free commandline buffer
    wce_FreeCommandLine();
#endif
}

char *
getlogin(void)
{
    return (char *)NTLoginName;
}

#define MAXCHILDNUM 256	/* max num of child processes */

static struct ChildRecord {
    HANDLE hProcess;	/* process handle */
    rb_pid_t pid;	/* process id */
} ChildRecord[MAXCHILDNUM];

#define FOREACH_CHILD(v) do { \
    struct ChildRecord* v; \
    for (v = ChildRecord; v < ChildRecord + sizeof(ChildRecord) / sizeof(ChildRecord[0]); ++v)
#define END_FOREACH_CHILD } while (0)

static struct ChildRecord *
FindChildSlot(rb_pid_t pid)
{

    FOREACH_CHILD(child) {
	if (child->pid == pid) {
	    return child;
	}
    } END_FOREACH_CHILD;
    return NULL;
}

static void
CloseChildHandle(struct ChildRecord *child)
{
    HANDLE h = child->hProcess;
    child->hProcess = NULL;
    child->pid = 0;
    CloseHandle(h);
}

static struct ChildRecord *
FindFreeChildSlot(void)
{
    FOREACH_CHILD(child) {
	if (!child->pid) {
	    child->pid = -1;	/* lock the slot */
	    child->hProcess = NULL;
	    return child;
	}
    } END_FOREACH_CHILD;
    return NULL;
}


/*
  ruby -lne 'BEGIN{$cmds = Hash.new(0); $mask = 1}'
   -e '$cmds[$_.downcase] |= $mask' -e '$mask <<= 1 if ARGF.eof'
   -e 'END{$cmds.sort.each{|n,f|puts "    \"\\#{f.to_s(8)}\" #{n.dump} + 1,"}}'
   98cmd ntcmd
 */
static const char *const szInternalCmds[] = {
    "\2" "assoc" + 1,
    "\3" "break" + 1,
    "\3" "call" + 1,
    "\3" "cd" + 1,
    "\1" "chcp" + 1,
    "\3" "chdir" + 1,
    "\3" "cls" + 1,
    "\2" "color" + 1,
    "\3" "copy" + 1,
    "\1" "ctty" + 1,
    "\3" "date" + 1,
    "\3" "del" + 1,
    "\3" "dir" + 1,
    "\3" "echo" + 1,
    "\2" "endlocal" + 1,
    "\3" "erase" + 1,
    "\3" "exit" + 1,
    "\3" "for" + 1,
    "\2" "ftype" + 1,
    "\3" "goto" + 1,
    "\3" "if" + 1,
    "\1" "lfnfor" + 1,
    "\1" "lh" + 1,
    "\1" "lock" + 1,
    "\3" "md" + 1,
    "\3" "mkdir" + 1,
    "\2" "move" + 1,
    "\3" "path" + 1,
    "\3" "pause" + 1,
    "\2" "popd" + 1,
    "\3" "prompt" + 1,
    "\2" "pushd" + 1,
    "\3" "rd" + 1,
    "\3" "rem" + 1,
    "\3" "ren" + 1,
    "\3" "rename" + 1,
    "\3" "rmdir" + 1,
    "\3" "set" + 1,
    "\2" "setlocal" + 1,
    "\3" "shift" + 1,
    "\2" "start" + 1,
    "\3" "time" + 1,
    "\2" "title" + 1,
    "\1" "truename" + 1,
    "\3" "type" + 1,
    "\1" "unlock" + 1,
    "\3" "ver" + 1,
    "\3" "verify" + 1,
    "\3" "vol" + 1,
};

static int
internal_match(const void *key, const void *elem)
{
    return strcmp(key, *(const char *const *)elem);
}

static int
is_command_com(const char *interp)
{
    int i = strlen(interp) - 11;

    if ((i == 0 || i > 0 && isdirsep(interp[i-1])) &&
	strcasecmp(interp+i, "command.com") == 0) {
	return 1;
    }
    return 0;
}

static int
is_internal_cmd(const char *cmd, int nt)
{
    char cmdname[9], *b = cmdname, c, **nm;

    do {
	if (!(c = *cmd++)) return 0;
    } while (isspace(c));
    while (isalpha(c)) {
	*b++ = tolower(c);
	if (b == cmdname + sizeof(cmdname)) return 0;
	c = *cmd++;
    }
    if (c == '.') c = *cmd;
    switch (c) {
      case '<': case '>': case '|':
	return 1;
      case '\0': case ' ': case '\t': case '\n':
	break;
      default:
	return 0;
    }
    *b = 0;
    nm = bsearch(cmdname, szInternalCmds,
		 sizeof(szInternalCmds) / sizeof(*szInternalCmds),
		 sizeof(*szInternalCmds),
		 internal_match);
    if (!nm || !(nm[0][-1] & (nt ? 2 : 1)))
	return 0;
    return 1;
}

SOCKET
rb_w32_get_osfhandle(int fh)
{
    return _get_osfhandle(fh);
}

int
rb_w32_argv_size(char *const *argv)
{
    const char *p;
    char *const *t;
    int len, n, bs, quote;

    for (t = argv, len = 0; *t; t++) {
	for (p = *t, n = quote = bs = 0; *p; ++p) {
	    switch (*p) {
	      case '\\':
		++bs;
		break;
	      case '"':
		n += bs + 1; bs = 0;
		quote = 1;
		break;
	      case ' ': case '\t':
		quote = 1;
	      default:
		bs = 0;
		p = CharNext(p) - 1;
		break;
	    }
	}
	len += p - *t + n + 1;
	if (quote) len += 2;
    }
    return len;
}

char *
rb_w32_join_argv(char *cmd, char *const *argv)
{
    const char *p, *s;
    char *q, *const *t;
    int n, bs, quote;

    for (t = argv, q = cmd; p = *t; t++) {
	quote = 0;
	s = p;
	if (!*p || strpbrk(p, " \t\"")) {
	    quote = 1;
	    *q++ = '"';
	}
	for (bs = 0; *p; ++p) {
	    switch (*p) {
	      case '\\':
		++bs;
		break;
	      case '"':
		memcpy(q, s, n = p - s); q += n; s = p;
		memset(q, '\\', ++bs); q += bs; bs = 0;
		break;
	      default:
		bs = 0;
		p = CharNext(p) - 1;
		break;
	    }
	}
	memcpy(q, s, n = p - s);
	q += n;
	if (quote) *q++ = '"';
	*q++ = ' ';
    }
    if (q > cmd) --q;
    *q = '\0';
    return cmd;
}

rb_pid_t
rb_w32_pipe_exec(const char *cmd, const char *prog, int mode, int *pipe,
		 int *write_pipe)
{
    struct ChildRecord* child;
    HANDLE hIn, hOut;
    HANDLE hDupIn, hDupOut;
    HANDLE hCurProc;
    SECURITY_ATTRIBUTES sa;
    BOOL reading, writing;
    int ret;

    /* Figure out what we're doing... */
    if (mode & O_RDWR) {
	reading = writing = TRUE;
    }
    else if (mode & O_WRONLY) {
	reading = FALSE;
	writing = TRUE;
    }
    else {
	reading = TRUE;
	writing = FALSE;
    }
    mode &= ~(O_RDWR|O_RDONLY|O_WRONLY);
    if (!(mode & O_BINARY))
	mode |= O_TEXT;

    sa.nLength              = sizeof (SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;
    ret = -1;

    RUBY_CRITICAL(do {
	/* create pipe */
	hCurProc = GetCurrentProcess();
	hIn = hOut = hDupIn = hDupOut = NULL;
	if (reading) {
	    HANDLE hTmpIn;
	    if (!CreatePipe(&hTmpIn, &hOut, &sa, 2048L)) {
		errno = map_errno(GetLastError());
		break;
	    }
	    if (!DuplicateHandle(hCurProc, hTmpIn, hCurProc, &hDupIn, 0,
				 FALSE, DUPLICATE_SAME_ACCESS)) {
		errno = map_errno(GetLastError());
		CloseHandle(hTmpIn);
		CloseHandle(hOut);
		break;
	    }
	    CloseHandle(hTmpIn);
	    hTmpIn = NULL;
	}
	if (writing) {
	    HANDLE hTmpOut;
	    if (!CreatePipe(&hIn, &hTmpOut, &sa, 2048L)) {
		errno = map_errno(GetLastError());
		break;
	    }
	    if (!DuplicateHandle(hCurProc, hTmpOut, hCurProc, &hDupOut, 0,
				 FALSE, DUPLICATE_SAME_ACCESS)) {
		errno = map_errno(GetLastError());
		CloseHandle(hIn);
		CloseHandle(hTmpOut);
		break;
	    }
	    CloseHandle(hTmpOut);
	    hTmpOut = NULL;
	}

	/* create child process */
	child = CreateChild(cmd, prog, &sa, hIn, hOut, NULL);
	if (!child) {
	    if (hIn)
		CloseHandle(hIn);
	    if (hOut)
		CloseHandle(hOut);
	    if (hDupIn)
		CloseHandle(hDupIn);
	    if (hDupOut)
		CloseHandle(hDupOut);
	    break;
	}

	/* associate handle to file descritor */
	if (reading) {
	    *pipe = rb_w32_open_osfhandle((intptr_t)hDupIn, O_RDONLY | mode);
	    if (writing)
		*write_pipe = rb_w32_open_osfhandle((intptr_t)hDupOut,
						    O_WRONLY | mode);
	}
	else {
	    *pipe = rb_w32_open_osfhandle((intptr_t)hDupOut, O_WRONLY | mode);
	}
	if (hIn)
	    CloseHandle(hIn);
	if (hOut)
	    CloseHandle(hOut);
	if (reading && writing && *write_pipe == -1) {
	    if (*pipe != -1)
		rb_w32_close(*pipe);
	    else
		CloseHandle(hDupIn);
	    CloseHandle(hDupOut);
	    CloseChildHandle(child);
	    break;
	}
	else if (*pipe == -1) {
	    if (reading)
		CloseHandle(hDupIn);
	    else
		CloseHandle(hDupOut);
	    CloseChildHandle(child);
	    break;
	}

	ret = child->pid;
    } while (0));

    return ret;
}

rb_pid_t
rb_w32_spawn(int mode, const char *cmd, const char *prog)
{
    struct ChildRecord *child;
    DWORD exitcode;

    switch (mode) {
      case P_NOWAIT:
      case P_OVERLAY:
	break;
      default:
	errno = EINVAL;
	return -1;
    }

    child = CreateChild(cmd, prog, NULL, NULL, NULL, NULL);
    if (!child) {
	return -1;
    }

    switch (mode) {
      case P_NOWAIT:
	return child->pid;
      case P_OVERLAY:
	WaitForSingleObject(child->hProcess, INFINITE);
	GetExitCodeProcess(child->hProcess, &exitcode);
	CloseChildHandle(child);
	_exit(exitcode);
      default:
	return -1;	/* not reached */
    }
}

rb_pid_t
rb_w32_aspawn(int mode, const char *prog, char *const *argv)
{
    int len = rb_w32_argv_size(argv);
    char *cmd = ALLOCA_N(char, len);

    if (!prog) prog = argv[0];
    return rb_w32_spawn(mode, rb_w32_join_argv(cmd, argv), prog);
}

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#else
# define MAXPATHLEN 512
#endif

static struct ChildRecord *
CreateChild(const char *cmd, const char *prog, SECURITY_ATTRIBUTES *psa,
	    HANDLE hInput, HANDLE hOutput, HANDLE hError)
{
    BOOL fRet;
    DWORD  dwCreationFlags;
    STARTUPINFO aStartupInfo;
    PROCESS_INFORMATION aProcessInformation;
    SECURITY_ATTRIBUTES sa;
    const char *shell;
    struct ChildRecord *child;
    char *p = NULL;
    char fbuf[MAXPATHLEN];

    if (!cmd && !prog) {
	errno = EFAULT;
	return NULL;
    }

    child = FindFreeChildSlot();
    if (!child) {
	errno = EAGAIN;
	return NULL;
    }

    if (!psa) {
	sa.nLength              = sizeof (SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle       = TRUE;
	psa = &sa;
    }

    memset(&aStartupInfo, 0, sizeof (STARTUPINFO));
    memset(&aProcessInformation, 0, sizeof (PROCESS_INFORMATION));
    aStartupInfo.cb = sizeof (STARTUPINFO);
    if (hInput || hOutput || hError) {
	aStartupInfo.dwFlags = STARTF_USESTDHANDLES;
	if (hInput) {
	    aStartupInfo.hStdInput  = hInput;
	}
	else {
	    aStartupInfo.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	}
	if (hOutput) {
	    aStartupInfo.hStdOutput = hOutput;
	}
	else {
	    aStartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	if (hError) {
	    aStartupInfo.hStdError = hError;
	}
	else {
	    aStartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	}
    }

    dwCreationFlags = (NORMAL_PRIORITY_CLASS);

    if (prog) {
	if (!(p = dln_find_exe_r(prog, NULL, fbuf, sizeof(fbuf)))) {
	    shell = prog;
	}
    }
    else {
	int redir = -1;
	int len = 0;
	int nt;
	while (ISSPACE(*cmd)) cmd++;
	for (prog = cmd; *prog; prog = CharNext(prog)) {
	    if (ISSPACE(*prog)) {
		len = prog - cmd;
		do ++prog; while (ISSPACE(*prog));
		if (!*prog--) break;
	    }
	    else {
		len = 0;
	    }
	}
	if (!len) len = strlen(cmd);
	if ((shell = getenv("RUBYSHELL")) && (redir = has_redirection(cmd))) {
	    char *tmp = ALLOCA_N(char, strlen(shell) + len + sizeof(" -c ") + 2);
	    sprintf(tmp, "%s -c \"%.*s\"", shell, len, cmd);
	    cmd = tmp;
	}
	else if ((shell = getenv("COMSPEC")) &&
		 (nt = !is_command_com(shell),
		  (redir < 0 ? has_redirection(cmd) : redir) ||
		  is_internal_cmd(cmd, nt))) {
	    char *tmp = ALLOCA_N(char, strlen(shell) + len + sizeof(" /c ")
				 + (nt ? 2 : 0));
	    sprintf(tmp, nt ? "%s /c \"%.*s\"" : "%s /c %.*s", shell, len, cmd);
	    cmd = tmp;
	}
	else {
	    shell = NULL;
	    prog = cmd;
	    for (;;) {
		if (!*prog) {
		    p = dln_find_exe_r(cmd, NULL, fbuf, sizeof(fbuf));
		    break;
		}
		if (strchr(".:*?\"/\\", *prog)) {
		    if (cmd[len]) {
			char *tmp = ALLOCA_N(char, len + 1);
			memcpy(tmp, cmd, len);
			tmp[len] = 0;
			cmd = tmp;
		    }
		    break;
		}
		if (ISSPACE(*prog) || strchr("<>|", *prog)) {
		    len = prog - cmd;
		    p = ALLOCA_N(char, len + 1);
		    memcpy(p, cmd, len);
		    p[len] = 0;
		    p = dln_find_exe_r(p, NULL, fbuf, sizeof(fbuf));
		    break;
		}
		prog++;
	    }
	}
    }
    if (p) {
	shell = p;
	while (*p) {
	    if ((unsigned char)*p == '/')
		*p = '\\';
	    p = CharNext(p);
	}
    }

    RUBY_CRITICAL({
	fRet = CreateProcess(shell, (char *)cmd, psa, psa,
			     psa->bInheritHandle, dwCreationFlags, NULL, NULL,
			     &aStartupInfo, &aProcessInformation);
	errno = map_errno(GetLastError());
    });

    if (!fRet) {
	child->pid = 0;		/* release the slot */
	return NULL;
    }

    CloseHandle(aProcessInformation.hThread);

    child->hProcess = aProcessInformation.hProcess;
    child->pid = (rb_pid_t)aProcessInformation.dwProcessId;

    if (!IsWinNT()) {
	/* On Win9x, make pid positive similarly to cygwin and perl */
	child->pid = -child->pid;
    }

    return child;
}

typedef struct _NtCmdLineElement {
    struct _NtCmdLineElement *next;
    char *str;
    int len;
    int flags;
} NtCmdLineElement;

//
// Possible values for flags
//

#define NTGLOB   0x1	// element contains a wildcard
#define NTMALLOC 0x2	// string in element was malloc'ed
#define NTSTRING 0x4	// element contains a quoted string

static int
insert(const char *path, VALUE vinfo, void *enc)
{
    NtCmdLineElement *tmpcurr;
    NtCmdLineElement ***tail = (NtCmdLineElement ***)vinfo;

    tmpcurr = (NtCmdLineElement *)malloc(sizeof(NtCmdLineElement));
    if (!tmpcurr) return -1;
    MEMZERO(tmpcurr, NtCmdLineElement, 1);
    tmpcurr->len = strlen(path);
    tmpcurr->str = strdup(path);
    if (!tmpcurr->str) return -1;
    tmpcurr->flags |= NTMALLOC;
    **tail = tmpcurr;
    *tail = &tmpcurr->next;

    return 0;
}


static NtCmdLineElement **
cmdglob(NtCmdLineElement *patt, NtCmdLineElement **tail)
{
    char buffer[MAXPATHLEN], *buf = buffer;
    char *p;
    NtCmdLineElement **last = tail;
    int status;

    if (patt->len >= MAXPATHLEN)
	if (!(buf = malloc(patt->len + 1))) return 0;

    strlcpy(buf, patt->str, patt->len + 1);
    buf[patt->len] = '\0';
    for (p = buf; *p; p = CharNext(p))
	if (*p == '\\')
	    *p = '/';
    status = ruby_brace_glob(buf, 0, insert, (VALUE)&tail);
    if (buf != buffer)
	free(buf);

    if (status || last == tail) return 0;
    if (patt->flags & NTMALLOC)
	free(patt->str);
    free(patt);
    return tail;
}

// 
// Check a command string to determine if it has I/O redirection
// characters that require it to be executed by a command interpreter
//

static int
has_redirection(const char *cmd)
{
    char quote = '\0';
    const char *ptr;

    //
    // Scan the string, looking for redirection (< or >) or pipe 
    // characters (|) that are not in a quoted string
    //

    for (ptr = cmd; *ptr;) {
	switch (*ptr) {
	  case '\'':
	  case '\"':
	    if (!quote)
		quote = *ptr;
	    else if (quote == *ptr)
		quote = '\0';
	    ptr++;
	    break;

	  case '>':
	  case '<':
	  case '|':
	    if (!quote)
		return TRUE;
	    ptr++;
	    break;

	  case '\\':
	    ptr++;
	  default:
	    ptr = CharNext(ptr);
	    break;
	}
    }
    return FALSE;
}

static inline char *
skipspace(char *ptr)
{
    while (ISSPACE(*ptr))
	ptr++;
    return ptr;
}

int 
rb_w32_cmdvector(const char *cmd, char ***vec)
{
    int globbing, len;
    int elements, strsz, done;
    int slashes, escape;
    char *ptr, *base, *buffer, *cmdline;
    char **vptr;
    char quote;
    NtCmdLineElement *curr, **tail;
    NtCmdLineElement *cmdhead = NULL, **cmdtail = &cmdhead;

    //
    // just return if we don't have a command line
    //

    while (ISSPACE(*cmd))
	cmd++;
    if (!*cmd) {
	*vec = NULL;
	return 0;
    }

    ptr = cmdline = strdup(cmd);

    //
    // Ok, parse the command line, building a list of CmdLineElements.
    // When we've finished, and it's an input command (meaning that it's
    // the processes argv), we'll do globing and then build the argument 
    // vector.
    // The outer loop does one interation for each element seen. 
    // The inner loop does one interation for each character in the element.
    //

    while (*(ptr = skipspace(ptr))) {
	base = ptr;
	quote = slashes = globbing = escape = 0;
	for (done = 0; !done && *ptr; ) {
	    //
	    // Switch on the current character. We only care about the
	    // white-space characters, the  wild-card characters, and the
	    // quote characters.
	    //

	    switch (*ptr) {
	      case '\\':
		if (quote != '\'') slashes++;
	        break;

	      case ' ':
	      case '\t':
	      case '\n':
		//
		// if we're not in a string, then we're finished with this
		// element
		//

		if (!quote) {
		    *ptr = 0;
		    done = 1;
		}
		break;

	      case '*':
	      case '?':
	      case '[':
	      case '{':
		// 
		// record the fact that this element has a wildcard character
		// N.B. Don't glob if inside a single quoted string
		//

		if (quote != '\'')
		    globbing++;
		slashes = 0;
		break;

	      case '\'':
	      case '\"':
		//
		// if we're already in a string, see if this is the
		// terminating close-quote. If it is, we're finished with 
		// the string, but not neccessarily with the element.
		// If we're not already in a string, start one.
		//

		if (!(slashes & 1)) {
		    if (!quote)
			quote = *ptr;
		    else if (quote == *ptr) {
			if (quote == '"' && quote == ptr[1])
			    ptr++;
			quote = '\0';
		    }
		}
		escape++;
		slashes = 0;
		break;

	      default:
		ptr = CharNext(ptr);
		slashes = 0;
		continue;
	    }
	    ptr++;
	}

	//
	// when we get here, we've got a pair of pointers to the element,
	// base and ptr. Base points to the start of the element while ptr
	// points to the character following the element.
	//

	len = ptr - base;
	if (done) --len;

	//
	// if it's an input vector element and it's enclosed by quotes, 
	// we can remove them.
	//

	if (escape) {
	    char *p = base, c;
	    slashes = quote = 0;
	    while (p < base + len) {
		switch (c = *p) {
		  case '\\':
		    p++;
		    if (quote != '\'') slashes++;
		    break;

		  case '\'':
		  case '"':
		    if (!(slashes & 1) && quote && quote != c) {
			p++;
			slashes = 0;
			break;
		    }
		    memcpy(p - ((slashes + 1) >> 1), p + (~slashes & 1),
			   base + len - p);
		    len -= ((slashes + 1) >> 1) + (~slashes & 1);
		    p -= (slashes + 1) >> 1;
		    if (!(slashes & 1)) {
			if (quote) {
			    if (quote == '"' && quote == *p)
				p++;
			    quote = '\0';
			}
			else
			    quote = c;
		    }
		    else
			p++;
		    slashes = 0;
		    break;

		  default:
		    p = CharNext(p);
		    slashes = 0;
		    break;
		}
	    }
	}

	curr = (NtCmdLineElement *)calloc(sizeof(NtCmdLineElement), 1);
	if (!curr) goto do_nothing;
	curr->str = base;
	curr->len = len;

	if (globbing && (tail = cmdglob(curr, cmdtail))) {
	    cmdtail = tail;
	}
	else {
	    *cmdtail = curr;
	    cmdtail = &curr->next;
	}
    }

    //
    // Almost done! 
    // Count up the elements, then allocate space for a vector of pointers
    // (argv) and a string table for the elements.
    // 

    for (elements = 0, strsz = 0, curr = cmdhead; curr; curr = curr->next) {
	elements++;
	strsz += (curr->len + 1);
    }

    len = (elements+1)*sizeof(char *) + strsz;
    buffer = (char *)malloc(len);
    if (!buffer) {
      do_nothing:
	while (curr = cmdhead) {
	    cmdhead = curr->next;
	    if (curr->flags & NTMALLOC) free(curr->str);
	    free(curr);
	}
	free(cmdline);
	for (vptr = *vec; *vptr; ++vptr);
	return vptr - *vec;
    }
    
    //
    // make vptr point to the start of the buffer
    // and ptr point to the area we'll consider the string table.
    //
    //   buffer (*vec)
    //   |
    //   V       ^---------------------V
    //   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    //   |   |       | ....  | NULL  |   | ..... |\0 |   | ..... |\0 |...
    //   +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    //   |-  elements+1             -| ^ 1st element   ^ 2nd element

    vptr = (char **) buffer;

    ptr = buffer + (elements+1) * sizeof(char *);

    while (curr = cmdhead) {
	strlcpy(ptr, curr->str, curr->len + 1);
	*vptr++ = ptr;
	ptr += curr->len + 1;
	cmdhead = curr->next;
	if (curr->flags & NTMALLOC) free(curr->str);
	free(curr);
    }
    *vptr = 0;

    *vec = (char **) buffer;
    free(cmdline);
    return elements;
}

//
// UNIX compatible directory access functions for NT
//

#define PATHLEN 1024

//
// The idea here is to read all the directory names into a string table
// (separated by nulls) and when one of the other dir functions is called
// return the pointer to the current file name. 
//

#define GetBit(bits, i) ((bits)[(i) / CHAR_BIT] &  (1 << (i) % CHAR_BIT))
#define SetBit(bits, i) ((bits)[(i) / CHAR_BIT] |= (1 << (i) % CHAR_BIT))

#define BitOfIsDir(n) ((n) * 2)
#define BitOfIsRep(n) ((n) * 2 + 1)
#define DIRENT_PER_CHAR (CHAR_BIT / 2)

DIR *
rb_w32_opendir(const char *filename)
{
    DIR               *p;
    long               len;
    long               idx;
    char	      *scanname;
    char	      *tmp;
    struct stati64     sbuf;
    WIN32_FIND_DATA fd;
    HANDLE          fh;

    //
    // check to see if we've got a directory
    //
    if (rb_w32_stati64(filename, &sbuf) < 0)
	return NULL;
    if (!(sbuf.st_mode & S_IFDIR) &&
	(!ISALPHA(filename[0]) || filename[1] != ':' || filename[2] != '\0' ||
	((1 << (filename[0] & 0x5f) - 'A') & GetLogicalDrives()) == 0)) {
	errno = ENOTDIR;
	return NULL;
    }

    //
    // Get us a DIR structure
    //
    p = calloc(sizeof(DIR), 1);
    if (p == NULL)
	return NULL;

    //
    // Create the search pattern
    //
    len = strlen(filename) + 2 + 1;
    if (!(scanname = malloc(len))) {
	free(p);
	return NULL;
    }
    strlcpy(scanname, filename, len);

    if (index("/\\:", *CharPrev(scanname, scanname + strlen(scanname))) == NULL)
	strlcat(scanname, "/*", len);
    else
	strlcat(scanname, "*", len);

    //
    // do the FindFirstFile call
    //
    fh = FindFirstFile(scanname, &fd);
    free(scanname);
    if (fh == INVALID_HANDLE_VALUE) {
	errno = map_errno(GetLastError());
	free(p);
	return NULL;
    }

    idx = 0;

    //
    // loop finding all the files that match the wildcard
    // (which should be all of them in this directory!).
    // the variable idx should point one past the null terminator
    // of the previous string found.
    //
    do {
	len = strlen(fd.cFileName) + 1;

	//
	// bump the string table size by enough for the
	// new name and it's null terminator 
	//
	tmp = realloc(p->start, idx + len);
	if (!tmp) {
	  error:
	    rb_w32_closedir(p);
	    FindClose(fh);
	    errno = ENOMEM;
	    return NULL;
	}

	p->start = tmp;
	strlcpy(&p->start[idx], fd.cFileName, len);

	if (p->nfiles % DIRENT_PER_CHAR == 0) {
	    tmp = realloc(p->bits, p->nfiles / DIRENT_PER_CHAR + 1);
	    if (!tmp)
		goto error;
	    p->bits = tmp;
	    p->bits[p->nfiles / DIRENT_PER_CHAR] = 0;
	}
	if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	    SetBit(p->bits, BitOfIsDir(p->nfiles));
	if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
	    SetBit(p->bits, BitOfIsRep(p->nfiles));

	p->nfiles++;
	idx += len;
    } while (FindNextFile(fh, &fd));
    FindClose(fh);
    p->size = idx;
    p->curr = p->start;
    return p;
}

//
// Move to next entry
//

static void
move_to_next_entry(DIR *dirp)
{
    if (dirp->curr) {
	dirp->loc++;
	dirp->curr += strlen(dirp->curr) + 1;
	if (dirp->curr >= (dirp->start + dirp->size)) {
	    dirp->curr = NULL;
	}
    }
}

//
// Readdir just returns the current string pointer and bumps the
// string pointer to the next entry.
//

struct direct  *
rb_w32_readdir(DIR *dirp)
{
    static int  dummy = 0;

    if (dirp->curr) {

	//
	// first set up the structure to return
	//
	dirp->dirstr.d_namlen = strlen(dirp->curr);
	if (!(dirp->dirstr.d_name = malloc(dirp->dirstr.d_namlen + 1)))
	    return NULL;
	strlcpy(dirp->dirstr.d_name, dirp->curr, dirp->dirstr.d_namlen + 1);

	//
	// Fake inode
	//
	dirp->dirstr.d_ino = dummy++;

	//
	// Attributes
	//
	dirp->dirstr.d_isdir = GetBit(dirp->bits, BitOfIsDir(dirp->loc));
	dirp->dirstr.d_isrep = GetBit(dirp->bits, BitOfIsRep(dirp->loc));

	//
	// Now set up for the next call to readdir
	//

	move_to_next_entry(dirp);

	return &(dirp->dirstr);

    } else
	return NULL;
}

//
// Telldir returns the current string pointer position
//

off_t
rb_w32_telldir(DIR *dirp)
{
    return dirp->loc;
}

//
// Seekdir moves the string pointer to a previously saved position
// (Saved by telldir).

void
rb_w32_seekdir(DIR *dirp, off_t loc)
{
    rb_w32_rewinddir(dirp);

    while (dirp->curr && dirp->loc < loc) {
	move_to_next_entry(dirp);
    }
}

//
// Rewinddir resets the string pointer to the start
//

void
rb_w32_rewinddir(DIR *dirp)
{
    dirp->curr = dirp->start;
    dirp->loc = 0;
}

//
// This just free's the memory allocated by opendir
//

void
rb_w32_closedir(DIR *dirp)
{
    if (dirp) {
	if (dirp->dirstr.d_name)
	    free(dirp->dirstr.d_name);
	if (dirp->start)
	    free(dirp->start);
	if (dirp->bits)
	    free(dirp->bits);
	free(dirp);
    }
}

#if (defined _MT || defined __MSVCRT__) && !defined __BORLANDC__
#define MSVCRT_THREADS
#endif
#ifdef MSVCRT_THREADS
# define MTHREAD_ONLY(x) x
# define STHREAD_ONLY(x)
#elif defined(__BORLANDC__)
# define MTHREAD_ONLY(x)
# define STHREAD_ONLY(x)
#else
# define MTHREAD_ONLY(x)
# define STHREAD_ONLY(x) x
#endif

typedef struct	{
    intptr_t osfhnd;	/* underlying OS file HANDLE */
    char osfile;	/* attributes of file (e.g., open in text mode?) */
    char pipech;	/* one char buffer for handles opened on pipes */
#ifdef MSVCRT_THREADS
    int lockinitflag;
    CRITICAL_SECTION lock;
#endif
#if _MSC_VER >= 1400
    char textmode;
    char pipech2[2];
#endif
}	ioinfo;

#if !defined _CRTIMP || defined __MINGW32__
#undef _CRTIMP
#define _CRTIMP __declspec(dllimport)
#endif

#if !defined(__BORLANDC__) && !defined(_WIN32_WCE)
EXTERN_C _CRTIMP ioinfo * __pioinfo[];

#define IOINFO_L2E			5
#define IOINFO_ARRAY_ELTS	(1 << IOINFO_L2E)
#define _pioinfo(i)	((ioinfo*)((char*)(__pioinfo[i >> IOINFO_L2E]) + (i & (IOINFO_ARRAY_ELTS - 1)) * (sizeof(ioinfo) + pioinfo_extra)))
#define _osfhnd(i)  (_pioinfo(i)->osfhnd)
#define _osfile(i)  (_pioinfo(i)->osfile)
#define _pipech(i)  (_pioinfo(i)->pipech)

#if _MSC_VER >= 1400
static size_t pioinfo_extra = 0;	/* workaround for VC++8 SP1 */

static void
set_pioinfo_extra(void)
{
    int fd;

    fd = open("NUL", O_RDONLY);
    for (pioinfo_extra = 0; pioinfo_extra <= 64; pioinfo_extra += sizeof(void *)) {
	if (_osfhnd(fd) == _get_osfhandle(fd)) {
	    break;
	}
    }
    close(fd);

    if (pioinfo_extra > 64) {
	/* not found, maybe something wrong... */
	pioinfo_extra = 0;
    }
}
#else
#define pioinfo_extra 0
#endif

#define _set_osfhnd(fh, osfh) (void)(_osfhnd(fh) = osfh)
#define _set_osflags(fh, flags) (_osfile(fh) = (flags))

#define FOPEN			0x01	/* file handle open */
#define FNOINHERIT		0x10	/* file handle opened O_NOINHERIT */
#define FAPPEND			0x20	/* file handle opened O_APPEND */
#define FDEV			0x40	/* file handle refers to device */
#define FTEXT			0x80	/* file handle is in text mode */

static int
rb_w32_open_osfhandle(intptr_t osfhandle, int flags)
{
    int fh;
    char fileflags;		/* _osfile flags */
    HANDLE hF;

    /* copy relevant flags from second parameter */
    fileflags = FDEV;

    if (flags & O_APPEND)
	fileflags |= FAPPEND;

    if (flags & O_TEXT)
	fileflags |= FTEXT;

    if (flags & O_NOINHERIT)
	fileflags |= FNOINHERIT;

    /* attempt to allocate a C Runtime file handle */
    hF = CreateFile("NUL", 0, 0, NULL, OPEN_ALWAYS, 0, NULL);
    fh = _open_osfhandle((long)hF, 0);
    CloseHandle(hF);
    if (fh == -1) {
	errno = EMFILE;		/* too many open files */
	_doserrno = 0L;		/* not an OS error */
    }
    else {

	MTHREAD_ONLY(EnterCriticalSection(&(_pioinfo(fh)->lock)));
	/* the file is open. now, set the info in _osfhnd array */
	_set_osfhnd(fh, osfhandle);

	fileflags |= FOPEN;		/* mark as open */

	_set_osflags(fh, fileflags); /* set osfile entry */
	MTHREAD_ONLY(LeaveCriticalSection(&_pioinfo(fh)->lock));
    }
    return fh;			/* return handle */
}

static void
init_stdhandle(void)
{
    int nullfd = -1;
    int keep = 0;
#define open_null(fd)						\
    (((nullfd < 0) ?						\
      (nullfd = open("NUL", O_RDWR|O_BINARY)) : 0),		\
     ((nullfd == (fd)) ? (keep = 1) : dup2(nullfd, fd)),	\
     (fd))

    if (fileno(stdin) < 0) {
	stdin->_file = open_null(0);
    }
    if (fileno(stdout) < 0) {
	stdout->_file = open_null(1);
    }
    if (fileno(stderr) < 0) {
	stderr->_file = open_null(2);
    }
    if (nullfd >= 0 && !keep) close(nullfd);
    setvbuf(stderr, NULL, _IONBF, 0);
}
#else

#define _set_osfhnd(fh, osfh) (void)((fh), (osfh))
#define _set_osflags(fh, flags) (void)((fh), (flags))

static void
init_stdhandle(void)
{
}
#endif

#ifdef __BORLANDC__
static int
rb_w32_open_osfhandle(intptr_t osfhandle, int flags)
{
    int fd = _open_osfhandle(osfhandle, flags);
    if (fd == -1) {
	errno = EMFILE;		/* too many open files */
	_doserrno = 0L;		/* not an OS error */
    }
    return fd;
}
#endif

#undef getsockopt

static int
is_socket(SOCKET sock)
{
    if (st_lookup(socklist, (st_data_t)sock, NULL))
	return TRUE;
    else
	return FALSE;
}

int
rb_w32_is_socket(int fd)
{
    return is_socket(TO_SOCKET(fd));
}

//
// Since the errors returned by the socket error function 
// WSAGetLastError() are not known by the library routine strerror
// we have to roll our own.
//

#undef strerror

char *
rb_w32_strerror(int e)
{
    static char buffer[512];
#if !defined __MINGW32__
    extern int sys_nerr;
#endif
    DWORD source = 0;
    char *p;

#if defined __BORLANDC__ && defined ENOTEMPTY // _sys_errlist is broken
    switch (e) {
      case ENAMETOOLONG:
	return "Filename too long";
      case ENOTEMPTY:
	return "Directory not empty";
    }
#endif

    if (e < 0 || e > sys_nerr) {
	if (e < 0)
	    e = GetLastError();
	if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
			  FORMAT_MESSAGE_IGNORE_INSERTS, &source, e, 0,
			  buffer, sizeof(buffer), NULL) == 0)
	    strlcpy(buffer, "Unknown Error", sizeof(buffer));
    }
    else
	strlcpy(buffer, strerror(e), sizeof(buffer));

    p = buffer;
    while ((p = strpbrk(p, "\r\n")) != NULL) {
	memmove(p, p + 1, strlen(p));
    }
    return buffer;
}

//
// various stubs
//


// Ownership
//
// Just pretend that everyone is a superuser. NT will let us know if
// we don't really have permission to do something.
//

#define ROOT_UID	0
#define ROOT_GID	0

rb_uid_t
getuid(void)
{
	return ROOT_UID;
}

rb_uid_t
geteuid(void)
{
	return ROOT_UID;
}

rb_gid_t
getgid(void)
{
	return ROOT_GID;
}

rb_gid_t
getegid(void)
{
    return ROOT_GID;
}

int
setuid(rb_uid_t uid)
{ 
    return (uid == ROOT_UID ? 0 : -1);
}

int
setgid(rb_gid_t gid)
{
    return (gid == ROOT_GID ? 0 : -1);
}

//
// File system stuff
//

int
ioctl(int i, int u, ...)
{
    errno = EINVAL;
    return -1;
}

#undef FD_SET

void
rb_w32_fdset(int fd, fd_set *set)
{
    unsigned int i;
    SOCKET s = TO_SOCKET(fd);

    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == s) {
            return;
        }
    }
    if (i == set->fd_count) {
        if (set->fd_count < FD_SETSIZE) {
            set->fd_array[i] = s;
            set->fd_count++;
        }
    }
}

#undef FD_CLR

void
rb_w32_fdclr(int fd, fd_set *set)
{
    unsigned int i;
    SOCKET s = TO_SOCKET(fd);

    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == s) {
            while (i < set->fd_count - 1) {
                set->fd_array[i] = set->fd_array[i + 1];
                i++;
            }
            set->fd_count--;
            break;
        }
    }
}

#undef FD_ISSET

int
rb_w32_fdisset(int fd, fd_set *set)
{
    int ret;
    SOCKET s = TO_SOCKET(fd);
    if (s == (SOCKET)INVALID_HANDLE_VALUE)
        return 0;
    RUBY_CRITICAL(ret = __WSAFDIsSet(s, set));
    return ret;
}

//
// Networking trampolines
// These are used to avoid socket startup/shutdown overhead in case 
// the socket routines aren't used.
//

#undef select

static int
extract_fd(fd_set *dst, fd_set *src, int (*func)(SOCKET))
{
    int s = 0;
    if (!src || !dst) return 0;

    while (s < src->fd_count) {
        SOCKET fd = src->fd_array[s];

	if (!func || (*func)(fd)) { /* move it to dst */
	    int d;

	    for (d = 0; d < dst->fd_count; d++) {
		if (dst->fd_array[d] == fd) break;
	    }
	    if (d == dst->fd_count && dst->fd_count < FD_SETSIZE) {
		dst->fd_array[dst->fd_count++] = fd;
	    }
	    memmove(
		&src->fd_array[s],
		&src->fd_array[s+1], 
		sizeof(src->fd_array[0]) * (--src->fd_count - s));
	}
	else s++;
    }

    return dst->fd_count;
}

static int
is_not_socket(SOCKET sock)
{
    return !is_socket(sock);
}

static int
is_pipe(SOCKET sock) /* DONT call this for SOCKET! it clains it is PIPE. */
{
    int ret;

    RUBY_CRITICAL({
	ret = (GetFileType((HANDLE)sock) == FILE_TYPE_PIPE);
    });

    return ret;
}

static int
is_readable_pipe(SOCKET sock) /* call this for pipe only */
{
    int ret;
    DWORD n = 0;

    RUBY_CRITICAL(
	if (PeekNamedPipe((HANDLE)sock, NULL, 0, NULL, &n, NULL)) {
	    ret = (n > 0);
	}
	else {
	    ret = (GetLastError() == ERROR_BROKEN_PIPE); /* pipe was closed */
	}
    );

    return ret;
}

static int
is_console(SOCKET sock) /* DONT call this for SOCKET! */
{
    int ret;
    DWORD n = 0;
    INPUT_RECORD ir;

    RUBY_CRITICAL(
	ret = (PeekConsoleInput((HANDLE)sock, &ir, 1, &n))
    );

    return ret;
}

static int
is_readable_console(SOCKET sock) /* call this for console only */
{
    int ret = 0;
    DWORD n = 0;
    INPUT_RECORD ir;

    RUBY_CRITICAL(
	if (PeekConsoleInput((HANDLE)sock, &ir, 1, &n) && n > 0) {
	    if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown &&
		ir.Event.KeyEvent.uChar.AsciiChar) {
		ret = 1;
	    }
	    else {
		ReadConsoleInput((HANDLE)sock, &ir, 1, &n);
	    }
	}
    );

    return ret;
}

static int
do_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex,
            struct timeval *timeout)
{
    int r = 0;

    if (nfds == 0) {
	if (timeout)
	    rb_w32_sleep(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
	else
	    rb_w32_sleep(INFINITE);
    }
    else {
	RUBY_CRITICAL(
	    EnterCriticalSection(&select_mutex);
	    r = select(nfds, rd, wr, ex, timeout);
	    LeaveCriticalSection(&select_mutex);
	    if (r == SOCKET_ERROR) {
		errno = map_errno(WSAGetLastError());
		r = -1;
	    }
	);
    }

    return r;
}

static inline int
subst(struct timeval *rest, const struct timeval *wait)
{
    while (rest->tv_usec < wait->tv_usec) {
	if (rest->tv_sec <= wait->tv_sec) {
	    return 0;
	}
	rest->tv_sec -= 1;
	rest->tv_usec += 1000 * 1000;
    }
    rest->tv_sec -= wait->tv_sec;
    rest->tv_usec -= wait->tv_usec;
    return 1;
}

static inline int
compare(const struct timeval *t1, const struct timeval *t2)
{
    if (t1->tv_sec < t2->tv_sec)
	return -1;
    if (t1->tv_sec > t2->tv_sec)
	return 1;
    if (t1->tv_usec < t2->tv_usec)
	return -1;
    if (t1->tv_usec > t2->tv_usec)
	return 1;
    return 0;
}

#undef Sleep
int WSAAPI
rb_w32_select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex,
	       struct timeval *timeout)
{
    int r;
    fd_set pipe_rd;
    fd_set cons_rd;
    fd_set else_rd;
    fd_set else_wr;
    fd_set except;
    int nonsock = 0;

    if (nfds < 0 || (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0))) {
	errno = EINVAL;
	return -1;
    }
    if (!NtSocketsInitialized) {
	StartSockets();
    }

    // assume else_{rd,wr} (other than socket, pipe reader, console reader)
    // are always readable/writable. but this implementation still has
    // problem. if pipe's buffer is full, writing to pipe will block
    // until some data is read from pipe. but ruby is single threaded system,
    // so whole system will be blocked forever.

    else_rd.fd_count = 0;
    nonsock += extract_fd(&else_rd, rd, is_not_socket);

    pipe_rd.fd_count = 0;
    extract_fd(&pipe_rd, &else_rd, is_pipe); // should not call is_pipe for socket

    cons_rd.fd_count = 0;
    extract_fd(&cons_rd, &else_rd, is_console); // ditto

    else_wr.fd_count = 0;
    nonsock += extract_fd(&else_wr, wr, is_not_socket);

    except.fd_count = 0;
    extract_fd(&except, ex, is_not_socket); // drop only

    r = 0;
    if (rd && rd->fd_count > r) r = rd->fd_count;
    if (wr && wr->fd_count > r) r = wr->fd_count;
    if (ex && ex->fd_count > r) r = ex->fd_count;
    if (nfds > r) nfds = r;

    {
	struct timeval rest;
	struct timeval wait;
	struct timeval zero;
	if (timeout) rest = *timeout;
	wait.tv_sec = 0; wait.tv_usec = 10 * 1000; // 10ms
	zero.tv_sec = 0; zero.tv_usec = 0;         //  0ms
	do {
	    if (nonsock) {
		// modifying {else,pipe,cons}_rd is safe because
		// if they are modified, function returns immediately.
		extract_fd(&else_rd, &pipe_rd, is_readable_pipe);
		extract_fd(&else_rd, &cons_rd, is_readable_console);
	    }

	    if (else_rd.fd_count || else_wr.fd_count) {
		r = do_select(nfds, rd, wr, ex, &zero); // polling
		if (r < 0) break; // XXX: should I ignore error and return signaled handles?
		r += extract_fd(rd, &else_rd, NULL); // move all
		r += extract_fd(wr, &else_wr, NULL); // move all
		break;
	    }
	    else {
		struct timeval *dowait =
		    compare(&rest, &wait) < 0 ? &rest : &wait;

		fd_set orig_rd;
		fd_set orig_wr;
		fd_set orig_ex;
		if (rd) orig_rd = *rd;
		if (wr) orig_wr = *wr;
		if (ex) orig_ex = *ex;
		r = do_select(nfds, rd, wr, ex, &zero);	// polling
		if (r != 0) break; // signaled or error
		if (rd) *rd = orig_rd;
		if (wr) *wr = orig_wr;
		if (ex) *ex = orig_ex;

		// XXX: should check the time select spent
		Sleep(dowait->tv_sec * 1000 + dowait->tv_usec / 1000);
	    }
	} while (!timeout || subst(&rest, &wait));
    }

    return r;
}

#undef accept

int WSAAPI
rb_w32_accept(int s, struct sockaddr *addr, int *addrlen)
{
    SOCKET r;

    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = accept(TO_SOCKET(s), addr, addrlen);
	if (r == INVALID_SOCKET) {
	    errno = map_errno(WSAGetLastError());
	    s = -1;
	}
	else {
	    s = rb_w32_open_osfhandle(r, O_RDWR|O_BINARY|O_NOINHERIT);
	    if (s != -1)
		st_insert(socklist, (st_data_t)r, (st_data_t)0);
	    else
		closesocket(r);
	}
    });
    return s;
}

#undef bind

int WSAAPI
rb_w32_bind(int s, const struct sockaddr *addr, int addrlen)
{
    int r;

    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = bind(TO_SOCKET(s), addr, addrlen);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef connect

int WSAAPI
rb_w32_connect(int s, const struct sockaddr *addr, int addrlen)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = connect(TO_SOCKET(s), addr, addrlen);
	if (r == SOCKET_ERROR) {
	    r = WSAGetLastError();
	    if (r != WSAEWOULDBLOCK) {
		errno = map_errno(r);
	    }
	    else {
		errno = EINPROGRESS;
		r = -1;
	    }
	}
    });
    return r;
}


#undef getpeername

int WSAAPI
rb_w32_getpeername(int s, struct sockaddr *addr, int *addrlen)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getpeername(TO_SOCKET(s), addr, addrlen);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef getsockname

int WSAAPI
rb_w32_getsockname(int s, struct sockaddr *addr, int *addrlen)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getsockname(TO_SOCKET(s), addr, addrlen);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

int WSAAPI
rb_w32_getsockopt(int s, int level, int optname, char *optval, int *optlen)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getsockopt(TO_SOCKET(s), level, optname, optval, optlen);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef ioctlsocket

int WSAAPI
rb_w32_ioctlsocket(int s, long cmd, u_long *argp)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = ioctlsocket(TO_SOCKET(s), cmd, argp);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef listen

int WSAAPI
rb_w32_listen(int s, int backlog)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = listen(TO_SOCKET(s), backlog);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef recv
#undef recvfrom
#undef send
#undef sendto

static int
overlapped_socket_io(BOOL input, int fd, char *buf, int len, int flags,
		     struct sockaddr *addr, int *addrlen)
{
    int r;
    int ret;
    int mode;
    st_data_t data;
    DWORD flg;
    WSAOVERLAPPED wol;
    WSABUF wbuf;
    int err;
    SOCKET s;

    if (!NtSocketsInitialized)
	StartSockets();

    s = TO_SOCKET(fd);
    st_lookup(socklist, (st_data_t)s, &data);
    mode = (int)data;
    if (!cancel_io || (mode & O_NONBLOCK)) {
	RUBY_CRITICAL({
	    if (input) {
		if (addr && addrlen)
		    r = recvfrom(s, buf, len, flags, addr, addrlen);
		else
		    r = recv(s, buf, len, flags);
	    }
	    else {
		if (addr && addrlen)
		    r = sendto(s, buf, len, flags, addr, *addrlen);
		else
		    r = send(s, buf, len, flags);
	    }
	    if (r == SOCKET_ERROR)
		errno = map_errno(WSAGetLastError());
	});
    }
    else {
	DWORD size;
	wbuf.len = len;
	wbuf.buf = buf;
	memset(&wol, 0, sizeof(wol));
	RUBY_CRITICAL({
	    wol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	    if (input) {
		flg = flags;
		if (addr && addrlen)
		    ret = WSARecvFrom(s, &wbuf, 1, &size, &flg, addr, addrlen,
				      &wol, NULL);
		else
		    ret = WSARecv(s, &wbuf, 1, &size, &flg, &wol, NULL);
	    }
	    else {
		if (addr && addrlen)
		    ret = WSASendTo(s, &wbuf, 1, &size, flags, addr, *addrlen,
				    &wol, NULL);
		else
		    ret = WSASend(s, &wbuf, 1, &size, flags, &wol, NULL);
	    }
	});

	if (ret != SOCKET_ERROR) {
	    r = size;
	}
	else if ((err = WSAGetLastError()) == WSA_IO_PENDING) {
	    switch (rb_w32_wait_events_blocking(&wol.hEvent, 1, INFINITE)) {
	      case WAIT_OBJECT_0:
		RUBY_CRITICAL(
		    ret = WSAGetOverlappedResult(s, &wol, &size, TRUE, &flg)
		    );
		if (ret) {
		    r = size;
		    break;
		}
		/* thru */
	      default:
		errno = map_errno(err);
		/* thru */
	      case WAIT_OBJECT_0 + 1:
		/* interrupted */
		r = -1;
		cancel_io((HANDLE)s);
		break;
	    }
	}
	else {
	    errno = map_errno(err);
	    r = -1;
	}
	CloseHandle(&wol.hEvent);
    }

    return r;
}

int WSAAPI
rb_w32_recv(int fd, char *buf, int len, int flags)
{
    return overlapped_socket_io(TRUE, fd, buf, len, flags, NULL, NULL);
}

int WSAAPI
rb_w32_recvfrom(int fd, char *buf, int len, int flags,
		struct sockaddr *from, int *fromlen)
{
    return overlapped_socket_io(TRUE, fd, buf, len, flags, from, fromlen);
}

int WSAAPI
rb_w32_send(int fd, const char *buf, int len, int flags)
{
    return overlapped_socket_io(FALSE, fd, (char *)buf, len, flags, NULL, NULL);
}

int WSAAPI
rb_w32_sendto(int fd, const char *buf, int len, int flags, 
	      const struct sockaddr *to, int tolen)
{
    return overlapped_socket_io(FALSE, fd, (char *)buf, len, flags,
				(struct sockaddr *)to, &tolen);
}

#undef setsockopt

int WSAAPI
rb_w32_setsockopt(int s, int level, int optname, const char *optval, int optlen)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = setsockopt(TO_SOCKET(s), level, optname, optval, optlen);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}
    
#undef shutdown

int WSAAPI
rb_w32_shutdown(int s, int how)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = shutdown(TO_SOCKET(s), how);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

static SOCKET
open_ifs_socket(int af, int type, int protocol)
{
    unsigned long proto_buffers_len = 0;
    int error_code;
    SOCKET out = INVALID_SOCKET;

    if (WSAEnumProtocols(NULL, NULL, &proto_buffers_len) == SOCKET_ERROR) {
	error_code = WSAGetLastError();
	if (error_code == WSAENOBUFS) {
	    WSAPROTOCOL_INFO *proto_buffers;
	    int protocols_available = 0;

	    proto_buffers = (WSAPROTOCOL_INFO *)malloc(proto_buffers_len);
	    if (!proto_buffers) {
		WSASetLastError(WSA_NOT_ENOUGH_MEMORY);
		return INVALID_SOCKET;
	    }

	    protocols_available =
		WSAEnumProtocols(NULL, proto_buffers, &proto_buffers_len);
	    if (protocols_available != SOCKET_ERROR) {
		int i;
		for (i = 0; i < protocols_available; i++) {
		    if ((af != AF_UNSPEC && af != proto_buffers[i].iAddressFamily) ||
			(type != proto_buffers[i].iSocketType) ||
			(protocol != 0 && protocol != proto_buffers[i].iProtocol))
			continue;

		    if ((proto_buffers[i].dwServiceFlags1 & XP1_IFS_HANDLES) == 0)
			continue;

		    out = WSASocket(af, type, protocol, &(proto_buffers[i]), 0,
				    WSA_FLAG_OVERLAPPED);
		    break;
		}
	    }

	    free(proto_buffers);
	}
    }

    return out;
}

#undef socket

int WSAAPI
rb_w32_socket(int af, int type, int protocol)
{
    SOCKET s;
    int fd;

    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	s = open_ifs_socket(af, type, protocol);
	if (s == INVALID_SOCKET) {
	    errno = map_errno(WSAGetLastError());
	    fd = -1;
	}
	else {
	    fd = rb_w32_open_osfhandle(s, O_RDWR|O_BINARY|O_NOINHERIT);
	    if (fd != -1)
		st_insert(socklist, (st_data_t)s, (st_data_t)0);
	    else
		closesocket(s);
	}
    });
    return fd;
}

#undef gethostbyaddr

struct hostent * WSAAPI
rb_w32_gethostbyaddr(const char *addr, int len, int type)
{
    struct hostent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = gethostbyaddr(addr, len, type);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef gethostbyname

struct hostent * WSAAPI
rb_w32_gethostbyname(const char *name)
{
    struct hostent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = gethostbyname(name);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef gethostname

int WSAAPI
rb_w32_gethostname(char *name, int len)
{
    int r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = gethostname(name, len);
	if (r == SOCKET_ERROR)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef getprotobyname

struct protoent * WSAAPI
rb_w32_getprotobyname(const char *name)
{
    struct protoent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getprotobyname(name);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef getprotobynumber

struct protoent * WSAAPI
rb_w32_getprotobynumber(int num)
{
    struct protoent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getprotobynumber(num);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef getservbyname

struct servent * WSAAPI
rb_w32_getservbyname(const char *name, const char *proto)
{
    struct servent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getservbyname(name, proto);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

#undef getservbyport

struct servent * WSAAPI
rb_w32_getservbyport(int port, const char *proto)
{
    struct servent *r;
    if (!NtSocketsInitialized) {
	StartSockets();
    }
    RUBY_CRITICAL({
	r = getservbyport(port, proto);
	if (r == NULL)
	    errno = map_errno(WSAGetLastError());
    });
    return r;
}

static int
socketpair_internal(int af, int type, int protocol, SOCKET *sv)
{
    SOCKET svr = INVALID_SOCKET, r = INVALID_SOCKET, w = INVALID_SOCKET;
    struct sockaddr_in sock_in4;
#ifdef INET6
    struct sockaddr_in6 sock_in6;
#endif
    struct sockaddr *addr;
    int ret = -1;
    int len;

    if (!NtSocketsInitialized) {
	StartSockets();
    }

    switch (af) {
      case AF_INET:
#if defined PF_INET && PF_INET != AF_INET
      case PF_INET:
#endif
	sock_in4.sin_family = AF_INET;
	sock_in4.sin_port = 0;
	sock_in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr = (struct sockaddr *)&sock_in4;
	len = sizeof(sock_in4);
	break;
#ifdef INET6
      case AF_INET6:
	memset(&sock_in6, 0, sizeof(sock_in6));
	sock_in6.sin6_family = AF_INET6;
	sock_in6.sin6_addr = IN6ADDR_LOOPBACK_INIT;
	addr = (struct sockaddr *)&sock_in6;
	len = sizeof(sock_in6);
	break;
#endif
      default:
	errno = EAFNOSUPPORT;
	return -1;
    }
    if (type != SOCK_STREAM) {
	errno = EPROTOTYPE;
	return -1;
    }

    RUBY_CRITICAL({
	do {
	    svr = open_ifs_socket(af, type, protocol);
	    if (svr == INVALID_SOCKET)
		break;
	    if (bind(svr, addr, len) < 0)
		break;
	    if (getsockname(svr, addr, &len) < 0)
		break;
	    if (type == SOCK_STREAM)
		listen(svr, 5);

	    w = open_ifs_socket(af, type, protocol);
	    if (w == INVALID_SOCKET)
		break;
	    if (connect(w, addr, len) < 0)
		break;

	    r = accept(svr, addr, &len);
	    if (r == INVALID_SOCKET)
		break;

	    ret = 0;
	} while (0);

	if (ret < 0) {
	    errno = map_errno(WSAGetLastError());
	    if (r != INVALID_SOCKET)
		closesocket(r);
	    if (w != INVALID_SOCKET)
		closesocket(w);
	}
	else {
	    sv[0] = r;
	    sv[1] = w;
	}
	if (svr != INVALID_SOCKET)
	    closesocket(svr);
    });

    return ret;
}

int
rb_w32_socketpair(int af, int type, int protocol, int *sv)
{
    SOCKET pair[2];

    if (socketpair_internal(af, type, protocol, pair) < 0)
	return -1;
    sv[0] = rb_w32_open_osfhandle(pair[0], O_RDWR|O_BINARY|O_NOINHERIT);
    if (sv[0] == -1) {
	closesocket(pair[0]);
	closesocket(pair[1]);
	return -1;
    }
    sv[1] = rb_w32_open_osfhandle(pair[1], O_RDWR|O_BINARY|O_NOINHERIT);
    if (sv[1] == -1) {
	rb_w32_close(sv[0]);
	closesocket(pair[1]);
	return -1;
    }
    st_insert(socklist, (st_data_t)pair[0], (st_data_t)0);
    st_insert(socklist, (st_data_t)pair[1], (st_data_t)0);

    return 0;
}

//
// Networking stubs
//

void endhostent(void) {}
void endnetent(void) {}
void endprotoent(void) {}
void endservent(void) {}

struct netent *getnetent (void) {return (struct netent *) NULL;}

struct netent *getnetbyaddr(long net, int type) {return (struct netent *)NULL;}

struct netent *getnetbyname(const char *name) {return (struct netent *)NULL;}

struct protoent *getprotoent (void) {return (struct protoent *) NULL;}

struct servent *getservent (void) {return (struct servent *) NULL;}

void sethostent (int stayopen) {}

void setnetent (int stayopen) {}

void setprotoent (int stayopen) {}

void setservent (int stayopen) {}

int
fcntl(int fd, int cmd, ...)
{
    SOCKET sock = TO_SOCKET(fd);
    va_list va;
    int arg;
    int ret;
    int flag = 0;
    st_data_t data;
    u_long ioctlArg;

    if (!is_socket(sock)) {
	errno = EBADF;
	return -1;
    }
    if (cmd != F_SETFL) {
	errno = EINVAL;
	return -1;
    }

    va_start(va, cmd);
    arg = va_arg(va, int);
    va_end(va);
    st_lookup(socklist, (st_data_t)sock, &data);
    flag = (int)data;
    if (arg & O_NONBLOCK) {
	flag |= O_NONBLOCK;
	ioctlArg = 1;
    }
    else {
	flag &= ~O_NONBLOCK;
	ioctlArg = 0;
    }
    RUBY_CRITICAL({
	ret = ioctlsocket(sock, FIONBIO, &ioctlArg);
	if (ret == 0)
	    st_insert(socklist, (st_data_t)sock, (st_data_t)flag);
	else
	    errno = map_errno(WSAGetLastError());
    });

    return ret;
}

#ifndef WNOHANG
#define WNOHANG -1
#endif

static rb_pid_t
poll_child_status(struct ChildRecord *child, int *stat_loc)
{
    DWORD exitcode;
    DWORD err;

    if (!GetExitCodeProcess(child->hProcess, &exitcode)) {
	/* If an error occured, return immediatly. */
	err = GetLastError();
	if (err == ERROR_INVALID_PARAMETER)
	    errno = ECHILD;
	else {
	    if (GetLastError() == ERROR_INVALID_HANDLE)
		errno = EINVAL;
	    else
		errno = map_errno(GetLastError());
	}
	CloseChildHandle(child);
	return -1;
    }
    if (exitcode != STILL_ACTIVE) {
	/* If already died, return immediatly. */
	rb_pid_t pid = child->pid;
	CloseChildHandle(child);
	if (stat_loc) *stat_loc = exitcode << 8;
	return pid;
    }
    return 0;
}

rb_pid_t
waitpid(rb_pid_t pid, int *stat_loc, int options)
{
    DWORD timeout;

    if (options == WNOHANG) {
	timeout = 0;
    } else {
	timeout = INFINITE;
    }

    if (pid == -1) {
	int count = 0;
	DWORD ret;
	HANDLE events[MAXCHILDNUM];

	FOREACH_CHILD(child) {
	    if (!child->pid || child->pid < 0) continue;
	    if ((pid = poll_child_status(child, stat_loc))) return pid;
	    events[count++] = child->hProcess;
	} END_FOREACH_CHILD;
	if (!count) {
	    errno = ECHILD;
	    return -1;
	}

	ret = rb_w32_wait_events_blocking(events, count, timeout);
	if (ret == WAIT_TIMEOUT) return 0;
	if ((ret -= WAIT_OBJECT_0) == count) {
	    return -1;
	}
	if (ret > count) {
	    errno = map_errno(GetLastError());
	    return -1;
	}

	return poll_child_status(ChildRecord + ret, stat_loc);
    }
    else {
	struct ChildRecord* child = FindChildSlot(pid);
	if (!child) {
	    errno = ECHILD;
	    return -1;
	}

	while (!(pid = poll_child_status(child, stat_loc))) {
	    /* wait... */
	    if (rb_w32_wait_events_blocking(&child->hProcess, 1, timeout) != WAIT_OBJECT_0) {
		/* still active */
		pid = 0;
		break;
	    }
	}
    }

    return pid;
}

#include <sys/timeb.h>

int _cdecl
gettimeofday(struct timeval *tv, struct timezone *tz)
{
    SYSTEMTIME st;
    time_t t;
    struct tm tm;

    GetLocalTime(&st);
    tm.tm_sec = st.wSecond;
    tm.tm_min = st.wMinute;
    tm.tm_hour = st.wHour;
    tm.tm_mday = st.wDay;
    tm.tm_mon = st.wMonth - 1;
    tm.tm_year = st.wYear - 1900;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    tv->tv_sec = t;
    tv->tv_usec = st.wMilliseconds * 1000;

    return 0;
}

char *
rb_w32_getcwd(char *buffer, int size)
{
    char *p = buffer;
    char *bp;
    int len;

    len = GetCurrentDirectory(0, NULL);
    if (!len) {
	errno = map_errno(GetLastError());
	return NULL;
    }

    if (p) {
	if (size < len) {
	    errno = ERANGE;
	    return NULL;
	}
    }
    else {
	p = malloc(len);
	size = len;
	if (!p) {
	    errno = ENOMEM;
        return NULL;
    }
    }

    if (!GetCurrentDirectory(size, p)) {
	errno = map_errno(GetLastError());
	if (!buffer)
	    free(p);
        return NULL;
    }

    for (bp = p; *bp != '\0'; bp = CharNext(bp)) {
	if (*bp == '\\') {
	    *bp = '/';
	}
    }

    return p;
}

int
chown(const char *path, int owner, int group)
{
    return 0;
}

int
kill(int pid, int sig)
{
    int ret = 0;
    DWORD err;

    if (pid <= 0) {
	errno = EINVAL;
	return -1;
    }

    if (IsWin95()) pid = -pid;
    if ((unsigned int)pid == GetCurrentProcessId() &&
	(sig != 0 && sig != SIGKILL)) {
	if ((ret = raise(sig)) != 0) {
	    /* MSVCRT doesn't set errno... */
	    errno = EINVAL;
	}
	return ret;
    }

    switch (sig) {
      case 0:
	RUBY_CRITICAL({
	    HANDLE hProc =
		OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
	    if (hProc == NULL || hProc == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_INVALID_PARAMETER) {
		    errno = ESRCH;
		}
		else {
		    errno = EPERM;
		}
		ret = -1;
	    }
	    else {
		CloseHandle(hProc);
	    }
	});
	break;

      case SIGINT:
	RUBY_CRITICAL({
	    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, (DWORD)pid)) {
		if ((err = GetLastError()) == 0)
		    errno = EPERM;
		else
		    errno = map_errno(GetLastError());
		ret = -1;
	    }
	});
	break;

      case SIGKILL:
	RUBY_CRITICAL({
	    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
	    if (hProc == NULL || hProc == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_INVALID_PARAMETER) {
		    errno = ESRCH;
		}
		else {
		    errno = EPERM;
		}
		ret = -1;
	    }
	    else {
		if (!TerminateProcess(hProc, 0)) {
		    errno = EPERM;
		    ret = -1;
		}
		CloseHandle(hProc);
	    }
	});
	break;

      default:
	errno = EINVAL;
	ret = -1;
	break;
    }

    return ret;
}

int
link(const char *from, const char *to)
{
    static BOOL (WINAPI *pCreateHardLink)(LPCTSTR, LPCTSTR, LPSECURITY_ATTRIBUTES) = NULL;
    static int myerrno = 0;

    if (!pCreateHardLink && !myerrno) {
	HANDLE hKernel;

	hKernel = GetModuleHandle("kernel32.dll");
	if (hKernel) {
	    pCreateHardLink = (BOOL (WINAPI *)(LPCTSTR, LPCTSTR, LPSECURITY_ATTRIBUTES))GetProcAddress(hKernel, "CreateHardLinkA");
	    if (!pCreateHardLink) {
		rb_notimplement();
	    }
	}
	else {
	    myerrno = map_errno(GetLastError());
	}
    }
    if (!pCreateHardLink) {
	errno = myerrno;
	return -1;
    }

    if (!pCreateHardLink(to, from, NULL)) {
	errno = map_errno(GetLastError());
	return -1;
    }

    return 0;
}

int
wait(int *status)
{
    return waitpid(-1, status, 0);
}

char *
rb_w32_getenv(const char *name)
{
    int len = strlen(name);
    char *env;

    if (envarea)
	FreeEnvironmentStrings(envarea);
    envarea = GetEnvironmentStrings();
    if (!envarea) {
	map_errno(GetLastError());
	return NULL;
    }

    for (env = envarea; *env; env += strlen(env) + 1)
	if (strncasecmp(env, name, len) == 0 && *(env + len) == '=')
	    return env + len + 1;

    return NULL;
}

int
rb_w32_rename(const char *oldpath, const char *newpath)
{
    int res = 0;
    int oldatts;
    int newatts;

    oldatts = GetFileAttributes(oldpath);
    newatts = GetFileAttributes(newpath);

    if (oldatts == -1) {
	errno = map_errno(GetLastError());
	return -1;
    }

    RUBY_CRITICAL({
	if (newatts != -1 && newatts & FILE_ATTRIBUTE_READONLY)
	    SetFileAttributesA(newpath, newatts & ~ FILE_ATTRIBUTE_READONLY);

	if (!MoveFile(oldpath, newpath))
	    res = -1;

	if (res) {
	    switch (GetLastError()) {
	      case ERROR_ALREADY_EXISTS:
	      case ERROR_FILE_EXISTS:
		if (IsWinNT()) {
		    if (MoveFileEx(oldpath, newpath, MOVEFILE_REPLACE_EXISTING))
			res = 0;
		} else {
		    for (;;) {
			if (!DeleteFile(newpath) && GetLastError() != ERROR_FILE_NOT_FOUND)
			    break;
			else if (MoveFile(oldpath, newpath)) {
			    res = 0;
			    break;
			}
		    }
		}
	    }
	}

	if (res)
	    errno = map_errno(GetLastError());
	else
	    SetFileAttributes(newpath, oldatts);
    });

    return res;
}

static int
isUNCRoot(const char *path)
{
    if (path[0] == '\\' && path[1] == '\\') {
	const char *p;
	for (p = path + 2; *p; p = CharNext(p)) {
	    if (*p == '\\')
		break;
	}
	if (p[0] && p[1]) {
	    for (p++; *p; p = CharNext(p)) {
		if (*p == '\\')
		    break;
	    }
	    if (!p[0] || !p[1] || (p[1] == '.' && !p[2]))
		return 1;
	}
    }
    return 0;
}

#define COPY_STAT(src, dest) do {		\
	(dest).st_dev 	= (src).st_dev;		\
	(dest).st_ino 	= (src).st_ino;		\
	(dest).st_mode  = (src).st_mode;	\
	(dest).st_nlink = (src).st_nlink;	\
	(dest).st_uid   = (src).st_uid;		\
	(dest).st_gid   = (src).st_gid;		\
	(dest).st_rdev 	= (src).st_rdev;	\
	(dest).st_size 	= (src).st_size;	\
	(dest).st_atime = (src).st_atime;	\
	(dest).st_mtime = (src).st_mtime;	\
	(dest).st_ctime = (src).st_ctime;	\
    } while (0)

#ifdef __BORLANDC__
#undef fstat
int
rb_w32_fstat(int fd, struct stat *st)
{
    BY_HANDLE_FILE_INFORMATION info;
    int ret = fstat(fd, st);

    if (ret) return ret;
    st->st_mode &= ~(S_IWGRP | S_IWOTH);
    if (GetFileInformationByHandle((HANDLE)_get_osfhandle(fd), &info) &&
	!(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
	st->st_mode |= S_IWUSR;
    }
    return ret;
}

int
rb_w32_fstati64(int fd, struct stati64 *st)
{
    BY_HANDLE_FILE_INFORMATION info;
    struct stat tmp;
    int ret = fstat(fd, &tmp);

    if (ret) return ret;
    tmp.st_mode &= ~(S_IWGRP | S_IWOTH);
    COPY_STAT(tmp, *st);
    if (GetFileInformationByHandle((HANDLE)_get_osfhandle(fd), &info)) {
	if (!(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
	    st->st_mode |= S_IWUSR;
	}
	st->st_size = ((__int64)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    }
    return ret;
}
#endif

static time_t
filetime_to_unixtime(const FILETIME *ft)
{
    FILETIME loc;
    SYSTEMTIME st;
    struct tm tm;
    time_t t;

    if (!FileTimeToLocalFileTime(ft, &loc)) {
	return 0;
    }
    if (!FileTimeToSystemTime(&loc, &st)) {
	return 0;
    }
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = st.wYear - 1900;
    tm.tm_mon = st.wMonth - 1;
    tm.tm_mday = st.wDay;
    tm.tm_hour = st.wHour;
    tm.tm_min = st.wMinute;
    tm.tm_sec = st.wSecond;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    return t == -1 ? 0 : t;
}

static unsigned
fileattr_to_unixmode(DWORD attr, const char *path)
{
    unsigned mode = 0;

    if (attr & FILE_ATTRIBUTE_READONLY) {
	mode |= S_IREAD;
    }
    else {
	mode |= S_IREAD | S_IWRITE | S_IWUSR;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
	mode |= S_IFDIR | S_IEXEC;
    }
    else {
	mode |= S_IFREG;
    }

    if (path && (mode & S_IFREG)) {
	const char *end = path + strlen(path);
	while (path < end) {
	    end = CharPrev(path, end);
	    if (*end == '.') {
		if ((strcmpi(end, ".bat") == 0) ||
		    (strcmpi(end, ".cmd") == 0) ||
		    (strcmpi(end, ".com") == 0) ||
		    (strcmpi(end, ".exe") == 0)) {
		    mode |= S_IEXEC;
		}
		break;
	    }
	}
    }

    mode |= (mode & 0700) >> 3;
    mode |= (mode & 0700) >> 6;

    return mode;
}

static int
winnt_stat(const char *path, struct stati64 *st)
{
    HANDLE h;
    WIN32_FIND_DATA wfd;

    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;

    if (_mbspbrk(path, "?*")) {
	errno = ENOENT;
	return -1;
    }
    h = FindFirstFile(path, &wfd);
    if (h != INVALID_HANDLE_VALUE) {
	FindClose(h);
	st->st_mode  = fileattr_to_unixmode(wfd.dwFileAttributes, path);
	st->st_atime = filetime_to_unixtime(&wfd.ftLastAccessTime);
	st->st_mtime = filetime_to_unixtime(&wfd.ftLastWriteTime);
	st->st_ctime = filetime_to_unixtime(&wfd.ftCreationTime);
	st->st_size = ((__int64)wfd.nFileSizeHigh << 32) | wfd.nFileSizeLow;
    }
    else {
	// If runtime stat(2) is called for network shares, it fails on WinNT.
	// Because GetDriveType returns 1 for network shares. (Win98 returns 4)
	DWORD attr = GetFileAttributes(path);
	if (attr == -1) {
	    errno = map_errno(GetLastError());
	    return -1;
	}
	st->st_mode  = fileattr_to_unixmode(attr, path);
    }

    st->st_dev = st->st_rdev = (isalpha(path[0]) && path[1] == ':') ?
	toupper(path[0]) - 'A' : _getdrive() - 1;

    return 0;
}

int
rb_w32_stat(const char *path, struct stat *st)
{
    struct stati64 tmp;

    if (rb_w32_stati64(path, &tmp)) return -1;
    COPY_STAT(tmp, *st);
    return 0;
}

int
rb_w32_stati64(const char *path, struct stati64 *st)
{
    const char *p;
    char *buf1, *s, *end;
    int len, size;
    int ret;

    if (!path || !st) {
	errno = EFAULT;
	return -1;
    }
    size = strlen(path) + 2;
    buf1 = ALLOCA_N(char, size);
    for (p = path, s = buf1; *p; p++, s++) {
	if (*p == '/')
	    *s = '\\';
	else
	    *s = *p;
    }
    *s = '\0';
    len = s - buf1;
    if (!len || '\"' == *(--s)) {
	errno = ENOENT;
	return -1;
    }
    end = CharPrev(buf1, buf1 + len);

    if (isUNCRoot(buf1)) {
	if (*end == '.')
	    *end = '\0';
	else if (*end != '\\')
	    strlcat(buf1, "\\", size);
    }
    else if (*end == '\\' || (buf1 + 1 == end && *end == ':'))
	strlcat(buf1, ".", size);

    ret = IsWinNT() ? winnt_stat(buf1, st) : stati64(buf1, st);
    if (ret == 0) {
	st->st_mode &= ~(S_IWGRP | S_IWOTH);
    }
    return ret;
}

static int
rb_chsize(HANDLE h, off_t size)
{
    long upos, lpos, usize, lsize, uend, lend;
    off_t end;
    int ret = -1;
    DWORD e;

    if (((lpos = SetFilePointer(h, 0, (upos = 0, &upos), SEEK_CUR)) == -1L &&
	 (e = GetLastError())) ||
	((lend = GetFileSize(h, (DWORD *)&uend)) == -1L && (e = GetLastError()))) {
	errno = map_errno(e);
	return -1;
    }
    end = ((off_t)uend << 32) | (unsigned long)lend;
    usize = (long)(size >> 32);
    lsize = (long)size;
    if (SetFilePointer(h, lsize, &usize, SEEK_SET) == -1L &&
	(e = GetLastError())) {
	errno = map_errno(e);
    }
    else if (!SetEndOfFile(h)) {
	errno = map_errno(GetLastError());
    }
    else {
	ret = 0;
    }
    SetFilePointer(h, lpos, &upos, SEEK_SET);
    return ret;
}

int
truncate(const char *path, off_t length)
{
    HANDLE h;
    int ret;
    if (IsWin95()) {
	int fd = open(path, O_WRONLY), e = 0;
	if (fd == -1) return -1;
	ret = chsize(fd, (unsigned long)length);
	if (ret == -1) e = errno;
	close(fd);
	if (ret == -1) errno = e;
	return ret;
    }
    h = CreateFile(path, GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
	errno = map_errno(GetLastError());
	return -1;
    }
    ret = rb_chsize(h, length);
    CloseHandle(h);
    return ret;
}

int
ftruncate(int fd, off_t length)
{
    long h;

    if (IsWin95()) {
	return chsize(fd, (unsigned long)length);
    }
    h = _get_osfhandle(fd);
    if (h == -1) return -1;
    return rb_chsize((HANDLE)h, length);
}

#ifdef __BORLANDC__
off_t
_filelengthi64(int fd)
{
    DWORD u, l;
    int e;

    l = GetFileSize((HANDLE)_get_osfhandle(fd), &u);
    if (l == (DWORD)-1L && (e = GetLastError())) {
	errno = map_errno(e);
	return (off_t)-1;
    }
    return ((off_t)u << 32) | l;
}

off_t
_lseeki64(int fd, off_t offset, int whence)
{
    long u, l;
    int e;
    HANDLE h = (HANDLE)_get_osfhandle(fd);

    if (!h) {
	errno = EBADF;
	return -1;
    }
    u = (long)(offset >> 32);
    if ((l = SetFilePointer(h, (long)offset, &u, whence)) == -1L &&
	(e = GetLastError())) {
	errno = map_errno(e);
	return -1;
    }
    return ((off_t)u << 32) | l;
}
#endif

int
fseeko(FILE *stream, off_t offset, int whence)
{
    off_t pos;
    switch (whence) {
      case SEEK_CUR:
	if (fgetpos(stream, (fpos_t *)&pos))
	    return -1;
	pos += offset;
	break;
      case SEEK_END:
	if ((pos = _filelengthi64(fileno(stream))) == (off_t)-1)
	    return -1;
	pos += offset;
	break;
      default:
	pos = offset;
	break;
    }
    return fsetpos(stream, (fpos_t *)&pos);
}

off_t
ftello(FILE *stream)
{
    off_t pos;
    if (fgetpos(stream, (fpos_t *)&pos)) return (off_t)-1;
    return pos;
}

static long
filetime_to_clock(FILETIME *ft)
{
    __int64 qw = ft->dwHighDateTime;
    qw <<= 32;
    qw |= ft->dwLowDateTime;
    qw /= 10000;  /* File time ticks at 0.1uS, clock at 1mS */
    return (long) qw;
}

int
rb_w32_times(struct tms *tmbuf)
{
    FILETIME create, exit, kernel, user;

    if (GetProcessTimes(GetCurrentProcess(),&create, &exit, &kernel, &user)) {
	tmbuf->tms_utime = filetime_to_clock(&user);
	tmbuf->tms_stime = filetime_to_clock(&kernel);
	tmbuf->tms_cutime = 0;
	tmbuf->tms_cstime = 0;
    }
    else {
	tmbuf->tms_utime = clock();
	tmbuf->tms_stime = 0;
	tmbuf->tms_cutime = 0;
	tmbuf->tms_cstime = 0;
    }
    return 0;
}

#define yield_once() Sleep(0)
#define yield_until(condition) do yield_once(); while (!(condition))

static void
catch_interrupt(void)
{
    yield_once();
    RUBY_CRITICAL(rb_w32_wait_events(NULL, 0, 0));
}

#if defined __BORLANDC__ || defined _WIN32_WCE
#undef read
int
read(int fd, void *buf, size_t size)
{
    int trap_immediate = rb_trap_immediate;
    int ret = _read(fd, buf, size);
    if ((ret < 0) && (errno == EPIPE)) {
	errno = 0;
	ret = 0;
    }
    rb_trap_immediate = trap_immediate;
    catch_interrupt();
    return ret;
}
#endif

#undef fgetc
int
rb_w32_getc(FILE* stream)
{
    int c, trap_immediate = rb_trap_immediate;
#ifndef _WIN32_WCE
    if (enough_to_get(stream->FILE_COUNT)) {
	c = (unsigned char)*stream->FILE_READPTR++;
	rb_trap_immediate = trap_immediate;
    }
    else 
#endif
    {
	c = _filbuf(stream);
#if defined __BORLANDC__ || defined _WIN32_WCE
        if ((c == EOF) && (errno == EPIPE)) {
	    clearerr(stream);
        }
#endif
	rb_trap_immediate = trap_immediate;
	catch_interrupt();
    }
    return c;
}

#undef fputc
int
rb_w32_putc(int c, FILE* stream)
{
    int trap_immediate = rb_trap_immediate;
#ifndef _WIN32_WCE
    if (enough_to_put(stream->FILE_COUNT)) {
	c = (unsigned char)(*stream->FILE_READPTR++ = (char)c);
	rb_trap_immediate = trap_immediate;
    }
    else 
#endif
    {
	c = _flsbuf(c, stream);
	rb_trap_immediate = trap_immediate;
	catch_interrupt();
    }
    return c;
}

struct asynchronous_arg_t {
    /* output field */
    void* stackaddr;
    int errnum;

    /* input field */
    uintptr_t (*func)(uintptr_t self, int argc, uintptr_t* argv);
    uintptr_t self;
    int argc;
    uintptr_t* argv;
};

static DWORD WINAPI
call_asynchronous(PVOID argp)
{
    DWORD ret;
    struct asynchronous_arg_t *arg = argp;
    arg->stackaddr = &argp;
    ret = (DWORD)arg->func(arg->self, arg->argc, arg->argv);
    arg->errnum = errno;
    return ret;
}

uintptr_t
rb_w32_asynchronize(asynchronous_func_t func, uintptr_t self,
		    int argc, uintptr_t* argv, uintptr_t intrval)
{
    DWORD val;
    BOOL interrupted = FALSE;
    HANDLE thr;

    RUBY_CRITICAL({
	struct asynchronous_arg_t arg;

	arg.stackaddr = NULL;
	arg.errnum = 0;
	arg.func = func;
	arg.self = self;
	arg.argc = argc;
	arg.argv = argv;

	thr = CreateThread(NULL, 0, call_asynchronous, &arg, 0, &val);

	if (thr) {
	    yield_until(arg.stackaddr);

	    if (rb_w32_wait_events_blocking(&thr, 1, INFINITE) != WAIT_OBJECT_0) {
		interrupted = TRUE;

		if (TerminateThread(thr, intrval)) {
		    yield_once();
		}
	    }

	    GetExitCodeThread(thr, &val);
	    CloseHandle(thr);

	    if (interrupted) {
		/* must release stack of killed thread, why doesn't Windows? */
		MEMORY_BASIC_INFORMATION m;

		memset(&m, 0, sizeof(m));
		if (!VirtualQuery(arg.stackaddr, &m, sizeof(m))) {
		    Debug(fprintf(stderr, "couldn't get stack base:%p:%d\n",
				  arg.stackaddr, GetLastError()));
		}
		else if (!VirtualFree(m.AllocationBase, 0, MEM_RELEASE)) {
		    Debug(fprintf(stderr, "couldn't release stack:%p:%d\n",
				  m.AllocationBase, GetLastError()));
		}
		errno = EINTR;
	    }
	    else {
		errno = arg.errnum;
	    }
	}
    });

    if (!thr) {
	rb_fatal("failed to launch waiter thread:%ld", GetLastError());
    }

    return val;
}

char **
rb_w32_get_environ(void)
{
    char *envtop, *env;
    char **myenvtop, **myenv;
    int num;

    /*
     * We avoid values started with `='. If you want to deal those values,
     * change this function, and some functions in hash.c which recognize
     * `=' as delimiter or rb_w32_getenv() and ruby_setenv().
     * CygWin deals these values by changing first `=' to '!'. But we don't
     * use such trick and follow cmd.exe's way that just doesn't show these
     * values.
     * (U.N. 2001-11-15)
     */
    envtop = GetEnvironmentStrings();
    for (env = envtop, num = 0; *env; env += strlen(env) + 1)
	if (*env != '=') num++;

    myenvtop = (char **)malloc(sizeof(char *) * (num + 1));
    for (env = envtop, myenv = myenvtop; *env; env += strlen(env) + 1) {
	if (*env != '=') {
	    if (!(*myenv = strdup(env))) {
		break;
	    }
	    myenv++;
	}
    }
    *myenv = NULL;
    FreeEnvironmentStrings(envtop);

    return myenvtop;
}

void
rb_w32_free_environ(char **env)
{
    char **t = env;

    while (*t) free(*t++);
    free(env);
}

#undef getpid
rb_pid_t
rb_w32_getpid(void)
{
    rb_pid_t pid;

    pid = getpid();

    if (IsWin95()) pid = -pid;

    return pid;
}


rb_pid_t
rb_w32_getppid(void)
{
    static long (WINAPI *pNtQueryInformationProcess)(HANDLE, int, void *, ULONG, ULONG *) = NULL;
    rb_pid_t ppid = 0;

    if (!IsWin95() && rb_w32_osver() >= 5) {
	if (!pNtQueryInformationProcess) {
	    HANDLE hNtDll = GetModuleHandle("ntdll.dll");
	    if (hNtDll) {
		pNtQueryInformationProcess = (long (WINAPI *)(HANDLE, int, void *, ULONG, ULONG *))GetProcAddress(hNtDll, "NtQueryInformationProcess");
		if (pNtQueryInformationProcess) {
		    struct {
			long ExitStatus;
			void* PebBaseAddress;
			ULONG AffinityMask;
			ULONG BasePriority;
			ULONG UniqueProcessId;
			ULONG ParentProcessId;
		    } pbi;
		    ULONG len;
		    long ret = pNtQueryInformationProcess(GetCurrentProcess(), 0, &pbi, sizeof(pbi), &len);
		    if (!ret) {
			ppid = pbi.ParentProcessId;
		    }
		}
	    }
	}
    }

    return ppid;
}

int
rb_w32_fclose(FILE *fp)
{
    int fd = fileno(fp);
    SOCKET sock = TO_SOCKET(fd);
    int save_errno = errno;

    if (fflush(fp)) return -1;
    if (!is_socket(sock)) {
	UnlockFile((HANDLE)sock, 0, 0, LK_LEN, LK_LEN);
	return fclose(fp);
    }
    _set_osfhnd(fd, (SOCKET)INVALID_HANDLE_VALUE);
    fclose(fp);
    errno = save_errno;
    if (closesocket(sock) == SOCKET_ERROR) {
	errno = map_errno(WSAGetLastError());
	return -1;
    }
    return 0;
}

int
rb_w32_close(int fd)
{
    SOCKET sock = TO_SOCKET(fd);
    int save_errno = errno;
    st_data_t key;

    if (!is_socket(sock)) {
	UnlockFile((HANDLE)sock, 0, 0, LK_LEN, LK_LEN);
	return _close(fd);
    }
    _set_osfhnd(fd, (SOCKET)INVALID_HANDLE_VALUE);
    key = (st_data_t)sock;
    st_delete(socklist, &key, NULL);
    sock = (SOCKET)key;
    _close(fd);
    errno = save_errno;
    if (closesocket(sock) == SOCKET_ERROR) {
	errno = map_errno(WSAGetLastError());
	return -1;
    }
    return 0;
}

#undef read
size_t
rb_w32_read(int fd, void *buf, size_t size)
{
    SOCKET sock = TO_SOCKET(fd);

    if (!is_socket(sock))
	return read(fd, buf, size);
    else
	return rb_w32_recv(fd, buf, size, 0);
}

#undef write
size_t
rb_w32_write(int fd, const void *buf, size_t size)
{
    SOCKET sock = TO_SOCKET(fd);

    if (!is_socket(sock)) {
	size_t ret = write(fd, buf, size);
	if ((int)ret < 0 && errno == EINVAL)
	    errno = map_errno(GetLastError());
	return ret;
    }
    else
	return rb_w32_send(fd, buf, size, 0);
}

static int
unixtime_to_filetime(time_t time, FILETIME *ft)
{
    struct tm *tm;
    SYSTEMTIME st;
    FILETIME lt;

    tm = localtime(&time);
    st.wYear = tm->tm_year + 1900;
    st.wMonth = tm->tm_mon + 1;
    st.wDayOfWeek = tm->tm_wday;
    st.wDay = tm->tm_mday;
    st.wHour = tm->tm_hour;
    st.wMinute = tm->tm_min;
    st.wSecond = tm->tm_sec;
    st.wMilliseconds = 0;
    if (!SystemTimeToFileTime(&st, &lt) ||
	!LocalFileTimeToFileTime(&lt, ft)) {
	errno = map_errno(GetLastError());
	return -1;
    }
    return 0;
}

int
rb_w32_utime(const char *path, const struct utimbuf *times)
{
    HANDLE hFile;
    FILETIME atime, mtime;
    struct stati64 stat;
    int ret = 0;

    if (rb_w32_stati64(path, &stat)) {
	return -1;
    }

    if (times) {
	if (unixtime_to_filetime(times->actime, &atime)) {
	    return -1;
	}
	if (unixtime_to_filetime(times->modtime, &mtime)) {
	    return -1;
	}
    }
    else {
	GetSystemTimeAsFileTime(&atime);
	mtime = atime;
    }

    RUBY_CRITICAL({
	const DWORD attr = GetFileAttributes(path);
	if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY))
	    SetFileAttributes(path, attr & ~FILE_ATTRIBUTE_READONLY);
	hFile = CreateFile(path, GENERIC_WRITE, 0, 0, OPEN_EXISTING,
			   IsWin95() ? 0 : FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (hFile == INVALID_HANDLE_VALUE) {
	    errno = map_errno(GetLastError());
	    ret = -1;
	}
	else {
	    if (!SetFileTime(hFile, NULL, &atime, &mtime)) {
		errno = map_errno(GetLastError());
		ret = -1;
	    }
	    CloseHandle(hFile);
	}
	if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY))
	    SetFileAttributes(path, attr);
    });

    return ret;
}

int
rb_w32_vsnprintf(char *buf, size_t size, const char *format, va_list va)
{
    int ret = _vsnprintf(buf, size, format, va);
    if (size > 0) buf[size - 1] = 0;
    return ret;
}

int
rb_w32_snprintf(char *buf, size_t size, const char *format, ...)
{
    int ret;
    va_list va;

    va_start(va, format);
    ret = vsnprintf(buf, size, format, va);
    va_end(va);
    return ret;
}

int
rb_w32_mkdir(const char *path, int mode)
{
    int ret = -1;
    RUBY_CRITICAL(do {
	if (CreateDirectory(path, NULL) == FALSE) {
	    errno = map_errno(GetLastError());
	    break;
	}
	if (chmod(path, mode) == -1) {
	    RemoveDirectory(path);
	    break;
	}
	ret = 0;
    } while (0));
    return ret;
}

int
rb_w32_rmdir(const char *path)
{
    int ret = 0;
    RUBY_CRITICAL({
	const DWORD attr = GetFileAttributes(path);
	if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY)) {
	    SetFileAttributes(path, attr & ~FILE_ATTRIBUTE_READONLY);
	}
	if (RemoveDirectory(path) == FALSE) {
	    errno = map_errno(GetLastError());
	    ret = -1;
	    if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY)) {
		SetFileAttributes(path, attr);
	    }
	}
    });
    return ret;
}

int
rb_w32_unlink(const char *path)
{
    int ret = 0;
    RUBY_CRITICAL({
	const DWORD attr = GetFileAttributes(path);
	if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY)) {
	    SetFileAttributes(path, attr & ~FILE_ATTRIBUTE_READONLY);
	}
	if (DeleteFile(path) == FALSE) {
	    errno = map_errno(GetLastError());
	    ret = -1;
	    if (attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_READONLY)) {
		SetFileAttributes(path, attr);
	    }
	}
    });
    return ret;
}

#if !defined(__BORLANDC__) && !defined(_WIN32_WCE)
int
rb_w32_isatty(int fd)
{
    if (!(_osfile(fd) & FOPEN)) {
	errno = EBADF;
	return 0;
    }
    if (!(_osfile(fd) & FDEV)) {
	errno = ENOTTY;
	return 0;
    }
    return 1;
}
#endif

//
// Fix bcc32's stdio bug
//

#ifdef __BORLANDC__
static int
too_many_files(void)
{
    FILE *f;
    for (f = _streams; f < _streams + _nfile; f++) {
	if (f->fd < 0) return 0;
    }
    return 1;
}

#undef fopen
FILE *
rb_w32_fopen(const char *path, const char *mode)
{
    FILE *f = (errno = 0, fopen(path, mode));
    if (f == NULL && errno == 0) {
	if (too_many_files())
	    errno = EMFILE;
    }
    return f;
}

FILE *
rb_w32_fdopen(int handle, const char *type)
{
    FILE *f = (errno = 0, _fdopen(handle, (char *)type));
    if (f == NULL && errno == 0) {
	if (handle < 0)
	    errno = EBADF;
	else if (too_many_files())
	    errno = EMFILE;
    }
    return f;
}

FILE *
rb_w32_fsopen(const char *path, const char *mode, int shflags)
{
    FILE *f = (errno = 0, _fsopen(path, mode, shflags));
    if (f == NULL && errno == 0) {
	if (too_many_files())
	    errno = EMFILE;
    }
    return f;
}
#endif
