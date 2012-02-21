/* File: base.c
   Time-stamp: <2012-02-21 19:10:14 gawen>

   Copyright (C) 2011 David Hauweele <david@hauweele.net>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>. */

/* TODO:
    cannot do this otherwise than reading string by string and convert them
    at first we just we just use large integers in the future */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <err.h>

#include "safe-call.h"

/* Since we are still using large integer for now
   some mechanism will override this to avoid
   integer overflow. */
#define MAX_LINE          4096

/* Default symbols will ensure the usual syntax in different bases. */
#define DEFAULT_MINUS     '-'
#define DEFAULT_EXPONENT  'E'
#define DEFAULT_SEPARATOR '.'
#define DEFAULT_SYMBOLS   "0123456789"                                  \
                          "abcdefghijklmnopqrstuvwxyz"                  \
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

static char *input_symbols  = "0123456789";
static char *output_symbols = DEFAULT_SYMBOLS;
static char separator       = DEFAULT_SEPARATOR;
static char exponent        = DEFAULT_EXPONENT;
static char minus           = DEFAULT_MINUS;
static int  input_base;
static int  output_base;
static bool leflag;

static const char *prog_name;

struct opts_name {
  char name_short;
  const char *name_long;
  const char *help;
};

/* Auto-Indentation of the standard help message
   according to the options specified in names. */
static void help(const struct opts_name *names)
{
  const struct opts_name *opt;
  int size;
  int max = 0;

  /* basic usage */
  fprintf(stderr, "Usage: %s [OPTIONS] [SOCKET-PATH]\n", prog_name);

  /* maximum option name size for padding */
  for(opt = names ; opt->name_long ; opt++) {
    size = strlen(opt->name_long);
    if(size > max)
      max = size;
  }

  /* print options and help messages */
  for(opt = names ; opt->name_long ; opt++) {
    if(opt->name_short != 0)
      fprintf(stderr, "  -%c, --%s", opt->name_short, opt->name_long);
    else
      fprintf(stderr, "      --%s", opt->name_long);

    /* padding */
    size = strlen(opt->name_long);
    for(; size <= max ; size++)
      fputc(' ', stderr);
    fprintf(stderr, "%s\n", opt->help);
  }
}

/* This ensure that the string in argument
   could be represented using the currently
   defined symbols. And return an integer
   to represent the base.

   Mainly used while parsing the command line. */
static int check_base(const char *arg, bool in)
{
  register int base = atoi(arg);

  if(base < 2 || base >= sizeof(DEFAULT_SYMBOLS))
    errx(EXIT_FAILURE, "%s base out of range",
         in ? "Input" : "Output");

  return base;
}

/* Check that the specified string is constitued of only
   one charater and then return this character.

   Mainly used while parsing the command line. */
static char check_character(const char *arg, const char *type)
{
  if(strlen(arg) != 1)
    errx(EXIT_FAILURE, "Invalid %s character", type);

  return arg[0];
}

/* Return the value associated to the specified symbol
   according to the input symbols or fail if out of range. */
static unsigned int input_symbols_value(char c)
{
  unsigned int i = 0;

  for(; input_symbols[i] != c && input_symbols[i] != '\0' ; i++);
  if(input_symbols[i] == '\0')
    errx(1, "Symbol invalid according to the specified input base");

  return i;
}

/* This function convert the line from the input-base parameters
   to the output base parameters. */
