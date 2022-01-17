#include <mruby.h>
#include "../src/picorbc.h"

mrb_value mrb_load_irep(mrb_state *mrb, const uint8_t *bin);

int loglevel = 1;

int
mrb_load_string(mrb_state *mrb, char *str)
{
  int ret;
  StreamInterface *si = StreamInterface_new(str, STREAM_TYPE_MEMORY);
  ParserState *p = Compiler_parseInitState(si->node_box_size);
  if (Compiler_compile(p, si)) {
    mrb_load_irep(mrb, p->scope->vm_code);
    ret = 0;
  } else {
    ret = 1;
  }
  return ret;
}

int
main(void)
{
  mrb_state *mrb = mrb_open();
  int ret = mrb_load_string(mrb, "puts 'hello world'");
  mrb_close(mrb);
  return ret;
}
