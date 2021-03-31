#include "cacti.h"
#include "err.h"
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define CASTSIZE 1024

typedef struct actor {
    const actor_id_t id;
    actor_id_t *id_public; //secure to share pointer id
    role_t *const role;
    message_t *messages; //message queue
    bool alive;
    bool in_queue;
    bool operated;
    long nmess; //num of messages in queue
    long fmess; //first queue message
    pthread_mutex_t *mutex;
    void** state;
} actor_t;


long cast = 0;
long siz;
long first = 0;
actor_t *actors; //actors structure
long dead; //number of dead actors
bool del; //is system in delete mode
bool sig; //is SIGINT catched
pthread_attr_t attr;
pthread_t th[POOL_SIZE];
pid_t tid[POOL_SIZE];
actor_id_t aid[POOL_SIZE];
pthread_mutex_t lock; //secure all global variables
pthread_mutex_t queue_mutex; //secure queue of actors to operate
sem_t queue_sem; // secure actors operations
int first_in_queue;
int in_queue;
actor_id_t *actors_queue;


actor_id_t actor_id_self() {
  pid_t p = pthread_self();
  for(int i = 0; i < POOL_SIZE; i++) {
    if (tid[i] == p)
      return aid[i];
  }
  return -1;
}

void actor_system_end() {
  int err;
  void *retval;
  for(int i = 1; i < POOL_SIZE; i++) {
    if ((err = pthread_join(th[i], &retval)) != 0) syserr(err,"join in end");
  }
  for(int i = 0; i < cast; i++) {
    free(actors[i].messages);
    free(actors[i].state);
    free(actors[i].id_public);
    if((err = pthread_mutex_lock(actors[i].mutex)) != 0) syserr(err, "mutex lock\n");
    if((err = pthread_mutex_unlock(actors[i].mutex)) != 0) syserr(err, "mutex unlock\n");
    if ((err = pthread_mutex_destroy(actors[i].mutex)) != 0) syserr(err, "mutex actor destroy");
    free(actors[i].mutex);
  }
  free(actors);
  free(actors_queue);
  if ((err = pthread_mutex_destroy(&queue_mutex)) != 0) syserr(err, "destroy in end");
  if ((err = pthread_mutex_destroy(&lock)) != 0) syserr(err, "destroy in end");
  if((err = sem_destroy(&queue_sem)) != 0) syserr(err,"sem destroy");
  if ((err = pthread_attr_destroy(&attr)) != 0) syserr(err, "attr destroy");
  cast = 0;
}

