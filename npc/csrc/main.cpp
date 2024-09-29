#include <nvboard.h>
#include <Vtop.h>
#include <verilated.h>
#include <verilated_vcd_c.h>
static TOP_NAME top;

void nvboard_bind_all_pins(TOP_NAME *top);

int main()
{
  nvboard_bind_all_pins(&top);
  nvboard_init();

  while (1)
  {
    nvboard_update();
    top.eval();
  }
  return 0;
}
