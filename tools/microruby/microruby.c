#include <mruby.h>

#ifdef MRB_NO_STDIO
# error mruby-bin-mruby conflicts 'MRB_NO_STDIO' in your build configuration
#endif

#include <stdlib.h>
#include <string.h>
#include <mruby/array.h>
//#include <mruby/compile.h>
//#include <mruby/dump.h>
#include <mruby/variable.h>
//#include <mruby/proc.h>

#include <picorbc.h>

#if defined(_WIN32) || defined(_WIN64)
# include <io.h> /* for setmode */
# include <fcntl.h>
#endif

struct _args {
  FILE *rfp;
  char *cmdline;
  mrb_bool fname        : 1;
  mrb_bool mrbfile      : 1;
  mrb_bool check_syntax : 1;
  mrb_bool verbose      : 1;
  mrb_bool version      : 1;
  mrb_bool debug        : 1;
  int argc;
  char **argv;
  int libc;
  char **libv;
};

struct options {
  int argc;
  char **argv;
  char *program;
  char *opt;
  char short_opt[2];
};

static void
usage(const char *name)
{
  static const char *const usage_msg[] = {
  "switches:",
  "-b           load and execute RiteBinary (mrb) file",
  "-c           check syntax only",
  "-d           set debugging flags (set $DEBUG to true)",
  "-e 'command' one line of script",
  "-r library   load the library before executing your script",
  "-v           print version number, then run in verbose mode",
  "--verbose    run in verbose mode",
  "--version    print the version",
  "--copyright  print the copyright",
  NULL
  };
  const char *const *p = usage_msg;

  printf("Usage: %s [switches] [programfile] [arguments]\n", name);
  while (*p)
    printf("  %s\n", *p++);
}

static void
options_init(struct options *opts, int argc, char **argv)
{
  opts->argc = argc;
  opts->argv = argv;
  opts->program = *argv;
  *opts->short_opt = 0;
}

static const char *
options_opt(struct options *opts)
{
  /* concatenated short options (e.g. `-cv`) */
  if (*opts->short_opt && *++opts->opt) {
   short_opt:
    opts->short_opt[0] = *opts->opt;
    opts->short_opt[1] = 0;
    return opts->short_opt;
  }

  while (++opts->argv, --opts->argc) {
    opts->opt = *opts->argv;

    /*  empty         || not start with `-`  || `-` */
    if (!opts->opt[0] || opts->opt[0] != '-' || !opts->opt[1]) return NULL;

    if (opts->opt[1] == '-') {
      /* `--` */
      if (!opts->opt[2]) {
        ++opts->argv, --opts->argc;
        return NULL;
      }
      /* long option */
      opts->opt += 2;
      *opts->short_opt = 0;
      return opts->opt;
    }
    else {
      /* short option */
      ++opts->opt;
      goto short_opt;
    }
  }
  return NULL;
}

static const char *
options_arg(struct options *opts)
{
  if (*opts->short_opt && opts->opt[1]) {
    /* concatenated short option and option argument (e.g. `-rLIBRARY`) */
    *opts->short_opt = 0;
    return opts->opt + 1;
  }
  --opts->argc, ++opts->argv;
  return opts->argc ? *opts->argv : NULL;
}

static char *
dup_arg_item(mrb_state *mrb, const char *item)
{
  size_t buflen = strlen(item) + 1;
  char *buf = (char*)mrb_malloc(mrb, buflen);
  memcpy(buf, item, buflen);
  return buf;
}