static void convert(const char *line)
{
  char output[MAX_LINE];
  int i;

  uint64_t overflow        = 0;
  uint64_t integer_part    = 0;
  uint64_t fractional_part = 0;

  bool minus_mode    = false;
  bool floating_mode = false;
  bool exponent_mode = false;

  if(line[0] == minus) {
    minus_mode = true;
    line++;
  }

  /* convert from input base */
  for(const char *s = line ; *s != '\0' ; s++) {
    /* ignore */
    switch(*s) {
    case '\n':
      continue;
    }

    integer_part *= input_base;
    integer_part += input_symbols_value(*s);

    /* check overflow */
    if(integer_part < overflow)
      errx(1, "Overflow");
    overflow = integer_part;
  }

  for(i = 0 ; integer_part ; i++) {
    output[i]     = output_symbols[integer_part % output_base];
    integer_part /= output_base;
  }
  output[i] = '\0';

  if(minus_mode)
    putchar(minus);
  for(; i >= 0 ; i--)
    putchar(output[i]);
  if(output[0] == '\0')
    putchar(output_symbols[0]);
  putchar('\n');
}

int main(int argc, char *argv[])
{
  int exit_status = EXIT_FAILURE;

  /* This structure is mainly used for the help(...) function and
     map options to their respective help message. */
  struct opts_name names[] = {
    { 'i', "input-base",    "Base of the input numbers" },
    { 'o', "output-base",   "Base of the output numbers" },
    { 'I', "input-sym",     "Symbols of the input numbers" },
    { 'O', "output-sym",    "Symbols of the output numbers" },
    { 's', "separator",     "Floating point number separator" },
    { 'e', "exponent",      "Floating point number exponent symbol" },
    { 'm', "minus",         "Minus symbol" },
    { 'b', "big-endian",    "Big endian notation" },
    { 'l', "little-endian", "Little endian notation" },
    { 'h', "help",          "Show this help message" },
    { 0, NULL, NULL }
  };

  /* getopt long options declaration */
  struct option opts[] = {
    { "input-base",    required_argument, NULL, 'i' },
    { "output-base",   required_argument, NULL, 'o' },
    { "input-sym",     required_argument, NULL, 'I' },
    { "output-sym",    required_argument, NULL, 'O' },
    { "separator",     required_argument, NULL, 's' },
    { "exponent",      required_argument, NULL, 'e' },
    { "minus",         required_argument, NULL, 'm' },
    { "big-endian",    no_argument, NULL, 'b' },
    { "little-endian", no_argument, NULL, 'l' },
    { "help",          no_argument, NULL, 'h' },
    { NULL, 0, NULL, 0 }
  };

  /* parse program name */
  prog_name = (const char *)strrchr(argv[0], '/');
  prog_name = prog_name ? (prog_name + 1) : argv[0];

  while(1) {
    int c = getopt_long(argc, argv, "i:o:I:O:s:e:m:blh", opts, NULL);

    if(c == -1)
      break;

    switch(c) {
      int base;

    case('i'):
      base = check_base(optarg, true);

      input_symbols  = xmalloc(base + 1);
      strncpy(input_symbols, DEFAULT_SYMBOLS, base);
      break;
    case('o'):
      base = check_base(optarg, false);

      output_symbols = xmalloc(base + 1);
      strncpy(output_symbols, DEFAULT_SYMBOLS, base);
      break;
    case('I'):
      input_symbols  = optarg;
      break;
    case('O'):
      output_symbols = optarg;
      break;
    case('s'):
      separator = check_character(optarg, "separator");
      break;
    case('e'):
      exponent  = check_character(optarg, "exponent");
      break;
    case('m'):
      minus     = check_character(optarg, "minus");
      break;
    case 'b':
      break;
    case 'l':
      leflag = true;
      break;
    case('h'):
      exit_status = EXIT_SUCCESS;
    default:
      help(names);
      exit(exit_status);
    }
  }

  input_base  = strlen(input_symbols);
  output_base = strlen(output_symbols);

  argc -= optind;
  argv += optind;

  if(!argv || *argv == '\0') {
    while(!feof(stdin)) {
      char buf[MAX_LINE];

      if(!fgets(buf, MAX_LINE, stdin))
        err(1, "Cannot read from stdin");

      convert(buf);
    }
  }

  for(; *argv ; argv++)
    convert(*argv);

  return 0;
}
