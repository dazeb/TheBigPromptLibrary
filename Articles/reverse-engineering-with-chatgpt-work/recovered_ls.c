/* recovered_ls.c — behavioral reconstruction
 *
 * Derived from reverse engineering a stripped GNU coreutils `ls` 9.4 binary
 * (SHA-256 0148f5ab3062a905281d8deb9645363da5131011c9e7b6dcaa38b504e41b68ea)
 * with Ghidra 12.1.2 driven headlessly through GhidraSQL 0.0.3.
 *
 * This is NOT GNU's original source and NOT a drop-in replacement for ls.
 * It reproduces the recovered program architecture and a subset of behavior;
 * see README.md in this directory for what is deliberately omitted.
 *
 * Because it is derived from analysis of a GPLv3 binary, this reconstruction
 * is licensed under the GNU General Public License, version 3 or later, to
 * match the analyzed original. See https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/*
 * Clean-room behavioral reconstruction of the analyzed /bin/ls control flow.
 *
 * This is intentionally compact and independent. It preserves the recovered
 * gather -> sort -> render -> deferred-directory traversal architecture, but
 * it is not the original GNU coreutils source and does not claim complete
 * command-line compatibility.
 */

typedef enum {
    FORMAT_SINGLE,
    FORMAT_COLUMNS,
    FORMAT_LONG,
    FORMAT_COMMAS
} OutputFormat;

typedef enum {
    SORT_NAME,
    SORT_SIZE,
    SORT_TIME,
    SORT_EXTENSION,
    SORT_NONE
} SortMode;

typedef enum {
    COLOR_NEVER,
    COLOR_ALWAYS,
    COLOR_AUTO
} ColorMode;

typedef struct {
    bool show_all;
    bool almost_all;
    bool directory_as_file;
    bool classify;
    bool human_readable;
    bool print_inode;
    bool print_blocks;
    bool numeric_ids;
    bool recursive;
    bool reverse;
    bool dereference;
    bool group_directories_first;
    OutputFormat format;
    SortMode sort;
    ColorMode color;
    int exit_status;
} Options;

typedef struct {
    char *path;
    char *name;
    char *link_target;
    struct stat metadata;
    unsigned char type_hint;
    bool stat_ok;
    bool command_line_arg;
} FileEntry;

typedef struct {
    FileEntry *items;
    size_t count;
    size_t capacity;
} FileVector;

typedef struct {
    char *path;
    char *display_name;
    bool command_line_arg;
} PendingDirectory;

typedef struct {
    PendingDirectory *items;
    size_t count;
    size_t capacity;
    size_t cursor;
} DirectoryQueue;

static Options options = {
    .format = FORMAT_SINGLE,
    .sort = SORT_NAME,
    .color = COLOR_NEVER
};
static FileVector files;
static DirectoryQueue directories;

static _Noreturn void die_out_of_memory(void)
{
    fputs("recovered-ls: memory exhausted\n", stderr);
    exit(2);
}

static void *xrealloc(void *memory, size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        die_out_of_memory();
    }
    void *result = realloc(memory, count * size);
    if (result == NULL && count != 0) {
        die_out_of_memory();
    }
    return result;
}

static char *xstrdup(const char *text)
{
    char *copy = strdup(text);
    if (copy == NULL) {
        die_out_of_memory();
    }
    return copy;
}

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    bool need_slash = directory_length != 0 && directory[directory_length - 1] != '/';
    char *result = xrealloc(NULL, directory_length + need_slash + name_length + 1, 1);

    memcpy(result, directory, directory_length);
    if (need_slash) {
        result[directory_length++] = '/';
    }
    memcpy(result + directory_length, name, name_length + 1);
    return result;
}

static bool stdout_isatty(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = isatty(STDOUT_FILENO) != 0;
    }
    return cached != 0;
}

