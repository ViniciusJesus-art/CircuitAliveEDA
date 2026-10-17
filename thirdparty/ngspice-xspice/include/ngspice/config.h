/* src/include/ngspice/config.h.  Generated from config.h.in by configure.  */
/* src/include/ngspice/config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if we want debug sensitivity analysis */
/* #undef ASDEBUG */

/* Define if we want to bypass cbd/cbs calculation for non varying vbs/vbd */
/* #undef CAPBYPASS */

/* The CIDER enhancements */
/* #undef CIDER */

/* Define to 1 if the `closedir' function returns void instead of int. */
/* #undef CLOSEDIR_VOID */

/* Define if you want to debug ngspice shell */
/* #undef CPDEBUG */

/* Define if we want debug distortion analysis (BLOCKTIMES) */
/* #undef D_DBG_BLOCKTIMES */

/* Define if we want debug distortion analysis (SMALLTIMES) */
/* #undef D_DBG_SMALLTIMES */

/* Define if we want some experimental code */
/* #undef EXPERIMENTAL_CODE */

/* Define if we want to enable experimental devices */
/* #undef EXP_DEV */

/* Define if you want to disable UNICODE support */
/* #undef EXT_ASC */

/* Define if you want to debug frontend */
/* #undef FTEDEBUG */

/* rel. path of libraries and scripts */
/* #undef HAS_RELPATH */

/* Tcl Windows */
/* #undef HAS_TCLWIN */

/* define to specify MS Windows executable with simple GUI */
/* #undef HAS_WINGUI */

/* Define to 1 if you have the `access' function. */
#define HAVE_ACCESS 1

/* Define to 1 if you have the `acosh' function. */
#define HAVE_ACOSH 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the `asinh' function. */
#define HAVE_ASINH 1

/* Define to 1 if you have the `atanh' function. */
#define HAVE_ATANH 1

/* Define to 1 if you have the <blt.h> header file. */
/* #undef HAVE_BLT_H */

/* Define to enable BSD editline */
/* #undef HAVE_BSDEDITLINE */

/* Define to 1 if you have the `clock_gettime' function. */
#define HAVE_CLOCK_GETTIME 1

/* Define to 1 if you have the <ctype.h> header file. */
#define HAVE_CTYPE_H 1

/* Define to 1 if you have the declaration of `isinf', and to 0 if you don't.
   */
#define HAVE_DECL_ISINF 1

/* Define to 1 if you have the declaration of `isnan', and to 0 if you don't.
   */
#define HAVE_DECL_ISNAN 1

/* Define to 1 if you have the declaration of `tzname', and to 0 if you don't.
   */
/* #undef HAVE_DECL_TZNAME */

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Have dirname in libgen */
#define HAVE_DIRNAME 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you don't have `vprintf' but do have `_doprnt.' */
/* #undef HAVE_DOPRNT */

/* Define to 1 if you have the `dup2' function. */
#define HAVE_DUP2 1

/* Define to 1 if you have the <editline/readline.h> header file. */
/* #undef HAVE_EDITLINE_READLINE_H */

/* Define to 1 if you have the `endpwent' function. */
#define HAVE_ENDPWENT 1

/* Define to 1 if you have the `erfc' function. */
#define HAVE_ERFC 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <fftw3.h> header file. */
#define HAVE_FFTW3_H 1

/* Define to 1 if you have the `finite' function. */
#define HAVE_FINITE 1

/* Define to 1 if you have the <float.h> header file. */
#define HAVE_FLOAT_H 1

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Define to 1 if you have the `ftime' function. */
#define HAVE_FTIME 1

/* Define to 1 if you have the `getcwd' function. */
#define HAVE_GETCWD 1

/* Define to 1 if you have the `gethostbyname' function. */
#define HAVE_GETHOSTBYNAME 1

/* Define to 1 if you have the <getopt.h> header file. */
#define HAVE_GETOPT_H 1

/* Have fcn getopt_long() */
#define HAVE_GETOPT_LONG 1

/* Define to 1 if you have the `getrlimit' function. */
#define HAVE_GETRLIMIT 1

/* Define to 1 if you have the `getrusage' function. */
/* #undef HAVE_GETRUSAGE */

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `getwd' function. */
/* #undef HAVE_GETWD */

/* Define if we have GNU readline */
/* #undef HAVE_GNUREADLINE */

/* Define to 1 if you have the <ieeefp.h> header file. */
/* #undef HAVE_IEEEFP_H */

/* Define to 1 if you have the `index' function. */
/* #undef HAVE_INDEX */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `isatty' function. */
#define HAVE_ISATTY 1

/* Have fft routines in libfftw3 */
#define HAVE_LIBFFTW3 /**/

/* Define to 1 if you have the `fontconfig' library (-lfontconfig). */
/* #undef HAVE_LIBFONTCONFIG */

