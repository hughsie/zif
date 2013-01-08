AC_DEFUN([AX_CHECK_ENUM],[
AS_VAR_PUSHDEF([ac_var],[ac_cv_defined_$2_$1])dnl
AC_CACHE_CHECK([for $2 defined in $1], ac_var,
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <$1>]], [[
  if($2);
]])],[AS_VAR_SET(ac_var, yes)],[AS_VAR_SET(ac_var, no)]))
AS_IF([test AS_VAR_GET(ac_var) != "no"], [$3], [$4])dnl
AS_VAR_POPDEF([ac_var])dnl
])
