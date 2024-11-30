/***************************************************************************************
 * Copyright (c) 2014-2024 Zihao Yu, Nanjing University
 *
 * NEMU is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 ***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <memory/paddr.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char *rl_gets()
{
  static char *line_read = NULL;

  if (line_read)
  {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read)
  {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args)
{
  cpu_exec(-1);
  return 0;
}

static int cmd_q(char *args)
{
  return -1;
}

static int cmd_help(char *args);

static int cmd_si(char *args);

static int cmd_info(char *args);

static int cmd_x(char *args);

static int cmd_p(char *args);

static struct
{
  const char *name;
  const char *description;
  int (*handler)(char *);
} cmd_table[] = {
    {"help", "Display information about all supported commands", cmd_help},
    {"c", "Continue the execution of the program", cmd_c},
    {"q", "Exit NEMU", cmd_q},

    /* TODO: Add more commands */
    {"si", "Execute the program N instructions", cmd_si},
    {"info", "Print the register info", cmd_info},
    {"x", "Print the memory info", cmd_x},
    {"p", "Enter the expr to get the result", cmd_p},
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_p(char *args)
{
  bool success;
  long expr_result = (long)(expr(args, &success));

  if (!success)
  {
    printf("Failed to evaluate expression: %s\n", args);
    return 0;
  }
  else
  {
    printf("%ld\n", expr_result);
    return 0;
  }
}

static int cmd_x(char *args)
{
  char *arg1 = strtok(NULL, " ");
  char *arg2 = strtok(NULL, " ");
  if (arg1 == NULL || arg2 == NULL)
  {
    printf("Usage: x N EXPR\n");
    return 0;
  }
  else
  {
    int N;
    paddr_t start_addr;
    bool success;

    // Convert N to integer
    N = atoi(arg1);

    // Evaluate expression in arg2 to obtain starting address
    start_addr = expr(arg2, &success);

    if (!success)
    {
      printf("Failed to evaluate expression: %s\n", arg2);
      return 0;
    }

    for (int i = 0; i < N; i++)
    {
      word_t data = paddr_read(start_addr, 4);
      printf("0x%08x: 0x%08x\n", start_addr, data);
      start_addr += 4;
    }
    return 0;
  }
}

static int cmd_info(char *args)
{
  char *arg = strtok(NULL, " ");

  if (arg == NULL)
    printf("Unknown command not found!\ntype 'info r'\n");

  else if (strcmp(arg, "r") == 0)
  {
    isa_reg_display();
  }
  else
    printf("Unknown command '%s'\ntype 'info r'\n", arg);
  return 0;
}

static int cmd_si(char *args)
{
  char *arg = strtok(NULL, " ");

  if (arg == NULL)
  {
    cpu_exec(1);
  }
  else if (arg != NULL)
  {
    int exec_num = atoi(arg);
    cpu_exec(exec_num);
  }
  return 0;
}

static int cmd_help(char *args)
{
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL)
  {
    /* no argument given */
    for (i = 0; i < NR_CMD; i++)
    {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else
  {
    for (i = 0; i < NR_CMD; i++)
    {
      if (strcmp(arg, cmd_table[i].name) == 0)
      {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode()
{
  is_batch_mode = true;
}

void sdb_mainloop()
{
  if (is_batch_mode)
  {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL;)
  {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL)
    {
      continue;
    }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end)
    {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i++)
    {
      if (strcmp(cmd, cmd_table[i].name) == 0)
      {
        if (cmd_table[i].handler(args) < 0)
        {
          return;
        }
        break;
      }
    }

    if (i == NR_CMD)
    {
      printf("Unknown command '%s'\n", cmd);
    }
  }
}

void init_sdb()
{
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