static _Noreturn void usage(int status)
{
    FILE *stream = status == 0 ? stdout : stderr;
    fprintf(stream,
            "Usage: recovered-ls [OPTION]... [FILE]...\n"
            "Behavioral reconstruction of the analyzed system ls pipeline.\n\n"
            "  -a, --all                    include entries beginning with .\n"
            "  -A, --almost-all             include hidden entries except . and ..\n"
            "  -d, --directory              list directories themselves\n"
            "  -F, --classify[=WHEN]        append file-type indicators\n"
            "  -h, --human-readable         print human-readable sizes\n"
            "  -i, --inode                  print inode numbers\n"
            "  -l                           use long format\n"
            "  -n, --numeric-uid-gid        numeric owner and group IDs\n"
            "  -r, --reverse                reverse sorting\n"
            "  -R, --recursive              recurse into subdirectories\n"
            "  -s, --size                   print allocated block counts\n"
            "  -S                           sort by size\n"
            "  -t                           sort by modification time\n"
            "  -U                           preserve directory order\n"
            "  -X                           sort by extension\n"
            "  -1                           one entry per line\n"
            "  -C                           simple column output\n"
            "  -m                           comma-separated output\n"
            "      --color[=WHEN]           never, always, or auto\n"
            "      --group-directories-first\n"
            "      --sort=WORD              name, size, time, extension, or none\n"
            "      --help                   display this help\n");
    exit(status);
}

static bool is_dot_or_dotdot(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static bool should_include(const char *name)
{
    if (name[0] != '.') {
        return true;
    }
    if (options.show_all) {
        return true;
    }
    return options.almost_all && !is_dot_or_dotdot(name);
}

static unsigned char inferred_type(const FileEntry *entry)
{
    if (entry->stat_ok) {
        if (S_ISDIR(entry->metadata.st_mode)) {
            return DT_DIR;
        }
        if (S_ISLNK(entry->metadata.st_mode)) {
            return DT_LNK;
        }
        if (S_ISFIFO(entry->metadata.st_mode)) {
            return DT_FIFO;
        }
        if (S_ISSOCK(entry->metadata.st_mode)) {
            return DT_SOCK;
        }
        if (S_ISCHR(entry->metadata.st_mode)) {
            return DT_CHR;
        }
        if (S_ISBLK(entry->metadata.st_mode)) {
            return DT_BLK;
        }
        if (S_ISREG(entry->metadata.st_mode)) {
            return DT_REG;
        }
    }
    return entry->type_hint;
}

static char get_type_indicator(bool stat_ok, mode_t mode, unsigned char type_hint)
{
    unsigned char type = type_hint;
    if (stat_ok) {
        if (S_ISDIR(mode)) {
            type = DT_DIR;
        } else if (S_ISLNK(mode)) {
            type = DT_LNK;
        } else if (S_ISFIFO(mode)) {
            type = DT_FIFO;
        } else if (S_ISSOCK(mode)) {
            type = DT_SOCK;
        } else if (S_ISREG(mode)) {
            type = DT_REG;
        }
    }

    switch (type) {
    case DT_DIR:
        return '/';
    case DT_LNK:
        return '@';
    case DT_FIFO:
        return '|';
    case DT_SOCK:
        return '=';
    case DT_REG:
        return stat_ok && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 ? '*' : '\0';
    default:
        return '\0';
    }
}

static void set_minor_error(bool command_line_arg)
{
    int severity = command_line_arg ? 2 : 1;
    if (options.exit_status < severity) {
        options.exit_status = severity;
    }
}

static void report_access_error(const char *path, bool command_line_arg)
{
    fprintf(stderr, "recovered-ls: cannot access '%s': %s\n", path, strerror(errno));
    set_minor_error(command_line_arg);
}

static char *read_symlink_target(const char *path, const struct stat *metadata)
{
    size_t capacity = metadata->st_size > 0 ? (size_t) metadata->st_size + 1 : 128;
    for (;;) {
        char *buffer = xrealloc(NULL, capacity, 1);
        ssize_t length = readlink(path, buffer, capacity - 1);
        if (length < 0) {
            free(buffer);
            return NULL;
        }
        if ((size_t) length < capacity - 1) {
            buffer[length] = '\0';
            return buffer;
        }
        free(buffer);
        if (capacity > SIZE_MAX / 2) {
            return NULL;
        }
        capacity *= 2;
    }
}

static uintmax_t gobble_file(const char *path,
                             const char *display_name,
                             bool command_line_arg,
                             unsigned char type_hint)
{
    if (files.count == files.capacity) {
        files.capacity = files.capacity == 0 ? 64 : files.capacity * 2;
        files.items = xrealloc(files.items, files.capacity, sizeof(*files.items));
    }

    FileEntry *entry = &files.items[files.count++];
    memset(entry, 0, sizeof(*entry));
    entry->path = xstrdup(path);
    entry->name = xstrdup(display_name);
    entry->type_hint = type_hint;
    entry->command_line_arg = command_line_arg;

    int result = options.dereference ? stat(path, &entry->metadata)
                                     : lstat(path, &entry->metadata);
    if (result == 0) {
        entry->stat_ok = true;
        if (S_ISLNK(entry->metadata.st_mode)) {
            entry->link_target = read_symlink_target(path, &entry->metadata);
        }
        return (uintmax_t) entry->metadata.st_blocks;
    }

    report_access_error(path, command_line_arg);
    return 0;
}

static void clear_files(void)
{
    for (size_t index = 0; index < files.count; ++index) {
        free(files.items[index].path);
        free(files.items[index].name);
        free(files.items[index].link_target);
    }
    files.count = 0;
}

static void queue_directory(const char *path,
                            const char *display_name,
                            bool command_line_arg)
{
    if (directories.count == directories.capacity) {
        directories.capacity = directories.capacity == 0 ? 16 : directories.capacity * 2;
        directories.items = xrealloc(directories.items,
                                     directories.capacity,
                                     sizeof(*directories.items));
    }

    PendingDirectory *pending = &directories.items[directories.count++];
    pending->path = xstrdup(path);
    pending->display_name = xstrdup(display_name);
    pending->command_line_arg = command_line_arg;
}

static bool entry_is_directory(const FileEntry *entry)
{
    return inferred_type(entry) == DT_DIR;
}

static const char *extension_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot == NULL || dot == name ? "" : dot + 1;
}

