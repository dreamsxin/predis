#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <limits.h>
#include <assert.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include "../lib/random_string.h"
#define HT_VAL_TYPE char
#include "../lib/hashtable.h"

struct item {
  char *key;
  unsigned int *key_hash;
  char *value;
  volatile bool set;
  volatile bool set_complete;
  volatile bool get;
  volatile int ctr;
};

struct record {
  bool set;
  bool get;
  int retry_count;
  bool fail;
  struct item *item;
};

struct tdata {
  volatile bool *start;
  volatile bool *thread_ready;
  int qlen;
  pthread_mutex_t *tlock;
  struct ht_table *hashtable;
  volatile int *rec_ctr;
  struct record *records;
  struct item **items;
};

volatile int good_set = 0;
volatile int good_get = 0;
volatile int bad_set = 0;
volatile int bad_get = 0;

void *tfunc(void *_shared) {
  struct tdata *shared = _shared;
  int qlen = shared->qlen;
  struct item **queue = shared->items;
  bool tmp1;
  bool tmp2;
  char *rep;
  char *prep;
  int rec_ctr;
  int rep1;
  int change_ctr;
  int retry_count;
  int *zero = malloc(sizeof(int));
  int fail_cnt;
  *(shared->thread_ready) = true;
  while (!__atomic_load_n(shared->start, __ATOMIC_SEQ_CST)) {}
  for (int i = 0; i < qlen; i++) {
    tmp1 = false;
    retry_count = 0;
    tmp2 = false;
    fail_cnt = 0;
    if (__atomic_compare_exchange_n(&(queue[i]->set), &tmp1, true, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      // Set was false (is now true)
      // *zero = 0;
      // printf("%p %p\n", shared->tlock, zero);
      // while (!__atomic_compare_exchange_n(shared->tlock, zero, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      //   sched_yield();
      //   fail_cnt++;
      //   *zero = 0;
      // }
      // pthread_mutex_lock(shared->tlock);
      rep1 = ht_store(shared->hashtable, queue[i]->key, queue[i]->value);
      // pthread_mutex_unlock(shared->tlock);
      // __atomic_store_n(&(shared->tlock), 0, __ATOMIC_SEQ_CST);
      // rep = redisCommand(ctx, "set %s %s", queue[i]->key, queue[i]->value);
      rec_ctr = __atomic_fetch_add(shared->rec_ctr, 1, __ATOMIC_SEQ_CST);
      shared->records[rec_ctr].set = true;
      shared->records[rec_ctr].item = queue[i];
      __atomic_store_n(&(queue[i]->set_complete), true, __ATOMIC_SEQ_CST);
      if (rep1 != 0) {
        __atomic_add_fetch(&bad_set, 1, __ATOMIC_SEQ_CST);
      } else {
        __atomic_add_fetch(&good_set, 1, __ATOMIC_SEQ_CST);
      }
    } else if (__atomic_compare_exchange_n(&(queue[i]->get), &tmp2, true, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      // Get was false (is now true)
      while (__atomic_load_n(&(queue[i]->set_complete), __ATOMIC_SEQ_CST) == false) {sched_yield();}
      // rep = redisCommand(ctx, "get %s", queue[i]->key);
      // *zero = 0;
      // while (!__atomic_compare_exchange_n(shared->tlock, zero, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      //   sched_yield();
      //   *zero = 0;
      // }
      // pthread_mutex_lock(shared->tlock);
      rep = ht_find(shared->hashtable, queue[i]->key);
      // pthread_mutex_unlock(shared->tlock);
      // __atomic_store_n(&(shared->tlock), 0, __ATOMIC_SEQ_CST);
      rec_ctr = __atomic_fetch_add(shared->rec_ctr, 1, __ATOMIC_SEQ_CST);
      shared->records[rec_ctr].get = true;
      shared->records[rec_ctr].item = queue[i];
      /* if (rep == NULL) {
        printf("AAAAA NULL REP\n");
        __atomic_add_fetch(&bad_get, 1, __ATOMIC_SEQ_CST);
      } else */ if (rep == NULL || strcmp(rep, queue[i]->value) != 0) {
        prep = NULL;
        change_ctr = 0;
        while ((rep == NULL || strcmp(rep, queue[i]->value) != 0) && retry_count < 500) {
          if (prep != NULL && strcmp(rep, prep) != 0)
            change_ctr++;
          sched_yield();
          prep = rep;
          rep = ht_find(shared->hashtable, queue[i]->key);
          // rep = redisCommand(ctx, "get %s", queue[i]->key);
          retry_count++;
        }
        shared->records[rec_ctr].retry_count = retry_count;
        shared->records[rec_ctr].fail = rep == NULL || strcmp(rep, queue[i]->value) != 0;
        printf("%ld | Retry: (%d %s | changes: %d) (%02lu) %-45s %s\n", syscall(SYS_gettid), retry_count, shared->records[rec_ctr].fail ? "fail" : "success", change_ctr, strlen(queue[i]->key) - 5, queue[i]->key, rep);
        if (!shared->records[rec_ctr].fail) {
          __atomic_add_fetch(&good_get, 1, __ATOMIC_SEQ_CST);
        } else {
          __atomic_add_fetch(&bad_get, 1, __ATOMIC_SEQ_CST);
        }
      } else {
        __atomic_add_fetch(&good_get, 1, __ATOMIC_SEQ_CST);
      }
    } else {
      printf("wrong\n");
    }
  }
  fflush(stdout);
  return NULL;
}

// https://stackoverflow.com/a/7666577/4948732
static const unsigned int hash(const unsigned int *str) {
  // unsigned int hsh = 0x0;
  // while (*str != 0x0) {
  //   hsh = (hsh << 2) ^ *str;
  //   str++;
  // }
  // return hsh;
  // return *str;
  unsigned int hash = 5381;
  unsigned int c;
  while ((c = *str++) != 0x0) {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }
  return hash;
}

int main(int argc, char *argv[]) {
  int item_count = 100000;
  int thread_count = get_nprocs();
  bool runchecks = false;
  bool quiet = false;

  const struct option prog_opts[] = {
    {"check", optional_argument, NULL, 'r'},
    {"threads", optional_argument, NULL, 't'},
    {"items", optional_argument, NULL, 'c'},
    {"quiet", no_argument, NULL, 'q'}
  };
  int c;
  while (1) {
    c = getopt_long(argc, argv, "x:t:c:qr", prog_opts, NULL);
    if (c == -1) { break; }
    switch (c) {
      case 'r':
        runchecks = true;
        break;
      case 't':
        if (optarg != NULL) {
          thread_count = strtol(optarg, NULL, 10);
        } else {
          printf("-t requires an argument\n");
        }
        break;
      case 'c':
        if (optarg != NULL) {
          item_count = strtol(optarg, NULL, 10);
        } else {
          printf("-c requires an argument\n");
        }
        break;
      case 'q':
        quiet = true;
        break;
    }
  }

  if (!quiet)
    printf("Item Count: %d\nThread Count: %d\nRunning Checks? %s\n", item_count, thread_count, runchecks ? "true" : "false");

  int items_per_queue = (item_count / thread_count) * 2;

  int *rec_ctr = malloc(sizeof(int));
  *rec_ctr = 0;
  struct record *records = malloc(sizeof(struct record) * (item_count * 2));
  for (int i = 0; i < item_count; i++) {
    records[i].set = false;
    records[i].get = false;
    records[i].retry_count = 0;
    records[i].fail = false;
    records[i].item = NULL;
  }

  struct item *items = malloc(sizeof(struct item)*item_count);
  struct item *item;
  unsigned int *key_hash;
  char *key;
  bool duplicate;
  int key_hash_ht_len = 1000000;
  unsigned int *key_hash_ht = malloc(sizeof(unsigned int)*key_hash_ht_len);
  for (int i = 0; i < key_hash_ht_len; i++) {
    key_hash_ht[i] = 0x0;
  }
  int key_hash_length;
  int block_len = sizeof(unsigned int)*CHAR_BIT;
  if (!quiet)
    printf("HT SIZE: %d\n", block_len * key_hash_ht_len);
  unsigned int hsh;
  unsigned int pval;
  int dupctr;
  float dupavj = 0.0;
  for (int i = 0; i < item_count; i++) {
    dupctr = -1;
    do {
      dupctr++;
      key = random_string('k', &key_hash, &key_hash_length);
      duplicate = false;
      hsh = hash(key_hash) % (key_hash_ht_len * block_len);
      if ((key_hash_ht[hsh / block_len] >> (hsh % block_len)) & 0x1 == 0x1) {
        // printf("%d taken\n", dupctr);
        // for (int j = 0; j < i; j++) {
        //   if (memcmp(items[j].key_hash, key_hash, key_hash_length) == 0) {
        //     duplicate = true;
        //     free(key_hash);
        //     break;
        //   }
        // }
        duplicate = true;
      } else {
        // printf("say this alot plx\n");
      }
      if (dupctr > 1000 && !quiet)
        printf("\r1000 is VERY not normal\n");
      // if (duplicate)
      //   printf("DUPE\n");
    } while (duplicate);
    dupavj = ((dupavj * i) + dupctr) / (i + 1);
    items[i].key = key;
    items[i].key_hash = key_hash;
    pval = key_hash_ht[hsh / block_len];
    // assert((0x0 ^ (0x1 << 0/* (hsh % block_len) */)) % 2 == 0);
    key_hash_ht[hsh / block_len] = key_hash_ht[hsh / block_len] | (0x1 << (hsh % block_len));
    assert((pval ^ key_hash_ht[hsh / block_len]) % 2 == 0 || (pval ^ key_hash_ht[hsh / block_len]) == 0x1);
    assert((key_hash_ht[hsh / block_len] >> (hsh % block_len)) & 0x1 == 0x1);
    free(key_hash);
    items[i].value = random_string('v', NULL, NULL);
    items[i].set = false;
    items[i].set_complete = false;
    items[i].get = false;
    items[i].ctr = 0;
    if (i != 0 && i % 20000 == 0 && !quiet) {
      printf("\r%d %f", i, dupavj);
      fflush(stdout);
    }
  }
  printf("\n");
  free(key_hash_ht);
  if (!quiet)
    printf("Generation complete\n");
  struct item **queues[thread_count];
  for (int i = 0; i < thread_count; i++) {
    queues[i] = malloc(sizeof(struct item*) * (items_per_queue + 2));
  }
  int count = 0;
  int idx;
  int qnum = 0;
  int qidx[thread_count];
  for (int i = 0; i < thread_count; i++) {
    qidx[i] = 0;
  }
  while (count < item_count*2) {
    idx = rand() % item_count;
    if (items[idx].ctr < 2) {
      qnum = (qnum + 1) % thread_count;
      queues[qnum][qidx[qnum]] = &items[idx];
      // memcpy(&(queues[qnum][qidx[qnum]]), &(items[idx]), sizeof(struct item));
      qidx[qnum]++;
      items[idx].ctr++;
      count++;
    }
  }
  struct tdata *shared;
  pthread_t thread_ids[thread_count];
  bool *thread_ready = malloc(sizeof(bool)*thread_count);
  bool *start = malloc(sizeof(bool));
  struct ht_table *ht = ht_init(8);
  *start = false;
  // int *tlock = malloc(sizeof(int));
  // printf("Tlock %p\n", tlock);
  // *tlock = 0;
  pthread_mutex_t *tlock = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(tlock, NULL);
  for (int i = 0; i < thread_count; i++) {
    shared = malloc(sizeof(struct tdata));
    shared->start = start;
    shared->qlen = qidx[i];
    shared->items = queues[i];
    shared->tlock = tlock;
    shared->hashtable = ht;
    shared->thread_ready = thread_ready + i;
    shared->records = records;
    shared->rec_ctr = rec_ctr;
    pthread_create(&(thread_ids[i]), NULL, tfunc, shared);
  }
  bool all;
  while (true) {
    all = true;
    for (int i = 0; i < thread_count; i++) {
      all = all || thread_ready[i];
    }
    if (all)
      break;
  }
  struct timespec t1, t2;
  clock_gettime(CLOCK_REALTIME, &t1);
  __atomic_store_n(start, true, __ATOMIC_SEQ_CST);
  for (int i = 0; i < thread_count; i++) {
    pthread_join(thread_ids[i], NULL);
  }
  clock_gettime(CLOCK_REALTIME, &t2);
  if (!quiet)
    printf("Run time: %.4fs\n", (t2.tv_sec - t1.tv_sec) + ((t2.tv_nsec - t1.tv_nsec) / 1000000000.0));
  if (runchecks && !quiet) {
    printf("Checking records...\n");
    int set_ary_ctr = 0;
    bool any;
    int err = 0;
    int fail_ctr = 0;
    float avj_retries = 0;
    char **set_ary = malloc(sizeof(char*)*item_count);
    for (int i = 0; i < item_count * 2; i++) {
      if (records[i].fail) {
        fail_ctr++;
      } else if (records[i].retry_count > 0) {
        avj_retries = ((avj_retries * (i - fail_ctr)) + records[i].retry_count) / ((i - fail_ctr) + 1);
      }
      if (records[i].set) {
        set_ary[set_ary_ctr] = records[i].item->key;
        set_ary_ctr++;
      } else if (records[i].get) {
        any = false;
        for (int j = 0; j < set_ary_ctr; j++) {
          if (strcmp(set_ary[j], records[i].item->key) == 0) {
            any = true;
            break;
          }
        }
        if (!any) {
          err++;
        }
      }
    }
    printf("Record check complete (%d errors)\n", err);
    printf("%d total failures\n%f retries before sucess\n", fail_ctr, avj_retries);
  }
  if (!quiet)
    printf("Set:\n  Good:  %d\n  Bad:   %d\nGet:\n  Good:  %d\n  Bad:   %d\n\n  Total: %d\n", good_set, bad_set, good_get, bad_get, item_count);
  return 0;
}
