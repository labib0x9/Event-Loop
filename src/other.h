#ifndef OTHER_H
#define OTHER_H

#include<stdio.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdatomic.h>
#include<signal.h>
#include<errno.h>
#include<sys/stat.h>
#include<sys/unistd.h>
#include<stdlib.h>
#include<stdbool.h>
#include<unistd.h>
#include<fcntl.h>

#include<sys/event.h>
#include<sys/types.h>

#include<pthread.h>

#define MAX_THREAD_COUNT 4
#define BUF_SIZE 256
#define QUEUE_SIZE 128
#define BACKLOG 10

enum {
    PROTOCOL_TCP,
    PROTOCOL_UDP,
};

enum {
    SCREATE_ERR,
};

typedef void (* func) (void* args);
typedef struct task_node {
    func func;
    void *args;
} task_node_t;

typedef struct {
   task_node_t* q;
   int cap, len;
   int readAt, insertAt;
} queue_t;

bool init_queue(queue_t* q, int cap);
bool is_full(queue_t* q);
bool is_empty(queue_t* q);
bool push(queue_t* q, task_node_t val);
bool pop(queue_t* q, task_node_t* val);
void free_queue(queue_t* q);
void free_queue_heap(queue_t* q);

typedef struct {
    pthread_t thread[MAX_THREAD_COUNT];
    pthread_mutex_t lock;
    pthread_cond_t cond;
    queue_t* q;
    bool shutdown;
} thread_pool_t;

thread_pool_t* create_pool(int queueSize);
void* start_pool(void* tPool);
bool push_task(thread_pool_t* pool, void (* func) (void* args), void* arg);
void destroy_pool(thread_pool_t* pool, int threadCount);

typedef struct {
    int port;
    char host[24];
} addr_t;

typedef struct {
    struct sockaddr_storage s_addr;
    addr_t addr;
    int err;
    int fd;
} listener_t;

typedef struct {
    int fd;
    struct sockaddr_storage addr;
    addr_t c_addr;
    int err;
    char buf[216];
    uintptr_t timer_id;
    time_t last_active;
} client_t;

typedef struct Server {
    listener_t ln;
    // route_t route;
    thread_pool_t* pool;
    volatile sig_atomic_t shutdown_signal;
    // atomic_bool shut_down;

    time_t recv_timeout;
    time_t send_timeout;

    // events
    int e_fd;

} server_t;

// typedef struct thread_node {
//     route_t *route;
//     client_t client;
// } thread_node_t;


void s_close(listener_t ln);
client_t s_accept(listener_t ln);
void conn_close(client_t client);

typedef struct LinkedListNode {
    client_t client;
    struct LinkedListNode* prev;
    struct LinkedListNode* next;
} l_node_t;

typedef struct LinkedList {
    l_node_t* head;
    l_node_t* tail;
} list_t;

list_t init_list();
int push_back(list_t* list, client_t clinet);
l_node_t* search(list_t* list, int fd);
int delete(list_t* list, int fd);
void free_list(list_t* list);

int set_fd_nonblock(int fd);

#endif
