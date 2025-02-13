# lal.m4 - lal specific macros
#
# serial 21

AC_DEFUN([LAL_WITH_DEFAULT_DEBUG_LEVEL],[
  AC_ARG_WITH(
    [default_debug_level],
    AS_HELP_STRING([--with-default-debug-level],[set default value of lalDebugLevel [default=1, i.e. print error messages]]),
    [AS_IF([test "x`expr "X${withval}" : ["^\(X[0-9][0-9]*$\)"]`" != "xX${withval}"],[
      AC_MSG_ERROR([bad integer value '${withval}' for --with-default-debug-level])
    ])],
    [with_default_debug_level=1]
  )
  AC_DEFINE_UNQUOTED([LAL_DEFAULT_DEBUG_LEVEL],[${with_default_debug_level}],[Set default value of lalDebugLevel])
])

AC_DEFUN([LAL_WITH_DATA_PATH],[
  AC_ARG_WITH(
    [relative_data_path],
    AS_HELP_STRING([--with-relative-data-path],[set location relative to liblalsupport.so for LAL data path [default: none]]),
    [
      AS_IF([test "x`expr "X${withval}" : ["^\(X\./.*$\)"]`" != "xX${withval}" && test "x`expr "X${withval}" : ["^\(X\.\./.*$\)"]`" != "xX${withval}"],[
        AC_MSG_ERROR([bad relative path value '${withval}' for --with-relative-data-path])
      ])
      AC_DEFINE_UNQUOTED([LAL_RELATIVE_DATA_PATH],["${with_relative_data_path}"],[Set location relative to liblal.so for LAL data path])
    ]
  )
  AC_ARG_WITH(
    [fallback_data_path],
    AS_HELP_STRING([--with-fallback-data-path],[use hard-coded fallback location for LAL data path [default: yes]]),
    [
      AS_CASE(["${withval}"],
        [yes],[:],
        [no],[:],
        AC_MSG_ERROR([bad value '${withval}' for --with-fallback-data-path])
      )
    ],[
      withval=yes
    ]
  )
  AS_IF([test "X${withval}" = Xyes],[
    AC_DEFINE_UNQUOTED([LAL_FALLBACK_DATA_PATH],[1],[Use hard-coded fallback location for LAL data path])
  ])
])

AC_DEFUN([LAL_ENABLE_FFTW3_MEMALIGN],
[AC_ARG_ENABLE(
  [fftw3_memalign],
  AS_HELP_STRING([--enable-fftw3-memalign],[use aligned memory optimizations with fftw3 [default=no]]),
  AS_CASE(["${enableval}"],
    [yes],[fftw3_memalign=true],
    [no],[fftw3_memalign=false],
    AC_MSG_ERROR([bad value for ${enableval} for --enable-fftw3-memalign])
  ),[fftw3_memalign=false])
])

AC_DEFUN([LAL_ENABLE_INTELFFT],
[AC_ARG_ENABLE(
  [intelfft],
  AS_HELP_STRING([--enable-intelfft],[use Intel FFT libraries insted of FFTW [default=no]]),
  AS_CASE(["${enableval}"],
    [yes],[intelfft=true],
    [no],[intelfft=false],
    [condor],[intelfft=true; qthread=tru; AC_DEFINE([LALQTHREAD],[1],[Use fake qthread library for MKL Condor compatibility])],
    AC_MSG_ERROR([bad value for ${enableval} for --enable-intelfft])
  ),[intelfft=false])
])

AC_DEFUN([LAL_ENABLE_MACROS],
[AC_ARG_ENABLE(
  [macros],
  AS_HELP_STRING([--enable-macros],[use LAL macros [default=yes]]),
  AS_CASE(["${enableval}"],
    [yes],,
    [no],AC_DEFINE([NOLALMACROS],[1],[Use functions rather than macros]),
    AC_MSG_ERROR([bad value for ${enableval} for --enable-macros])
  ),)
])

AC_DEFUN([LAL_ENABLE_PTHREAD_LOCK], [
  AC_ARG_ENABLE([pthread_lock],
    AS_HELP_STRING([--enable-pthread-lock],[use pthread mutex lock for threadsafety @<:@default=yes@:>@]),
    AS_CASE(["${enableval}"],
      [yes],[lal_pthread_lock=true],
      [no],[lal_pthread_lock=false],
      AC_MSG_ERROR([bad value for ${enableval} for --enable-pthread-lock])
    ),
    [lal_pthread_lock=true]
  )
  AS_IF([test "x$lal_pthread_lock" = "xtrue"], [
    # check for pthreads
    AX_PTHREAD([
      LALSUITE_ADD_FLAGS([C],[${PTHREAD_CFLAGS}],[${PTHREAD_LIBS}])
    ],[
      AC_MSG_ERROR([do not know how to compile posix threads])
    ])
    AC_DEFINE([LAL_PTHREAD_LOCK],[1],[Use pthread mutex lock for threadsafety])
  ])
  AC_SUBST([PTHREAD_CFLAGS])
  AC_SUBST([PTHREAD_LIBS])
])

AC_DEFUN([LAL_INTEL_MKL_QTHREAD_WARNING],
[echo "**************************************************************"
 echo "* LAL will be linked against the fake POSIX thread library!  *"
 echo "*                                                            *"
 echo "* This build of LAL will not be thread safe and cannot be    *"
 echo "* linked against the system pthread library.                 *"
 echo "*                                                            *"
 echo "* The environment variables                                  *"
 echo "*                                                            *"
 echo "*    MKL_SERIAL=YES                                          *"
 echo "*    KMP_LIBRARY=serial                                      *"
 echo "*                                                            *"
 echo "* must be set before running executables linked against this *"
 echo "* build of LAL.                                              *"
 echo "*                                                            *"
 echo "* Please see the documention of the FFT package for details. *"
 echo "**************************************************************"
])

AC_DEFUN([LAL_INTEL_FFT_LIBS_MSG_ERROR],
[echo "**************************************************************"
 echo "* The --enable-static and --enable-shared options are        *"
 echo "* mutually exclusive with Intel FFT libraries.               *"
 echo "*                                                            *"
 echo "* Please reconfigure with:                                   *"
 echo "*                                                            *"
 echo "*   --enable-static --disable-shared                         *"
 echo "*                                                            *"
 echo "* for static libraries or                                    *"
 echo "*                                                            *"
 echo "*   --disable-static --enable-shared                         *"
 echo "*                                                            *"
 echo "* for shared libraries.                                      *"
 echo "*                                                            *"
 echo "* Please see the instructions in the file INSTALL.           *"
 echo "**************************************************************"
AC_MSG_ERROR([Intel FFT must use either static or shared libraries])
])

AC_DEFUN([LAL_ENABLE_DEBUG],[
  AC_ARG_ENABLE(
    [debug],
    AS_HELP_STRING([--enable-debug],[THIS OPTION IS NO LONGER SUPPORTED]),
    AC_MSG_ERROR([[
**************************************************************************
* The options --enable-debug/--disable-debug are no longer supported.    *
* The only important thing that this option used to do was to change the *
* default value of lalDebugLevel - if you still want to do this, please  *
* use the new option --with-default-debug-level. Otherwise simply remove *
* --enable-debug/--disable-debug from your ./configure command line.     *
**************************************************************************
]])
  )
])
