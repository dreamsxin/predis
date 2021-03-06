#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "command_parser.h"
#include "tools/cmds.c"
#include "tools/command_parser_hashes.h"

static const char unrecognized_command[] = "Unrecognized command: %s";
static const char command_done[] = "DONE: %d";
static const char error_head[] = "ERROR";
static const char done_head[] = "DONE";

static char *print_result(int res, struct return_val *rval, bool force_error_codes) {
  int output_len = (res < 0 || rval == NULL) ? snprintf(NULL, 0, "%d", res) : strlen(rval->value);
  size_t size = (res < 0 ? sizeof(error_head) : sizeof(done_head)) + sizeof(char) * (2 /* colon space */ + output_len + 1 /* newline */) + 1 /* nul */;
  char *ret_buf = malloc(size);
  if ((force_error_codes && res < 0) || rval == NULL) {
    snprintf(ret_buf, size, "%s: %d", res < 0 ? error_head : done_head, res);
  } else {
    snprintf(ret_buf, size, "%s: %s", res < 0 ? error_head : done_head, rval->value);
  }
  return ret_buf;
}

char *parse_command(struct main_struct *ms, struct return_val *rval, char **args, int arglen) {
  int errors;
  int ret_buf_size;
  char* ret_buf;
  int output_len;
  char *cmd = args[0];
  int cmd_len = strlen(cmd);
  if (in_word_set(cmd, cmd_len)) {
    unsigned int cmd_hsh = hash(cmd, cmd_len);
    switch (cmd_hsh) {
      case SET_HSH: {
        // printf("set %s %s\n", args[1], args[2]);
        errors = set("string", ms, args[1], args + 2);
        // errors = ht_store(ms->hashtable, args[1], idx);
        if (errors != 0) {
          ret_buf_size = snprintf(NULL, 0, "HT_ERROR: %d", errors);
          ret_buf = malloc(sizeof(char)*(ret_buf_size + 1));
          snprintf(ret_buf, ret_buf_size + 1, "HT_ERROR: %d", errors);
          // del(ms, args[2]);
        } else {
          ret_buf = strdup("OK");
          // ret_buf = print_result(idx, NULL, false);
        }
        break;
      }
      case LINIT_HSH: {
        errors = set("linked_list", ms, args[1], args + 2);
        if (errors != 0) {
          ret_buf_size = snprintf(NULL, 0, "HT_ERROR: %d", errors);
          ret_buf = malloc(sizeof(char)*(ret_buf_size + 1));
          snprintf(ret_buf, ret_buf_size + 1, "HT_ERROR: %d", errors);
          // del(ms, args[2]);
        } else {
          ret_buf = strdup("OK");
          // ret_buf = print_result(idx, NULL, false);
        }
        break;
      }
      case LPUSH_HSH: {
        errors = update("linked_list", "push", ms, args[1], args + 2);
        if (errors != 0) {
          ret_buf_size = snprintf(NULL, 0, "HT_ERROR: %d", errors);
          ret_buf = malloc(sizeof(char)*(ret_buf_size + 1));
          snprintf(ret_buf, ret_buf_size + 1, "HT_ERROR: %d", errors);
          // del(ms, args[2]);
        } else {
          ret_buf = strdup("OK");
          // ret_buf = print_result(idx, NULL, false);
        }
        break;
      }
      case LINDEX_HSH: {
        errors = get("linked_list", ms, "fetch", rval, args[1], args + 2);
        if (errors >= 0) {
          ret_buf = malloc(sizeof(char)*strlen(rval->value) + 1);
          strcpy(ret_buf, rval->value);
        } else {
          // printf("Get error %d\n", errors);
          ret_buf = print_result(errors, rval, true);
        }
        if (errors == 1) {
          free(rval->value);
        }
        break;
      }
      case LPOP_HSH: {
        errors = get("linked_list", ms, "pop", rval, args[1], args + 2);
        if (errors >= 0) {
          ret_buf = malloc(sizeof(char)*strlen(rval->value) + 1);
          strcpy(ret_buf, rval->value);
        } else {
          // printf("Get error %d\n", errors);
          ret_buf = print_result(errors, rval, true);
        }
        if (errors == 1) {
          free(rval->value);
        }
        break;
      }
      case GET_HSH: {
        // printf("get %s\n", args[1]);
        errors = get("string", ms, "fetch", rval, args[1], args + 2);
        if (errors >= 0) {
          ret_buf = malloc(sizeof(char)*strlen(rval->value) + 1);
          strcpy(ret_buf, rval->value);
        } else {
          // printf("Get error %d\n", errors);
          ret_buf = print_result(errors, rval, true);
        }
        if (errors == 1) {
          free(rval->value);
        }
        break;
      }
      case UPDATE_HSH: {
        // update <type> <updater> <index> <new value>
        // idx = strtol(args[3], NULL, 10);
        errors = update(args[1], args[2], ms, args[3], args + 4);
        ret_buf = print_result(errors, NULL, false);
        break;
      }
      case DELETE_HSH: {
        // delete <key>
        // idx = strtol(args[1], NULL, 10);
        errors = del(ms, args[1]);
        ret_buf = print_result(errors, NULL, false);
        break;
      }
      case CLEAN_HSH: {
        errors = clean_queue(ms);
        output_len = snprintf(NULL, 0, "%d", errors);
        ret_buf = malloc(sizeof(command_done)+sizeof(char)*output_len+1);
        snprintf(ret_buf, sizeof(command_done)+sizeof(char)*output_len+1, command_done, errors);
        break;
      }
      default: {
        printf("gperf: Some kind of error occured\n");
        ret_buf = malloc(sizeof(unrecognized_command)+sizeof(char)*cmd_len+1);
        snprintf(ret_buf, sizeof(unrecognized_command)+sizeof(char)*cmd_len + 1, unrecognized_command, cmd);
      }
    }
  } else {
    ret_buf = malloc(sizeof(unrecognized_command)+sizeof(char)*cmd_len+1);
    snprintf(ret_buf, sizeof(unrecognized_command)+sizeof(char)*cmd_len + 1, unrecognized_command, cmd);
  }
  return ret_buf;
}
