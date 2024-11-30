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

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>
#include <memory/paddr.h>
enum
{
  TK_NOTYPE = 256,
  TK_EQ = 1,   // ==
  TK_HEX = 2,  // 0x
  TK_NUM = 3,  // 0~9
  TK_PLUS,     // +
  TK_MINUS,    // -
  TK_MULTIPLY, // * is multiplied by
  TK_DIVIDE,   // / is divided by
  TK_ILT,      // < is less than
  TK_IMT,      // > is more than
  TK_ILET,     // <= is less than or equal to
  TK_IMET,     // >= is more than or equal to
  TK_LPAREN,   // ( open parenthesis
  TK_RPAREN,   // ) close parenthesis
  TK_REG,      // register
  TK_X,        // variable
  TK_DRF,      // dereferenced
  TK_NEG       // negative

  /* TODO: Add more token types */

};

static struct rule
{
  const char *regex;
  int token_type;
} rules[] = {

    /* TODO: Add more rules.
     * Pay attention to the precedence level of different rules.
     */

    {" +", TK_NOTYPE},                // spaces
    {"==", TK_EQ},                    // equal
    {"0x[0-9a-fA-F]+", TK_HEX},       // hex
    {"[0-9]+", TK_NUM},               // num
    {"\\+", TK_PLUS},                 // +
    {"\\-", TK_MINUS},                // -
    {"\\*", TK_MULTIPLY},             // *
    {"\\/", TK_DIVIDE},               // /
    {"<=", TK_ILET},                  // <=
    {">=", TK_IMET},                  // >=
    {"<", TK_ILT},                    // <
    {">", TK_IMT},                    // >
    {"\\(", TK_LPAREN},               // (
    {"\\)", TK_RPAREN},               // )
    {"\\$[a-zA-Z0-9]+", TK_REG},      // register
    {"[a-zA-Z_][a-zA-Z0-9_]*", TK_X}, // veriable
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex()
{
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i++)
  {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0)
    {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token
{
  int type;
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used)) = 0;

void copy_token_str(char *dest, const char *src, int len)
{
  if (len < sizeof(tokens[nr_token].str))
  {
    strncpy(dest, src, len);
    dest[len] = '\0';
  }
  else
  {
    strncpy(dest, src, sizeof(tokens[nr_token].str) - 1);
    dest[sizeof(tokens[nr_token].str) - 1] = '\0';
  }
}

static bool make_token(char *e)
{
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0')
  {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i++)
    {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0)
      {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type)
        {
        // notype
        case TK_NOTYPE:
          break;

        // equal
        case TK_EQ:
          tokens[nr_token].type = TK_EQ;
          tokens[nr_token].str[0] = '\0';
          break;

        // hex
        case TK_HEX:
          tokens[nr_token].type = TK_HEX;
          copy_token_str(tokens[nr_token].str, substr_start, substr_len);
          break;

        // num
        case TK_NUM:
          tokens[nr_token].type = TK_NUM;
          copy_token_str(tokens[nr_token].str, substr_start, substr_len);
          break;

        // +
        case TK_PLUS:
          tokens[nr_token].type = TK_PLUS;
          tokens[nr_token].str[0] = '\0';
          break;

        // -
        case TK_MINUS:
          if (nr_token == 0 || (tokens[nr_token - 1].type != TK_NUM &&
                                tokens[nr_token - 1].type != TK_HEX &&
                                tokens[nr_token - 1].type != TK_REG &&
                                tokens[nr_token - 1].type != TK_RPAREN &&
                                tokens[nr_token - 1].type != TK_X))
          {
            tokens[nr_token].type = TK_NEG; // dereference
          }
          else
          {
            tokens[nr_token].type = TK_MINUS; // multiplication
          }
          tokens[nr_token].str[0] = '\0';
          break;

        // *
        case TK_MULTIPLY:
          // 如果前一个 token 不是数字、十六进制、寄存器、右括号或变量，
          // 那么此处的 * 应被识别为解引用符号，否则是乘法。
          if (nr_token == 0 || (tokens[nr_token - 1].type != TK_NUM &&
                                tokens[nr_token - 1].type != TK_HEX &&
                                tokens[nr_token - 1].type != TK_REG &&
                                tokens[nr_token - 1].type != TK_RPAREN &&
                                tokens[nr_token - 1].type != TK_X))
          {
            tokens[nr_token].type = TK_DRF; // dereference
          }
          else
          {
            tokens[nr_token].type = TK_MULTIPLY; // multiplication
          }
          tokens[nr_token].str[0] = '\0';
          break;

        // /
        case TK_DIVIDE:
          tokens[nr_token].type = TK_DIVIDE;
          tokens[nr_token].str[0] = '\0';
          break;

        // <=
        case TK_ILET:
          tokens[nr_token].type = TK_ILET;
          tokens[nr_token].str[0] = '\0';
          break;

        // <=
        case TK_IMET:
          tokens[nr_token].type = TK_IMET;
          tokens[nr_token].str[0] = '\0';
          break;

        // <
        case TK_ILT:
          tokens[nr_token].type = TK_ILT;
          tokens[nr_token].str[0] = '\0';
          break;

        // >
        case TK_IMT:
          tokens[nr_token].type = TK_IMT;
          tokens[nr_token].str[0] = '\0';
          break;

        // (
        case TK_LPAREN:
          tokens[nr_token].type = TK_LPAREN;
          tokens[nr_token].str[0] = '\0';
          break;

        // )
        case TK_RPAREN:
          tokens[nr_token].type = TK_RPAREN;
          tokens[nr_token].str[0] = '\0';
          break;

        // register
        case TK_REG:
          tokens[nr_token].type = TK_REG;
          copy_token_str(tokens[nr_token].str, substr_start, substr_len);
          break;

        // veriable
        case TK_X:
          tokens[nr_token].type = TK_X;
          copy_token_str(tokens[nr_token].str, substr_start, substr_len);
          break;

        default:
          printf("Unknown token type %d at position %d\n", rules[i].token_type, position);
          assert(0);
        }
        nr_token++;
        break;
      }
    }

    if (i == NR_REGEX)
    {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

bool check_parentheses(int p, int q)
{
  int count = 0;
  bool is_parentheses = tokens[p].type == TK_LPAREN && tokens[q].type == TK_RPAREN;

  for (int i = p; i <= q; i++)
  {
    if (tokens[i].type == TK_LPAREN)
    {
      count++;
    }
    else if (tokens[i].type == TK_RPAREN)
    {
      count--;
      if (count < 0)
      {
        return false;
      }
    }
  }

  return (count == 0) && is_parentheses;
}

int get_priority(int type)
{
  switch (type)
  {
  case TK_NOTYPE:
    return 0;
  case TK_LPAREN:
  case TK_RPAREN:
    return 1;
  case TK_DRF: // 解引用
  case TK_NEG:
    return 2;
  case TK_MULTIPLY:
  case TK_DIVIDE:
    return 3;
  case TK_PLUS:
  case TK_MINUS:
    return 4;
  case TK_ILT:
  case TK_IMT:
  case TK_ILET:
  case TK_IMET:
    return 5;
  case TK_EQ:
    return 6;
  default:
    return 7; // 数字、寄存器等操作数
  }
}

// 判断是否为单目运算符
bool is_unary(int type)
{
  return type == TK_DRF ||
         type == TK_NEG;
}

typedef struct
{
  uint32_t value;
  bool success;
} eval_result_t;

// 基本运算函数
eval_result_t eval_add(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a + b, .success = true};
}