/* Define to 1 if you have the `freetype' library (-lfreetype). */
/* #undef HAVE_LIBFREETYPE */

/* Define if we want garbage collection enabled */
/* #undef HAVE_LIBGC */

/* Define to 1 if you have the <libintl.h> header file. */
#define HAVE_LIBINTL_H 1

/* Define to 1 if you have the `m' library (-lm). */
#define HAVE_LIBM 1

/* Define to 1 if you have the `pthread' library (-lpthread). */
#define HAVE_LIBPTHREAD 1

/* Have libsamplerate */
#define HAVE_LIBSAMPLERATE /**/

/* Have libsndfile */
/* #undef HAVE_LIBSNDFILE */

/* Have xft routines in libxft */
/* #undef HAVE_LIBXFT */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the `localtime' function. */
#define HAVE_LOCALTIME 1

/* Define to 1 if you have the `logb' function. */
#define HAVE_LOGB 1

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the `memmove' function. */
/* #undef HAVE_MEMMOVE */

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the `modf' function. */
#define HAVE_MODF 1

/* Define to 1 if you have the <ncurses/termcap.h> header file. */
#define HAVE_NCURSES_TERMCAP_H 1

/* Define to 1 if you have the <ndir.h> header file, and it defines `DIR'. */
/* #undef HAVE_NDIR_H */

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the `popen' function. */
#define HAVE_POPEN 1

/* Define to 1 if you have the <pwd.h> header file. */
#define HAVE_PWD_H 1

/* Define to 1 if you have the `qsort' function. */
#define HAVE_QSORT 1

/* Define to 1 if you have the `queryperformancecounter' function. */
/* #undef HAVE_QUERYPERFORMANCECOUNTER */

/* Define to 1 if you have the <readline/history.h> header file. */
/* #undef HAVE_READLINE_HISTORY_H */

/* Define to 1 if you have the <readline/readline.h> header file. */
/* #undef HAVE_READLINE_READLINE_H */

/* Define to 1 if you have the `rindex' function. */
/* #undef HAVE_RINDEX */

/* Define to 1 if you have the <samplerate.h> header file. */
#define HAVE_SAMPLERATE_H 1

/* Define to 1 if you have the `scalb' function. */
#define HAVE_SCALB 1

/* Define to 1 if you have the `scalbn' function. */
#define HAVE_SCALBN 1

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the <sgtty.h> header file. */
/* #undef HAVE_SGTTY_H */

/* Define to 1 if the system has the type `sighandler_t'. */
/* #undef HAVE_SIGHANDLER_T */

/* Define if we have sigsetjmp(). */
#define HAVE_SIGSETJMP /**/

/* Define to 1 if the system has the type `sig_t'. */
#define HAVE_SIG_T 1

/* Define to 1 if you have the <sndfile.h> header file. */
/* #undef HAVE_SNDFILE_H */

/* Define to 1 if you have the `snprintf' function. */
#define HAVE_SNPRINTF 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* Define to 1 if stdbool.h conforms to C99. */
/* #undef HAVE_STDBOOL_H */

/* Define to 1 if you have the <stddef.h> header file. */
#define HAVE_STDDEF_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the <stropts.h> header file. */
/* #undef HAVE_STROPTS_H */

/* Define to 1 if you have the `strrchr' function. */
#define HAVE_STRRCHR 1

/* Define to 1 if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* Define to 1 if you have the `strtol' function. */
#define HAVE_STRTOL 1

/* Define to 1 if `tm_zone' is a member of `struct tm'. */
#define HAVE_STRUCT_TM_TM_ZONE 1

/* Define to 1 if you have the <sys/dir.h> header file, and it defines `DIR'.
   */
/* #undef HAVE_SYS_DIR_H */

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/io.h> header file. */
#define HAVE_SYS_IO_H 1

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines `DIR'.
   */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/sysctl.h> header file. */
/* #undef HAVE_SYS_SYSCTL_H */

/* Define to 1 if you have the <sys/timeb.h> header file. */
#define HAVE_SYS_TIMEB_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the `tcgetattr' function. */
#define HAVE_TCGETATTR 1

/* Define to 1 if you have the <tcl.h> header file. */
/* #undef HAVE_TCL_H */

/* Define to 1 if you have the `tcsetattr' function. */
#define HAVE_TCSETATTR 1

/* Define to 1 if you have the `tdestroy' function. */
#define HAVE_TDESTROY 1