static int compare_entries(const void *left_pointer, const void *right_pointer)
{
    const FileEntry *left = left_pointer;
    const FileEntry *right = right_pointer;
    int result = 0;

    if (options.group_directories_first) {
        bool left_directory = entry_is_directory(left);
        bool right_directory = entry_is_directory(right);
        if (left_directory != right_directory) {
            result = left_directory ? -1 : 1;
        }
    }

    if (result == 0) {
        switch (options.sort) {
        case SORT_SIZE:
            if (left->metadata.st_size != right->metadata.st_size) {
                result = left->metadata.st_size > right->metadata.st_size ? -1 : 1;
            }
            break;
        case SORT_TIME:
            if (left->metadata.st_mtim.tv_sec != right->metadata.st_mtim.tv_sec) {
                result = left->metadata.st_mtim.tv_sec > right->metadata.st_mtim.tv_sec ? -1 : 1;
            } else if (left->metadata.st_mtim.tv_nsec != right->metadata.st_mtim.tv_nsec) {
                result = left->metadata.st_mtim.tv_nsec > right->metadata.st_mtim.tv_nsec ? -1 : 1;
            }
            break;
        case SORT_EXTENSION:
            result = strcmp(extension_of(left->name), extension_of(right->name));
            break;
        case SORT_NONE:
            result = 0;
            break;
        case SORT_NAME:
            break;
        }
    }

    if (result == 0 && options.sort != SORT_NONE) {
        result = strcoll(left->name, right->name);
    }
    return options.reverse ? -result : result;
}

