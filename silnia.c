#include "cacti.h"
#include<stdio.h>
#include<stdlib.h>
#include "err.h"

int n;
role_t *role;

void give(void **stateptr, size_t nbytes, void *data) {
  (void)(nbytes);
  actor_id_t where = *((actor_id_t*) data);
  message_t mess;
  free(data);
  mess.message_type = 2;
  mess.nbytes = sizeof(long) * 2;
  mess.data = *stateptr;
  send_message(where, mess);
}

void hello(void **stateptr, size_t nbytes, void *data) {
  (void) (stateptr);
  (void) (nbytes);
  actor_id_t father = *((actor_id_t*) data);
  actor_id_t my_id = actor_id_self();
  message_t mess_giv;
  mess_giv.message_type = 1;
  mess_giv.nbytes = sizeof(actor_id_t *);
  actor_id_t *my_id_p = malloc(sizeof(actor_id_t));
  *my_id_p = my_id;
  mess_giv.data = (void *) my_id_p;
  send_message(father, mess_giv);
  message_t mess_god;
  mess_god.message_type = MSG_GODIE;
  mess_god.nbytes = 0;
  mess_god.data = 0;
  send_message(father, mess_god);
}

void calculate(void **stateptr, size_t nbytes, void *data) {
  actor_id_t my_id = actor_id_self();
  if (nbytes != sizeof(long) * 2) return;
  long *tab = (long *) data;
  long k = tab[0];
  long ksol = tab[1];
  free(data);
  long sol = (k + 1) * ksol;
  if (k + 1 >= n) {
    if(k >= n) printf("%ld\n", ksol);
    else printf("%ld\n", sol);
    message_t mess_god;
    mess_god.message_type = MSG_GODIE;
    mess_god.nbytes = 0;
    mess_god.data = 0;
    send_message(my_id, mess_god);
  }
  else {
    long *calc_data = malloc(sizeof(long) * 2);
    calc_data[0] = k + 1;
    calc_data[1] = sol;
    *stateptr = calc_data;
    message_t next;
    next.message_type = MSG_SPAWN;
    next.nbytes = sizeof(role);
    next.data = (void *) role;
    send_message(actor_id_self(), next);
  }
}

void first_fun(void **stateptr, size_t nbytes, void *data) {
  (void)(stateptr);
  (void)(nbytes);
  (void)(data);
  message_t first_mess;
  first_mess.message_type = 2;
  first_mess.nbytes = sizeof(long) * 2;
  long *datatab = malloc(sizeof(long) * 2);
  datatab[0] = 1;
  datatab[1] = 1;
  first_mess.data = (void*) datatab;
  send_message(actor_id_self(), first_mess);
}

int main(){
  scanf("%d", &n);
  int err;
  actor_id_t first_actor;
  role = malloc(sizeof *role);
  role->nprompts = 3;
  role->prompts = malloc(sizeof(act_t) * 3);
  *(void **)&role->prompts[0] = (void*) &hello;
  *(void **)&role->prompts[1] = (void*) &give;
  *(void **)&role->prompts[2] = (void*) &calculate;
  role_t *first_role;
  first_role = malloc(sizeof *role);
  first_role->nprompts = 3;
  first_role->prompts = malloc(sizeof(act_t) * 3);
  *(void **)&first_role->prompts[0] = (void*) &first_fun;
  *(void **)&first_role->prompts[1] = (void*) &give;
  *(void **)&first_role->prompts[2] = (void*) &calculate;
  if((err = actor_system_create(&first_actor, first_role)) != 0) syserr(err, "create");
  actor_system_join(first_actor);
  free((void*)role->prompts);
  free((void*)first_role->prompts);
  free(role);
  free(first_role);
	return 0;
}
