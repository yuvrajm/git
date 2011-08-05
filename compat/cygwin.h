#include <sys/types.h>
#include <sys/stat.h>

typedef int (*stat_fn_t)(const char*, struct stat*);
extern stat_fn_t cygwin_stat_fn;
extern stat_fn_t cygwin_lstat_fn;

#define stat(path, buf) (*cygwin_stat_fn)(path, buf)
#define lstat(path, buf) (*cygwin_lstat_fn)(path, buf)

#define has_dos_drive_prefix(path) (isalpha(*(path)) && (path)[1] == ':')
#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