static void sort_files(void)
{
    if (options.sort != SORT_NONE && files.count > 1) {
        qsort(files.items, files.count, sizeof(*files.items), compare_entries);
    } else if (options.reverse && files.count > 1) {
        for (size_t left = 0, right = files.count - 1; left < right; ++left, --right) {
            FileEntry temporary = files.items[left];
            files.items[left] = files.items[right];
            files.items[right] = temporary;
        }
    }
}

static void mode_string(mode_t mode, char output[11])
{
    output[0] = S_ISDIR(mode) ? 'd' :
                S_ISLNK(mode) ? 'l' :
                S_ISCHR(mode) ? 'c' :
                S_ISBLK(mode) ? 'b' :
                S_ISFIFO(mode) ? 'p' :
                S_ISSOCK(mode) ? 's' : '-';

    static const mode_t bits[] = {
        S_IRUSR, S_IWUSR, S_IXUSR,
        S_IRGRP, S_IWGRP, S_IXGRP,
        S_IROTH, S_IWOTH, S_IXOTH
    };
    static const char marks[] = "rwxrwxrwx";
    for (size_t index = 0; index < 9; ++index) {
        output[index + 1] = (mode & bits[index]) != 0 ? marks[index] : '-';
    }

    if ((mode & S_ISUID) != 0) {
        output[3] = output[3] == 'x' ? 's' : 'S';
    }
    if ((mode & S_ISGID) != 0) {
        output[6] = output[6] == 'x' ? 's' : 'S';
    }
    if ((mode & S_ISVTX) != 0) {
        output[9] = output[9] == 'x' ? 't' : 'T';
    }
    output[10] = '\0';
}

static void format_size(off_t size, char output[32])
{
    if (!options.human_readable) {
        snprintf(output, 32, "%jd", (intmax_t) size);
        return;
    }

    static const char units[] = "BKMGTPE";
    double value = (double) size;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) - 1) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0 || value >= 10.0) {
        snprintf(output, 32, "%.0f%c", value, units[unit]);
    } else {
        snprintf(output, 32, "%.1f%c", value, units[unit]);
    }
}

static const char *color_for(const FileEntry *entry)
{
    if (options.color == COLOR_NEVER ||
        (options.color == COLOR_AUTO && !stdout_isatty())) {
        return "";
    }

    mode_t mode = entry->metadata.st_mode;
    switch (inferred_type(entry)) {
    case DT_DIR:
        return "\033[01;34m";
    case DT_LNK:
        return "\033[01;36m";
    case DT_FIFO:
        return "\033[33m";
    case DT_SOCK:
        return "\033[01;35m";
    case DT_CHR:
    case DT_BLK:
        return "\033[01;33m";
    default:
        return entry->stat_ok && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0
                   ? "\033[01;32m"
                   : "";
    }
}

static void print_colored_name(const FileEntry *entry)
{
    const char *color = color_for(entry);
    bool colored = color[0] != '\0';
    if (colored) {
        fputs(color, stdout);
    }
    fputs(entry->name, stdout);
    if (colored) {
        fputs("\033[0m", stdout);
    }

    if (options.classify) {
        char suffix = get_type_indicator(entry->stat_ok,
                                         entry->metadata.st_mode,
                                         entry->type_hint);
        if (suffix != '\0') {
            putchar(suffix);
        }
    }
}