eval_result_t eval_sub(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a - b, .success = true};
}

eval_result_t eval_mul(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a * b, .success = true};
}

eval_result_t eval_div(uint32_t a, uint32_t b)
{
  if (b == 0)
  {
    return (eval_result_t){.value = 0, .success = false};
  }
  return (eval_result_t){.value = a / b, .success = true};
}

// 比较运算函数
eval_result_t eval_eq(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a == b, .success = true};
}

eval_result_t eval_lt(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a < b, .success = true};
}

eval_result_t eval_gt(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a > b, .success = true};
}

eval_result_t eval_le(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a <= b, .success = true};
}

eval_result_t eval_ge(uint32_t a, uint32_t b)
{
  return (eval_result_t){.value = a >= b, .success = true};
}

eval_result_t eval_neg(uint32_t a)
{
  return (eval_result_t){.value = 0 - a, .success = true};
}

eval_result_t eval_reg(char *reg_name)
{
  bool success = true;
  word_t value = isa_reg_str2val(reg_name, &success);
  return (eval_result_t){.value = value, .success = success};
}

eval_result_t eval_deref(uint32_t add_name)
{
  paddr_t start_addr;

  // Evaluate expression in arg2 to obtain starting address
  start_addr = add_name;

  word_t data = paddr_read(start_addr, 4);

  // word_t value = paddr_read(addr, 4);
  return (eval_result_t){.value = data, .success = true};
}