static int
parse_args(mrb_state *mrb, int argc, char **argv, struct _args *args)
{
  static const struct _args args_zero = { 0 };
  struct options opts[1];
  const char *opt, *item;

  *args = args_zero;
  options_init(opts, argc, argv);
  while ((opt = options_opt(opts))) {
    if (strcmp(opt, "b") == 0) {
      args->mrbfile = TRUE;
    }
    else if (strcmp(opt, "c") == 0) {
      args->check_syntax = TRUE;
    }
    else if (strcmp(opt, "d") == 0) {
      args->debug = TRUE;
    }
    else if (strcmp(opt, "e") == 0) {
      if ((item = options_arg(opts))) {
        if (!args->cmdline) {
          args->cmdline = dup_arg_item(mrb, item);
        }
        else {
          size_t cmdlinelen;
          size_t itemlen;

          cmdlinelen = strlen(args->cmdline);
          itemlen = strlen(item);
          args->cmdline =
            (char *)mrb_realloc(mrb, args->cmdline, cmdlinelen + itemlen + 2);
          args->cmdline[cmdlinelen] = '\n';
          memcpy(args->cmdline + cmdlinelen + 1, item, itemlen + 1);
        }
      }
      else {
        fprintf(stderr, "%s: No code specified for -e\n", opts->program);
        return EXIT_FAILURE;
      }
    }
    else if (strcmp(opt, "h") == 0) {
      usage(opts->program);
      exit(EXIT_SUCCESS);
    }
    else if (strcmp(opt, "r") == 0) {
      if ((item = options_arg(opts))) {
        if (args->libc == 0) {
          args->libv = (char**)mrb_malloc(mrb, sizeof(char*));
        }
        else {
          args->libv = (char**)mrb_realloc(mrb, args->libv, sizeof(char*) * (args->libc + 1));
        }
        args->libv[args->libc++] = dup_arg_item(mrb, item);
      }
      else {
        fprintf(stderr, "%s: No library specified for -r\n", opts->program);
        return EXIT_FAILURE;
      }
    }
    else if (strcmp(opt, "v") == 0) {
      if (!args->verbose) {
        mrb_show_version(mrb);
        args->version = TRUE;
      }
      args->verbose = TRUE;
    }
    else if (strcmp(opt, "version") == 0) {
      mrb_show_version(mrb);
      exit(EXIT_SUCCESS);
    }
    else if (strcmp(opt, "verbose") == 0) {
      args->verbose = TRUE;
    }
    else if (strcmp(opt, "copyright") == 0) {
      mrb_show_copyright(mrb);
      exit(EXIT_SUCCESS);
    }
    else {
      fprintf(stderr, "%s: invalid option %s%s (-h will show valid options)\n",
              opts->program, opt[1] ? "--" : "-", opt);
      return EXIT_FAILURE;
    }
  }

  argc = opts->argc; argv = opts->argv;
  if (args->cmdline == NULL) {
    if (*argv == NULL) {
      if (args->version) exit(EXIT_SUCCESS);
      args->rfp = stdin;
    }
    else {
      args->rfp = strcmp(argv[0], "-") == 0 ?
        stdin : fopen(argv[0], "rb");
      if (args->rfp == NULL) {
        fprintf(stderr, "%s: Cannot open program file: %s\n", opts->program, argv[0]);
        return EXIT_FAILURE;
      }
      args->fname = TRUE;
      args->cmdline = argv[0];
      argc--; argv++;
    }
  }
#if defined(_WIN32) || defined(_WIN64)
  if (args->rfp == stdin) {
    _setmode(_fileno(stdin), O_BINARY);
  }
#endif
  args->argv = (char **)mrb_realloc(mrb, args->argv, sizeof(char*) * (argc + 1));
  memcpy(args->argv, argv, (argc+1) * sizeof(char*));
  args->argc = argc;

  return EXIT_SUCCESS;
}

static void
cleanup(mrb_state *mrb, struct _args *args)
{
  if (args->rfp && args->rfp != stdin)
    fclose(args->rfp);
  if (!args->fname)
    mrb_free(mrb, args->cmdline);
  mrb_free(mrb, args->argv);
  if (args->libc) {
    while (args->libc--) {
      mrb_free(mrb, args->libv[args->libc]);
    }
    mrb_free(mrb, args->libv);
  }
  mrb_close(mrb);
}


struct mrb_parser_state;

/* from compile.h */
typedef struct mrbc_context {
  mrb_sym *syms;
  int slen;
  char *filename;
  uint16_t lineno;
  int (*partial_hook)(struct mrb_parser_state*);
  void *partial_data;
  struct RClass *target_class;
  mrb_bool capture_errors:1;
  mrb_bool dump_result:1;
  mrb_bool no_exec:1;
  mrb_bool keep_lv:1;
  mrb_bool no_optimize:1;
  const struct RProc *upper;

  size_t parser_nerr;
} mrbc_context;

/* from parse.y */
MRB_API const char*
mrbc_filename(mrb_state *mrb, mrbc_context *c, const char *s)
{
  if (s) {
    size_t len = strlen(s);
    char *p = (char *)mrb_malloc(mrb, len + 1);

    memcpy(p, s, len + 1);
    if (c->filename) {
      mrb_free(mrb, c->filename);
    }
    c->filename = p;
  }
  return c->filename;
}

/* from parse.y */
MRB_API mrbc_context*
mrbc_context_new(mrb_state *mrb)
{
  return (mrbc_context *)mrb_calloc(mrb, 1, sizeof(mrbc_context));
}

/* from parse.y */
MRB_API void
mrbc_context_free(mrb_state *mrb, mrbc_context *cxt)
{
  mrb_free(mrb, cxt->filename);
  mrb_free(mrb, cxt->syms);
  mrb_free(mrb, cxt);
}

/* from proc.h */
struct REnv {
  MRB_OBJECT_HEADER;
  mrb_value *stack;
  struct mrb_context *cxt;
  mrb_sym mid;
};

/* from proc.h */
static inline struct REnv *
mrb_vm_ci_env(const mrb_callinfo *ci)
{
  if (ci->u.env && ci->u.env->tt == MRB_TT_ENV) {
    return ci->u.env;
  }
  else {
    return NULL;
  }
}

/* from proc.h */
static inline void
mrb_vm_ci_env_set(mrb_callinfo *ci, struct REnv *e)
{
  if (ci->u.env) {
    if (ci->u.env->tt == MRB_TT_ENV) {
      if (e) {
        e->c = ci->u.env->c;
        ci->u.env = e;
      }
      else {
        ci->u.target_class = ci->u.env->c;
      }
    }
    else {
      if (e) {
        e->c = ci->u.target_class;
        ci->u.env = e;
      }
    }
  }
  else {
    ci->u.env = e;
  }
}