static void print_long_entry(const FileEntry *entry)
{
    char mode[11] = "??????????";
    char size[32] = "?";
    char time_text[32] = "????????????";
    char owner[32] = "?";
    char group[32] = "?";

    if (entry->stat_ok) {
        mode_string(entry->metadata.st_mode, mode);
        format_size(entry->metadata.st_size, size);

        struct tm timestamp;
        if (localtime_r(&entry->metadata.st_mtime, &timestamp) != NULL) {
            strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M", &timestamp);
        }

        if (options.numeric_ids) {
            snprintf(owner, sizeof(owner), "%ju", (uintmax_t) entry->metadata.st_uid);
            snprintf(group, sizeof(group), "%ju", (uintmax_t) entry->metadata.st_gid);
        } else {
            struct passwd *password = getpwuid(entry->metadata.st_uid);
            struct group *group_entry = getgrgid(entry->metadata.st_gid);
            snprintf(owner,
                     sizeof(owner),
                     "%s",
                     password == NULL ? "?" : password->pw_name);
            snprintf(group,
                     sizeof(group),
                     "%s",
                     group_entry == NULL ? "?" : group_entry->gr_name);
        }
    }

    if (options.print_inode) {
        printf("%8ju ", entry->stat_ok ? (uintmax_t) entry->metadata.st_ino : 0);
    }
    if (options.print_blocks) {
        printf("%6ju ", entry->stat_ok ? (uintmax_t) entry->metadata.st_blocks : 0);
    }
    printf("%s %3ju %-8s %-8s %8s %s ",
           mode,
           entry->stat_ok ? (uintmax_t) entry->metadata.st_nlink : 0,
           owner,
           group,
           size,
           time_text);
    print_colored_name(entry);
    if (entry->link_target != NULL) {
        printf(" -> %s", entry->link_target);
    }
    putchar('\n');
}

static size_t display_length(const FileEntry *entry)
{
    size_t length = strlen(entry->name);
    if (options.classify &&
        get_type_indicator(entry->stat_ok, entry->metadata.st_mode, entry->type_hint) != '\0') {
        ++length;
    }
    return length;
}

static size_t terminal_width(void)
{
    struct winsize window;
    if (stdout_isatty() && ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 &&
        window.ws_col != 0) {
        return window.ws_col;
    }
    return 80;
}

static void print_current_files(void)
{
    if (options.format == FORMAT_LONG) {
        for (size_t index = 0; index < files.count; ++index) {
            print_long_entry(&files.items[index]);
        }
        return;
    }

    if (options.format == FORMAT_COMMAS) {
        for (size_t index = 0; index < files.count; ++index) {
            if (index != 0) {
                fputs(", ", stdout);
            }
            print_colored_name(&files.items[index]);
        }
        if (files.count != 0) {
            putchar('\n');
        }
        return;
    }

    if (options.format == FORMAT_COLUMNS) {
        size_t width = terminal_width();
        size_t column = 0;
        for (size_t index = 0; index < files.count; ++index) {
            size_t entry_length = display_length(&files.items[index]);
            if (column != 0 && column + 2 + entry_length > width) {
                putchar('\n');
                column = 0;
            }
            if (column != 0) {
                fputs("  ", stdout);
                column += 2;
            }
            print_colored_name(&files.items[index]);
            column += entry_length;
        }
        if (column != 0) {
            putchar('\n');
        }
        return;
    }

    for (size_t index = 0; index < files.count; ++index) {
        if (options.print_inode) {
            printf("%8ju ",
                   files.items[index].stat_ok
                       ? (uintmax_t) files.items[index].metadata.st_ino
                       : 0);
        }
        if (options.print_blocks) {
            printf("%6ju ",
                   files.items[index].stat_ok
                       ? (uintmax_t) files.items[index].metadata.st_blocks
                       : 0);
        }
        print_colored_name(&files.items[index]);
        putchar('\n');
    }
}

static void extract_dirs_from_files(void)
{
    size_t retained = 0;
    for (size_t index = 0; index < files.count; ++index) {
        FileEntry *entry = &files.items[index];
        if (!options.directory_as_file && entry_is_directory(entry)) {
            queue_directory(entry->path, entry->name, entry->command_line_arg);
            free(entry->path);
            free(entry->name);
            free(entry->link_target);
        } else {
            if (retained != index) {
                files.items[retained] = files.items[index];
            }
            ++retained;
        }
    }
    files.count = retained;
}