eval_result_t eval_op(int op_type, uint32_t val1, uint32_t val2)
{
  switch (op_type)
  {
  case TK_PLUS:
    return eval_add(val1, val2);
  case TK_MINUS:
    return eval_sub(val1, val2);
  case TK_MULTIPLY:
    return eval_mul(val1, val2);
  case TK_DIVIDE:
    return eval_div(val1, val2);
  case TK_EQ:
    return eval_eq(val1, val2);
  case TK_ILT:
    return eval_lt(val1, val2);
  case TK_IMT:
    return eval_gt(val1, val2);
  case TK_ILET:
    return eval_le(val1, val2);
  case TK_IMET:
    return eval_ge(val1, val2);
  default:
    return (eval_result_t){.value = 0, .success = false};
  }
}

int find_main_operator(int p, int q)
{
  int main_op = -1;
  int min_priority = -1;
  int parentheses = 0;

  for (int i = q; i >= p; i--)
  {
    if (tokens[i].type == TK_NOTYPE)
      continue;
    switch (tokens[i].type)
    {
    case TK_RPAREN:
      parentheses++;
      break;
    case TK_LPAREN:
      parentheses--;
      break;
    default:
      if (parentheses == 0)
      {
        int cur_priority = get_priority(tokens[i].type);
        if (cur_priority != 7 &&
            (min_priority == -1 || cur_priority <= min_priority) &&
            !is_unary(tokens[i].type))
        {
          min_priority = cur_priority;
          main_op = i;
        }
      }
      break;
    }
  }

  return main_op;
}

word_t eval_expr(int p, int q, bool *success)
{
  printf("Evaluating expression from %d to %d\n", p, q);
  for (int i = p; i <= q; i++)
  {
    printf("Token %d: type=%d, str=%s\n", i, tokens[i].type, tokens[i].str);
  }

  if (p > q)
  {
    *success = false;
    return 0;
  }

  if (p == q)
  {
    switch (tokens[p].type)
    {
    case TK_NUM:
      *success = true;
      return strtoul(tokens[p].str, NULL, 10);
    case TK_HEX:
      *success = true;
      return strtoul(tokens[p].str + 2, NULL, 16);
    case TK_REG:
    {
      eval_result_t result = eval_reg(tokens[p].str);
      *success = result.success;
      return result.value;
    }
    default:
      *success = false;
      return 0;
    }
  }

  // 如果第一个token是负号，直接处理
  if (tokens[p].type == TK_NEG)
  {
    word_t val = eval_expr(p + 1, q, success);
    if (!*success)
      return 0;
    eval_result_t result = eval_neg(val);
    *success = result.success;
    return result.value;
  }

  if (tokens[p].type == TK_DRF)
  { // 添加对解引用的处理
    word_t addr = eval_expr(p + 1, q, success);
    if (!*success)
      return 0;

    eval_result_t result = eval_deref(addr);
    *success = result.success;
    return result.value;
  }

  if (check_parentheses(p, q))
  {
    return eval_expr(p + 1, q - 1, success);
  }

  int op = find_main_operator(p, q);
  if (op == -1)
  {
    *success = false;
    return 0;
  }

  if (is_unary(tokens[op].type))
  {
    word_t val = eval_expr(op + 1, q, success);
    if (!*success)
      return 0;

    eval_result_t result;
    switch (tokens[op].type)
    {
    case TK_NEG:
      result = eval_neg(val);
      break;
    case TK_DRF:
      result = eval_deref(val);
      break;
    default:
      *success = false;
      return 0;
    }
    *success = result.success;
    return result.value;
  }

  word_t val1 = eval_expr(p, op - 1, success);
  if (!*success)
    return 0;
  word_t val2 = eval_expr(op + 1, q, success);
  if (!*success)
    return 0;

  eval_result_t result = eval_op(tokens[op].type, val1, val2);
  *success = result.success;
  return result.value;
}

word_t expr(char *e, bool *success)
{
  if (!make_token(e))
  {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */

  return eval_expr(0, nr_token - 1, success);

  return 0;
}