void *worker(void *data) {
  long id;
  id = *((long*) data);
  free(data);
  int err;
  tid[id] = pthread_self();
  while(true) {
    if((err = sem_wait(&queue_sem)) != 0) syserr(err, "sem wait in worker");
    if ((err = pthread_mutex_lock(&queue_mutex)) != 0) syserr(err,"lock in worker");
    if(del) {
      if((err = sem_post(&queue_sem)) != 0) syserr(err, "post in end");
      if ((err = pthread_mutex_unlock(&queue_mutex)) != 0) syserr(err,"lock in worker");
      if(id == 0)
        actor_system_end();
      return 0;
    }
    in_queue--;
    actor_id_t actor = actors_queue[first_in_queue];
    first_in_queue = (first_in_queue + 1) % CAST_LIMIT;
    aid[id] = actor;
    actor -= first;
    if ((err = pthread_mutex_unlock(&queue_mutex)) != 0) syserr(err, "unlock in worker");
    if ((err = pthread_mutex_lock(actors[actor].mutex)) != 0) syserr(err,"lock in worker");
    long nmess = actors[actor].nmess;
    long fmess = actors[actor].fmess;
    actors[actor].in_queue = false;
    actors[actor].operated = true;
    if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err, "unlock in worker");
    for(int i = 0; i < nmess; i++) {
      message_t mess = actors[actor].messages[(fmess + i) % ACTOR_QUEUE_LIMIT];
      if(mess.message_type == MSG_GODIE) {
        actors[actor].alive = false;
      }
      if(mess.message_type == MSG_SPAWN) {
        if ((err = pthread_mutex_lock(&lock)) != 0) syserr(err, "lock lock");
        if (cast == CAST_LIMIT || sig) {
          if ((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock lock");
        }
        else {
          if(cast == siz) {
            siz *= 2;
            actor_t *actors_aux = malloc(siz * sizeof *actors);
            memcpy(actors_aux, actors, cast*sizeof *actors);
            free(actors);
            actors = actors_aux;
          }
          long ac = cast;
          *(actor_id_t *)&actors[ac].id = cast + first;
          cast += 1;
          actors[ac].id_public = malloc(sizeof(actor_id_t));
          *actors[ac].id_public = actors[ac].id;
          *(role_t **)&actors[ac].role = (role_t *) mess.data;
          actors[ac].messages = (message_t *) malloc(ACTOR_QUEUE_LIMIT * sizeof(message_t));
          actors[ac].alive = true;
          actors[ac].nmess = 0;
          actors[ac].fmess = 0;
          actors[ac].operated = false;
          actors[ac].in_queue = false;
          actors[ac].state = malloc(sizeof(void**));
          actors[ac].mutex = malloc(sizeof(pthread_mutex_t));
          if ((err = pthread_mutex_init(actors[ac].mutex, 0)) != 0) syserr(err,"init mutex in spawn");
          *actors[ac].state = 0;
          if ((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock lock");
          message_t hell;
          hell.message_type = MSG_HELLO;
          hell.nbytes = sizeof(actor_id_t *);
          *actors[actor].id_public = actors[actor].id;
          hell.data = actors[actor].id_public;
          send_message(ac + first, hell);
        }
      }
      if(mess.message_type != MSG_GODIE && mess.message_type != MSG_SPAWN) {
        (*(actors[actor].role->prompts[mess.message_type]))(actors[actor].state, mess.nbytes, mess.data);
      }
      if ((err = pthread_mutex_lock(actors[actor].mutex)) != 0) syserr(err,"lock in worker");
      actors[actor].nmess--;
      actors[actor].fmess = (actors[actor].fmess + 1) % ACTOR_QUEUE_LIMIT;
      if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err,"unlock in worker");
    }
    if ((err = pthread_mutex_lock(actors[actor].mutex)) != 0) syserr(err,"lock in worker");
    actors[actor].operated = false;
    if(actors[actor].in_queue) {
      if ((err = pthread_mutex_lock(&queue_mutex)) != 0) syserr(err, "lock in send");
      actors_queue[(first_in_queue + in_queue) % CAST_LIMIT] = actor + first;
      in_queue++;
      if ((err = sem_post(&queue_sem)) != 0) syserr(err, "post in send");
      if ((err = pthread_mutex_unlock(&queue_mutex)) != 0) syserr(err, "unlock in send");
    }
    else {
      if(!actors[actor].alive) {
        if((err = pthread_mutex_lock(&lock)) != 0) syserr(err, "lock in worker");
        dead++;
        if(dead >= cast) {
          del = true;
          if((err = sem_post(&queue_sem)) != 0) syserr(err, "post in end");
          if((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock in worker");
        }
        else if((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock in worker");
      }
    }
    if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err, "unlock in worker");
  }
}

void catch (int sign) {
  int err;
  (void)sign;
  if ((err = pthread_mutex_lock(&lock)) != 0) syserr(err, "lock in sig");
  sig = true;
  long ct = cast;
  if ((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock in sig");
  for (int i = 0; i < ct; i++) {
    if ((err = pthread_mutex_lock(actors[i].mutex)) != 0) syserr(err, "lock in sig");
    actors[i].alive = false;
    if(actors[i].nmess == 0) {
      message_t kil;
      kil.message_type = MSG_GODIE;
      kil.nbytes = 0;
      kil.data = NULL;
      actors[i].messages[(actors[i].nmess + actors[i].fmess) % ACTOR_QUEUE_LIMIT] = kil;
      actors[i].nmess++;
      if (!actors[i].in_queue) {
        actors[i].in_queue = true;
        if(!actors[i].operated) {
          if ((err = pthread_mutex_lock(&queue_mutex)) != 0) syserr(err, "lock in send");
          actors_queue[(first_in_queue + in_queue) % CAST_LIMIT] = i + first;
          in_queue++;
          if ((err = sem_post(&queue_sem)) != 0) syserr(err, "post in send");
          if ((err = pthread_mutex_unlock(&queue_mutex)) != 0) syserr(err, "unlock in send");
        }
      }
    }
    if ((err = pthread_mutex_unlock(actors[i].mutex)) != 0) syserr(err, "lock in sig");
  }
}

int actor_system_create(actor_id_t *actor, role_t *const role){
  int err;
  sig = false;
  if ((err = pthread_mutex_init(&lock, 0)) != 0) return -6;
  if ((err = pthread_mutex_lock(&lock)) != 0) return -1;
  if (cast > 0) {
    if ((err = pthread_mutex_unlock(&lock)) != 0) return -11;
    return -100;
  }
  struct sigaction action;
  sigset_t block_mask;
  sigemptyset (&block_mask);
  sigaddset(&block_mask, SIGINT);
  action.sa_handler = catch;
  action.sa_mask = block_mask;
  action.sa_flags = 0;
  if (sigaction (SIGINT, &action, 0) == -1) return -15;
  cast = 1;
  siz = CASTSIZE;
  dead = 0;
  del = false;
  if(CAST_LIMIT < siz)
    siz = CAST_LIMIT;
  if ((err = pthread_mutex_unlock(&lock)) != 0) return -11;
  if((err = sem_init(&queue_sem, 0, 0)) != 0) return -1;
  if ((err = pthread_mutex_init(&queue_mutex, 0)) != 0) return -12;
  actors = malloc(siz * sizeof (*actors));
  actors[0].messages = (message_t*) malloc(ACTOR_QUEUE_LIMIT * sizeof(message_t));
  *(actor_id_t *)&actors[0].id = first;
  actors[0].id_public = malloc(sizeof(actor_id_t));
  *actors[0].id_public = actors[0].id;
  *(role_t **)&actors[0].role = role;
  actors[0].alive = true;
  actors[0].nmess = 0;
  actors[0].fmess = 0;
  actors[0].operated = false;
  actors[0].in_queue = false;
  actors[0].state = malloc(sizeof(void**));
  *actors[0].state = 0;
  actors[0].mutex = malloc(sizeof(pthread_mutex_t));
  if ((err = pthread_mutex_init(actors[0].mutex, 0)) != 0) return -10;
  *actor = first;
  long *worker_arg;
  if((err = pthread_attr_init(&attr)) != 0) return -2;
  if((err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE)) != 0)
    return -3;
  actors_queue = malloc(CAST_LIMIT * sizeof *actors_queue);
  first_in_queue = 0;
  in_queue = 0;
  for (int i = 0; i < POOL_SIZE; i++) {
    worker_arg = malloc(sizeof(long));
    *worker_arg = i;
    tid[i] = -1;
    if((err = pthread_create(&th[i], &attr, worker, worker_arg)) != 0)
        return -5;
  }
  message_t mes;
  mes.message_type = MSG_HELLO;
  mes.nbytes = 0;
  mes.data = NULL;
  send_message(first, mes);
  return 0;
}


void actor_system_join(actor_id_t actor) {
  int err;
  (void)(actor);
  void *retval;
  if ((err = pthread_join(th[0], &retval)) != 0) syserr(err,"join in join");
}


int send_message(actor_id_t actor_id, message_t message) {
  int err;
  if ((err = pthread_mutex_lock(&lock)) != 0) syserr(err, "lock lock in send");
  actor_id_t actor = actor_id - first;
  if (actor < 0 || cast <= actor) {
    if ((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock in send");
    return -2;
  }
  if ((err = pthread_mutex_unlock(&lock)) != 0) syserr(err, "unlock in send");
  if ((err = pthread_mutex_lock(actors[actor].mutex)) != 0) syserr(err, "actor lock in send");
  if(actors[actor].nmess > 0 && !actors[actor].in_queue && !actors[actor].operated) syserr(actor, "WTF");
  if (!actors[actor].alive) {
    if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err, "unlock in send");
    return -1;
  }
  if(actors[actor].nmess == ACTOR_QUEUE_LIMIT) {
    if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err, "unlock in send");
    return -3;
  }
  actors[actor].messages[(actors[actor].nmess + actors[actor].fmess) % ACTOR_QUEUE_LIMIT] = message;
  actors[actor].nmess++;
  if (!actors[actor].in_queue) {
    actors[actor].in_queue = true;
    if(!actors[actor].operated) {
      if ((err = pthread_mutex_lock(&queue_mutex)) != 0) syserr(err, "lock in send");
      actors_queue[(first_in_queue + in_queue) % CAST_LIMIT] = actor_id;
      in_queue++;
      if ((err = sem_post(&queue_sem)) != 0) syserr(err, "post in send");
      if ((err = pthread_mutex_unlock(&queue_mutex)) != 0) syserr(err, "unlock in send");
    }
  }
  if ((err = pthread_mutex_unlock(actors[actor].mutex)) != 0) syserr(err, "unlock in send");
  return 0;
}