static void print_dir(const char *path,
                      const char *display_name,
                      bool command_line_arg,
                      bool print_header)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        fprintf(stderr,
                "recovered-ls: cannot open directory '%s': %s\n",
                path,
                strerror(errno));
        set_minor_error(command_line_arg);
        return;
    }

    if (print_header) {
        printf("%s:\n", display_name);
    }

    clear_files();
    uintmax_t total_blocks = 0;
    errno = 0;

    for (struct dirent *entry = readdir(directory);
         entry != NULL;
         entry = readdir(directory)) {
        if (!should_include(entry->d_name)) {
            continue;
        }
        char *full_path = join_path(path, entry->d_name);
        total_blocks += gobble_file(full_path, entry->d_name, false, entry->d_type);
        free(full_path);
    }

    if (errno != 0) {
        fprintf(stderr,
                "recovered-ls: reading directory '%s': %s\n",
                path,
                strerror(errno));
        set_minor_error(command_line_arg);
    }
    if (closedir(directory) != 0) {
        fprintf(stderr,
                "recovered-ls: closing directory '%s': %s\n",
                path,
                strerror(errno));
        set_minor_error(command_line_arg);
    }

    sort_files();
    if (options.format == FORMAT_LONG || options.print_blocks) {
        printf("total %ju\n", (total_blocks + 1) / 2);
    }
    print_current_files();

    if (options.recursive) {
        for (size_t index = 0; index < files.count; ++index) {
            FileEntry *entry = &files.items[index];
            if (entry_is_directory(entry) && !is_dot_or_dotdot(entry->name)) {
                char *child_path = join_path(path, entry->name);
                char *child_display = join_path(display_name, entry->name);
                queue_directory(child_path, child_display, false);
                free(child_path);
                free(child_display);
            }
        }
    }
}

static SortMode parse_sort_mode(const char *name)
{
    if (strcmp(name, "name") == 0) {
        return SORT_NAME;
    }
    if (strcmp(name, "size") == 0) {
        return SORT_SIZE;
    }
    if (strcmp(name, "time") == 0) {
        return SORT_TIME;
    }
    if (strcmp(name, "extension") == 0) {
        return SORT_EXTENSION;
    }
    if (strcmp(name, "none") == 0) {
        return SORT_NONE;
    }
    fprintf(stderr, "recovered-ls: unsupported sort mode '%s'\n", name);
    usage(2);
}

static ColorMode parse_color_mode(const char *name)
{
    if (name == NULL || strcmp(name, "always") == 0 || strcmp(name, "yes") == 0) {
        return COLOR_ALWAYS;
    }
    if (strcmp(name, "auto") == 0 || strcmp(name, "tty") == 0) {
        return COLOR_AUTO;
    }
    if (strcmp(name, "never") == 0 || strcmp(name, "no") == 0) {
        return COLOR_NEVER;
    }
    fprintf(stderr, "recovered-ls: unsupported color mode '%s'\n", name);
    usage(2);
}

