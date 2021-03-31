#include "cacti.h"
#include<stdio.h>
#include<stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "err.h"

#define MSG_ASK 1
#define MSG_GIVE 2
#define MSG_CALC 3

int k,n;
int *input_matrix;
int *input_time;
role_t *role;
actor_id_t first_actor;

typedef struct actor_memory {
  actor_id_t next;
  actor_id_t father;
  bool first;
  bool last;
  int kol;
} actor_memory_t;

void hello(void **state, size_t nbytes, void *data) {
  (void) (nbytes);
  actor_id_t my_id = actor_id_self();
  (*state) = malloc(sizeof(actor_memory_t));
  actor_memory_t *pam = (actor_memory_t *) *state;
  pam->father = *(actor_id_t *)data;
  pam->first = false;
  pam->last = false;
  message_t mess_ask;
  mess_ask.message_type = MSG_ASK;
  mess_ask.nbytes = sizeof(actor_id_t *);
  actor_id_t *my_id_p = malloc(sizeof(actor_id_t));
  *my_id_p = my_id;
  mess_ask.data = (void *) my_id_p;
  if(send_message(pam->father, mess_ask) != 0) syserr(-2, "");
}

void first_fun(void **state, size_t nbytes, void *data) {
  (void) (nbytes);
  (void) (data);
  actor_id_t my_id = actor_id_self();
  (*state) = malloc(sizeof(actor_memory_t));
  actor_memory_t *pam = (actor_memory_t *) *state;
  pam->first = true;
  pam->kol = 0;
  if(n == 1) {
    pam->last = true;
    message_t ms_calc;
    ms_calc.message_type = MSG_CALC;
    ms_calc.nbytes = sizeof(int);
    ms_calc.data = 0;
    if(send_message(my_id, ms_calc) != 0) syserr(-1, "");
  }
  message_t ms_sp;
  ms_sp.message_type = MSG_SPAWN;
  ms_sp.nbytes = sizeof(role);
  ms_sp.data = (void *) role;
  if(send_message(my_id, ms_sp) != 0) syserr(-3, "");
}

void ask_for_col(void **state, size_t nbytes, void *data) {
  (void) (nbytes);
  actor_memory_t *pam = (actor_memory_t *) *state;
  pam->next = *(actor_id_t *) data;
  free(data);
  message_t ms;
  ms.message_type = MSG_GIVE;
  ms.nbytes = sizeof(int);
  ms.data = malloc(sizeof(int));
  *(int *)ms.data = pam->kol + 1;
  if( 0 != send_message(pam->next, ms)) syserr(-4, "");
}

void give_value(void **state, size_t nbytes, void *data) {
  (void) (nbytes);
  actor_memory_t *pam = (actor_memory_t *) *state;
  pam->kol = *(int *) data;
  free(data);
  if(pam->kol + 1 == n) {
    pam->last = true;
    message_t ms_calc;
    ms_calc.message_type = MSG_CALC;
    ms_calc.nbytes = sizeof(int) * 2;
    int *datatab = malloc(sizeof(int) * 2);
    ms_calc.data = (void *) datatab;
    datatab[0] = 0;
    datatab[1] = 0;
    if( 0 != send_message(first_actor, ms_calc)) syserr(-5, "");
  }
  else {
    message_t ms_sp;
    ms_sp.message_type = MSG_SPAWN;
    ms_sp.nbytes = sizeof(role);
    ms_sp.data = (void *) role;
    if (0 != send_message(actor_id_self(), ms_sp)) syserr(-6,"");
  }
}

void calculate(void **state, size_t nbytes, void *data) {
  (void)(nbytes);
  int *datatab = (int *) data;
  actor_memory_t *pam = (actor_memory_t *) *state;
  bool end = datatab[0] == k - 1;
  datatab[1] = datatab[1] + input_matrix[datatab[0] * n + pam->kol];
  usleep(input_time[datatab[0] * n + pam->kol] * 1000);
  if(pam->last) {
    printf("%d\n", datatab[1]);
    free(data);
  }
  else {
    message_t ms_calc;
    ms_calc.message_type = MSG_CALC;
    ms_calc.nbytes = sizeof(int) * 2;
    ms_calc.data = data;
    if(send_message(pam->next, ms_calc)!= 0) syserr(-8, "");
  }
  if(pam->first && !end) {
    message_t ms_calc;
    ms_calc.message_type = MSG_CALC;
    ms_calc.nbytes = sizeof(int) * 2;
    int *new_datatab = malloc(sizeof(int) * 2);
    ms_calc.data = (void *) new_datatab;
    new_datatab[0] = datatab[0] + 1;
    new_datatab[1] = 0;
    if(send_message(actor_id_self(), ms_calc) != 0) syserr(-7,"");
  }
  if(end) {
    free(*state);
    message_t mess_god;
    mess_god.message_type = MSG_GODIE;
    mess_god.nbytes = 0;
    mess_god.data = 0;
    if(0 != send_message(actor_id_self(), mess_god)) syserr(-9,"");
  }
}

int main() {
  int err;
  scanf("%d%d", &k, &n);
  input_matrix = malloc(n * k * sizeof(int));
  input_time = malloc(n * k * sizeof(float));
  for (int i = 0; i < k * n; i++) {
    scanf("%d%d", &input_matrix[i], &input_time[i]);
  }
  role_t *first_role;
  role = malloc(sizeof *role);
  role->nprompts = 4;
  role->prompts = malloc(sizeof(act_t) * 4);
  *(void **)&role->prompts[0] = (void*) &hello;
  *(void **)&role->prompts[1] = (void*) &ask_for_col;
  *(void **)&role->prompts[2] = (void*) &give_value;
  *(void **)&role->prompts[3] = (void*) &calculate;
  first_role = malloc(sizeof *role);
  first_role->nprompts = 4;
  first_role->prompts = malloc(sizeof(act_t) * 4);
  *(void **)&first_role->prompts[0] = (void*) &first_fun;
  *(void **)&first_role->prompts[1] = (void*) &ask_for_col;
  *(void **)&first_role->prompts[2] = (void*) &give_value;
  *(void **)&first_role->prompts[3] = (void*) &calculate;
  if((err = actor_system_create(&first_actor, first_role)) != 0) syserr(err, "create");
  actor_system_join(first_actor);
  free(input_matrix);
  free(input_time);
  free((void*) first_role->prompts);
  free((void*) role->prompts);
  free(first_role);
  free(role);
	return 0;
}
