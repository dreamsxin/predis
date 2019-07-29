#ifndef DATA_TYPE_H
#define DATA_TYPE_H

struct return_val {
  char* value;
};

struct data_type {
  char *name;
  int (*setter)(void**, char*);
  int (*updater)(void**, char*);
  int (*getter)(void*, struct return_val*);
};

#endif // DATA_TYPE_H