static void parse_options(int argc, char **argv)
{
    enum {
        OPT_COLOR = 256,
        OPT_GROUP_DIRECTORIES_FIRST,
        OPT_SORT,
        OPT_HELP,
        OPT_VERSION
    };
    static const struct option long_options[] = {
        {"all", no_argument, NULL, 'a'},
        {"almost-all", no_argument, NULL, 'A'},
        {"classify", optional_argument, NULL, 'F'},
        {"color", optional_argument, NULL, OPT_COLOR},
        {"directory", no_argument, NULL, 'd'},
        {"group-directories-first", no_argument, NULL, OPT_GROUP_DIRECTORIES_FIRST},
        {"human-readable", no_argument, NULL, 'h'},
        {"inode", no_argument, NULL, 'i'},
        {"numeric-uid-gid", no_argument, NULL, 'n'},
        {"recursive", no_argument, NULL, 'R'},
        {"reverse", no_argument, NULL, 'r'},
        {"size", no_argument, NULL, 's'},
        {"sort", required_argument, NULL, OPT_SORT},
        {"dereference", no_argument, NULL, 'L'},
        {"help", no_argument, NULL, OPT_HELP},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0}
    };

    int option;
    while ((option = getopt_long(argc, argv, "aA1CdmF::hilnRrsStULX", long_options, NULL)) != -1) {
        switch (option) {
        case 'a':
            options.show_all = true;
            options.almost_all = false;
            break;
        case 'A':
            options.almost_all = true;
            break;
        case '1':
            options.format = FORMAT_SINGLE;
            break;
        case 'C':
            options.format = FORMAT_COLUMNS;
            break;
        case 'd':
            options.directory_as_file = true;
            break;
        case 'F':
            options.classify = optarg == NULL || strcmp(optarg, "never") != 0;
            break;
        case 'f':
            options.show_all = true;
            options.sort = SORT_NONE;
            break;
        case 'h':
            options.human_readable = true;
            break;
        case 'i':
            options.print_inode = true;
            break;
        case 'l':
            options.format = FORMAT_LONG;
            break;
        case 'L':
            options.dereference = true;
            break;
        case 'm':
            options.format = FORMAT_COMMAS;
            break;
        case 'n':
            options.numeric_ids = true;
            options.format = FORMAT_LONG;
            break;
        case 'R':
            options.recursive = true;
            break;
        case 'r':
            options.reverse = true;
            break;
        case 's':
            options.print_blocks = true;
            break;
        case 'S':
            options.sort = SORT_SIZE;
            break;
        case 't':
            options.sort = SORT_TIME;
            break;
        case 'U':
            options.sort = SORT_NONE;
            break;
        case 'X':
            options.sort = SORT_EXTENSION;
            break;
        case OPT_COLOR:
            options.color = parse_color_mode(optarg);
            break;
        case OPT_GROUP_DIRECTORIES_FIRST:
            options.group_directories_first = true;
            break;
        case OPT_SORT:
            options.sort = parse_sort_mode(optarg);
            break;
        case OPT_HELP:
            usage(0);
        case OPT_VERSION:
            puts("recovered-ls 0.1 (behavioral reconstruction)");
            exit(0);
        default:
            usage(2);
        }
    }
}

int main(int argc, char **argv)
{
    parse_options(argc, argv);
    if (options.format == FORMAT_SINGLE && stdout_isatty() && !options.numeric_ids) {
        options.format = FORMAT_COLUMNS;
    }

    if (optind == argc) {
        queue_directory(".", ".", true);
    } else {
        for (int index = optind; index < argc; ++index) {
            struct stat metadata;
            int result = options.dereference ? stat(argv[index], &metadata)
                                             : lstat(argv[index], &metadata);
            if (result == 0 && S_ISDIR(metadata.st_mode) && !options.directory_as_file) {
                queue_directory(argv[index], argv[index], true);
            } else {
                gobble_file(argv[index], argv[index], true, DT_UNKNOWN);
            }
        }
    }

    sort_files();
    extract_dirs_from_files();
    bool printed_files = files.count != 0;
    print_current_files();
    clear_files();

    bool need_headers = directories.count > 1 || printed_files;
    while (directories.cursor < directories.count) {
        PendingDirectory pending = directories.items[directories.cursor++];
        if (directories.cursor > 1 || printed_files) {
            putchar('\n');
        }
        print_dir(pending.path,
                  pending.display_name,
                  pending.command_line_arg,
                  need_headers);
        free(pending.path);
        free(pending.display_name);
    }

    clear_files();
    free(files.items);
    free(directories.items);
    return options.exit_status;
}