/* from proc.h */
void mrb_env_unshare(mrb_state*, struct REnv*);




MRB_API mrb_value mrb_load_irep(mrb_state *mrb, const uint8_t *bin);

int loglevel = 1;

MRB_API mrb_value
mrb_load_string(mrb_state *mrb, const char *str)
{
  mrb_value ret;
  StreamInterface *si = StreamInterface_new((char *)str, STREAM_TYPE_MEMORY);
  ParserState *p = Compiler_parseInitState(si->node_box_size);
  if (Compiler_compile(p, si)) {
    mrb_load_irep(mrb, p->scope->vm_code);
  } else {
  }
  return ret;
}

MRB_API mrb_value
mrb_load_string_cxt(mrb_state *mrb, const char *s, mrbc_context *c)
{
  //return mrb_load_nstring_cxt(mrb, s, strlen(s), c);
  return mrb_load_string(mrb, s);
}

/* from parse.y */
MRB_API void
mrbc_cleanup_local_variables(mrb_state *mrb, mrbc_context *c)
{
  if (c->syms) {
    mrb_free(mrb, c->syms);
    c->syms = NULL;
    c->slen = 0;
  }
}





int
main(int argc, char **argv)
{
  mrb_state *mrb = mrb_open();
  int n = -1;
  int i;
  struct _args args;
  mrb_value ARGV;
  mrbc_context *c;
  mrb_value v;
  mrb_sym zero_sym;

  if (mrb == NULL) {
    fprintf(stderr, "%s: Invalid mrb_state, exiting mruby\n", *argv);
    return EXIT_FAILURE;
  }

  n = parse_args(mrb, argc, argv, &args);
  if (n == EXIT_FAILURE || (args.cmdline == NULL && args.rfp == NULL)) {
    cleanup(mrb, &args);
    return n;
  }
  else {
    int ai = mrb_gc_arena_save(mrb);
    ARGV = mrb_ary_new_capa(mrb, args.argc);
    for (i = 0; i < args.argc; i++) {
      char* utf8 = mrb_utf8_from_locale(args.argv[i], -1);
      if (utf8) {
        mrb_ary_push(mrb, ARGV, mrb_str_new_cstr(mrb, utf8));
        mrb_utf8_free(utf8);
      }
    }
    mrb_define_global_const(mrb, "ARGV", ARGV);
    mrb_gv_set(mrb, mrb_intern_lit(mrb, "$DEBUG"), mrb_bool_value(args.debug));

    c = mrbc_context_new(mrb);
    if (args.verbose)
      c->dump_result = TRUE;
    if (args.check_syntax)
      c->no_exec = TRUE;

    /* Set $0 */
    zero_sym = mrb_intern_lit(mrb, "$0");
    if (args.rfp) {
      const char *cmdline;
      cmdline = args.cmdline ? args.cmdline : "-";
      mrbc_filename(mrb, c, cmdline);
      mrb_gv_set(mrb, zero_sym, mrb_str_new_cstr(mrb, cmdline));
    }
    else {
      mrbc_filename(mrb, c, "-e");
      mrb_gv_set(mrb, zero_sym, mrb_str_new_lit(mrb, "-e"));
    }

    /* Load libraries */
    for (i = 0; i < args.libc; i++) {
      struct REnv *e;
      FILE *lfp = fopen(args.libv[i], "rb");
      if (lfp == NULL) {
        fprintf(stderr, "%s: Cannot open library file: %s\n", *argv, args.libv[i]);
        mrbc_context_free(mrb, c);
        cleanup(mrb, &args);
        return EXIT_FAILURE;
      }
      if (args.mrbfile) {
//        v = mrb_load_irep_file_cxt(mrb, lfp, c);
      }
      else {
//        v = mrb_load_detect_file_cxt(mrb, lfp, c);
      }
      fclose(lfp);
      e = mrb_vm_ci_env(mrb->c->cibase);
      mrb_vm_ci_env_set(mrb->c->cibase, NULL);
      mrb_env_unshare(mrb, e);
      mrbc_cleanup_local_variables(mrb, c);
    }

    /* Load program */
    if (args.mrbfile) {
//      v = mrb_load_irep_file_cxt(mrb, args.rfp, c);
    }
    else if (args.rfp) {
//      v = mrb_load_detect_file_cxt(mrb, args.rfp, c);
    }
    else {
      char* utf8 = mrb_utf8_from_locale(args.cmdline, -1);
      if (!utf8) abort();
//      v = mrb_load_string_cxt(mrb, utf8, c);
v = mrb_load_string(mrb, utf8);
      mrb_utf8_free(utf8);
    }

    mrb_gc_arena_restore(mrb, ai);
    mrbc_context_free(mrb, c);
    if (mrb->exc) {
      if (!mrb_undef_p(v)) {
        mrb_print_error(mrb);
      }
      n = EXIT_FAILURE;
    }
    else if (args.check_syntax) {
      puts("Syntax OK");
    }
  }
  cleanup(mrb, &args);

  return n;
}