/* Define if we have ncurses/terminfo or termcap */
/* #undef HAVE_TERMCAP */

/* Define to 1 if you have the <termcap.h> header file. */
#define HAVE_TERMCAP_H 1

/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the <termio.h> header file. */
/* #undef HAVE_TERMIO_H */

/* Define to 1 if you have the <term.h> header file. */
#define HAVE_TERM_H 1

/* Define to 1 if you have the `time' function. */
#define HAVE_TIME 1

/* Define to 1 if you have the `times' function. */
/* #undef HAVE_TIMES */

/* Define to 1 if your `struct tm' has `tm_zone'. Deprecated, use
   `HAVE_STRUCT_TM_TM_ZONE' instead. */
#define HAVE_TM_ZONE 1

/* Define to 1 if you have the `tsearch' function. */
#define HAVE_TSEARCH 1

/* Define to 1 if you don't have `tm_zone' but do have the external array
   `tzname'. */
/* #undef HAVE_TZNAME */

/* Define to 1 if you have the `ulimit' function. */
/* #undef HAVE_ULIMIT */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `utimes' function. */
#define HAVE_UTIMES 1

/* Define to 1 if you have the <values.h> header file. */
#define HAVE_VALUES_H 1

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the `vprintf' function. */
#define HAVE_VPRINTF 1

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* Define to 1 if the system has the type `_Bool'. */
#define HAVE__BOOL 1

/* Define to 1 if you have the </proc/meminfo> header file. */
#define HAVE__PROC_MEMINFO 1

/* Define to 1 if the system has the type `__sighandler_t'. */
#define HAVE___SIGHANDLER_T 1

/* Client-Server only via stdio. */
/* #undef IPC_DEBUG_VIA_STDIO */

/* Client-Server via socket. */
#define IPC_UNIX_SOCKETS 1

/* Define if we want KLU linear systems solver */
/* KLU and CIDER are detected from the linked library by CMake. */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* The NDEV interface */
/* #undef NDEV */

/* Define if you want to discover :) */
/* #undef NEWPRED */

/* Compile with debug info */
/* #undef NGDEBUG */

/* Define the directory for executables */
#define NGSPICEBINDIR "/usr/local/bin"

/* Define the build date */
#define NGSPICEBUILDDATE "Sat Oct 17 19:17:38 UTC 2026"

/* Define the directory for architecture independent data files */
#define NGSPICEDATADIR "/usr/local/share/ngspice"

/* Define the directory for architecture-dependent libraries */
#define NGSPICELIBDIR "/usr/local/lib/ngspice"

/* Define if we want NOBYPASS */
/* #undef NOBYPASS */

/* Experimental code never implemented to damp Newton iterations */
/* #undef NODELIMITING */

/* Internal (old) help not available */
#define NOINTHELP 1

/* Bison is older than 2.7 */
/* #undef OLD_BISON */

/* The OSDI enhancements */
#define OSDI 1

/* Other Operating System */
#define OS_COMPILED 6

/* Name of package */
#define PACKAGE "ngspice"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://ngspice.sourceforge.net/bugrep.html"

/* Define to the full name of this package. */
#define PACKAGE_NAME "ngspice"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "ngspice 47"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "ngspice"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "47"

/* Define if we want predictor algorithm */
/* #undef PREDICTOR */

/* Define if you want to debug pole-zero analysis */
/* #undef PZDEBUG */

/* S parameter analysis */
#define RFSPICE 1

/* Define to the type of arg 1 for `select'. */
#define SELECT_TYPE_ARG1 int

/* Define to the type of args 2, 3 and 4 for `select'. */
#define SELECT_TYPE_ARG234 (fd_set *)

/* Define to the type of arg 5 for `select'. */
#define SELECT_TYPE_ARG5 (struct timeval *)

/* Define if we want debug sensitivity analysis */
/* #undef SENSDEBUG */

/* shared ngspice module */
#define SHARED_MODULE 1

/* Define to 1 if the `S_IS*' macros in <sys/stat.h> do not work properly. */
/* #undef STAT_MACROS_BROKEN */

/* Define to 1 if all of the C90 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Define if we want stepdebug */
/* #undef STEPDEBUG */

/* Tcl Module */
/* #undef TCL_MODULE */

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* OpenMP parallel processing */
#define USE_OMP 1

/* Version number of package */
#define VERSION "47"

/* Define if you want PSS analysis */
/* #undef WITH_PSS */

/* The XSPICE enhancements */
#define XSPICE 1

/* Define to 1 if the X Window System is missing or not being used. */
#define X_DISPLAY_MISSING 1

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Define as a signed integer type capable of holding a process identifier. */
/* #undef pid_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */
