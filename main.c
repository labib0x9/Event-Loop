// #include<stdio.h>
// #include<unistd.h>
// #include<fcntl.h>

// #include<sys/event.h>
// #include<sys/types.h>

// #include<pthread.h>

// int pipe_fd[2];

// // writes to a pipe
// // thread worker
// void* worker(void* arg) {
//     WORK_TO_DO:
//         sleep(10);
//     char x = 'x';
//     if (write(pipe_fd[1], &x, 1) != 1) goto WORK_TO_DO;

//     printf("Worker= %zu, Wrote to pipe_fd[1]\n", (unsigned long) pthread_self());
//     return NULL;
// }

// int main() {    

//     // pipe creation
//     int ok = pipe(pipe_fd);
//     if (ok == -1) {
//         perror("Pipe");
//         return 0;
//     }

//     printf("===========================\n");

//     // non-blocking
//     int flags = fcntl(pipe_fd[0], F_GETFL, 0);
//     printf("READER FLAG= %d\n", flags);
//     fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
//     printf("READER FLAG= %d\n", flags | O_NONBLOCK);

//     flags = fcntl(pipe_fd[1], F_GETFL, 0);
//     printf("WRITER FLAG= %d\n", flags);
//     fcntl(pipe_fd[1], F_SETFL, flags | O_NONBLOCK);
//     printf("WRITER FLAG= %d\n", flags | O_NONBLOCK);

//     printf("===========================\n");

//     // Data flows, fd[1] -> fd[0]
//     printf("Read end, fd[0] = %d\n", pipe_fd[0]);
//     printf("Write end, fd[1] = %d\n", pipe_fd[1]);

//     printf("===========================\n");

//     // thread
//     pthread_t tid;
//     pthread_create(&tid, NULL, worker, NULL);

//     // kernel event queue is created 
//     int e_fd = kqueue();
//     if (e_fd == -1) {
//         perror("Kqueue");
//         return 0;
//     }

//     printf("KQUEUE_FD= %d\n", e_fd);

//     // Register a event
//     // will notify if pipe_fd[0] is ready to read.
//     struct kevent ev;
//     EV_SET(&ev, pipe_fd[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
//     if (kevent(e_fd, &ev, 1, NULL, 0, NULL) == -1) {
//         perror("Register");
//         return 0;
//     };

//     printf("EL started\n");
//     // event loop
//     struct kevent events[16];
//     int n = kevent(e_fd, NULL, 0, events, 16, NULL);    // hangs.... WHY ? BCZ, kernel blocks as no fd is ready to receieve...

//     printf("event, n= %d\n", n);

//     // iterate over all the event kernel returns...
//     for (int i = 0; i < n; i++) {
//         struct kevent* e = &events[i];
//         if (e->filter == EVFILT_READ) {
//             // ready to read
//             // Read only one time, bcz Write is send to only once..
//             char buf[64];
//             int m = read(pipe_fd[0], &buf, sizeof(buf) - 1);
//             buf[m] = '\0';
//             printf("READ DATA[%d]= %s\n", m, buf);
//         } else if (e->filter == EVFILT_WRITE) {
//             // ready to write
//         } else {
//             // do nothing
//         }
//     }

//     printf("===========================\n");

//     // wait for thread worker to finish...
//     pthread_join(tid, NULL);
 
//     close(e_fd);
//     close(pipe_fd[0]);
//     close(pipe_fd[1]);
    
// 	return 0;
// }


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

#define MAX_EVENT_COUNT 16

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


void s_close(listener_t ln) {
    close(ln.fd);
}

client_t s_accept(listener_t ln) {
    client_t conn;
    conn.err = 0;
    socklen_t len = sizeof(conn.addr);
    // printf("Listening...\n");
    conn.fd = accept(ln.fd, (struct sockaddr*) &conn.addr, &len);
    if (conn.fd < 0) {
        conn.err = 1;
        return conn;
    }
    // printf("Accepted... %d\n", conn.fd);
    return conn;
}

void conn_close(client_t client) {
    close(client.fd);
}

bool init_queue(queue_t* q, int cap) {
    if (q == NULL) return false;

    // for now, no long capacity
    if (cap <= 0 || cap > (int) 1e6) {
        cap = 1;
    }
    
    q->cap = cap;
    q->len = 0;
    q->readAt = q->insertAt = 0;
    
    task_node_t *qq = (task_node_t*) malloc(sizeof(task_node_t) * q->cap);
    if (qq == NULL) return false;

    q->q = qq;
    return true;
}

bool is_full(queue_t* q) {
    if (q == NULL) return false;
    return q->len == q->cap;
}

bool is_empty(queue_t* q) {
    if (q == NULL) return false;
    return q->len == 0;
}

// O(N) iteration.
// slow but canbe faster
bool resize(queue_t* q) {
    if (q == NULL) return false;
    if (!is_full(q)) return false;

    // greater than 1e7
    long long newCap = 2ll * q->cap;
    if (newCap >= (int) 1e7) return false;

    task_node_t* newQ = (task_node_t*) malloc(sizeof(task_node_t) * (int) newCap);
    if (newQ == NULL) return false;

    for (int i = 0; i < q->len; i++) {
        newQ[i] = q->q[(q->readAt + i) % q->cap];
    }

    free(q->q);
    q->q = newQ;
    q->cap = (int) newCap;
    q->insertAt = q->len;
    q->readAt = 0;
    return true;
}

// insert at back, if no room is free, then resizes by 2 * len
bool push(queue_t* q, task_node_t val) {
    if (q == NULL) return false;
    
    if (is_full(q)) {
        bool ok = resize(q);
        if (!ok) return ok;
    }
    
    q->q[q->insertAt] = val;
    q->insertAt = (q->insertAt + 1) % q->cap; 
    q->len++;
    return true;
}

bool pop(queue_t* q, task_node_t* val) {
    if (q == NULL) return false;
    if (is_empty(q)) return false;

    if (val != NULL) {
        *val = q->q[q->readAt];
    }
    
    q->readAt = (q->readAt + 1) % q->cap;
    q->len--;
    return true;
}

void free_queue_heap(queue_t* q) {
    if (q == NULL) return;
    if (q->q != NULL) free(q->q);
    if (q != NULL) free(q);
}

void free_queue(queue_t* q) {
    if (q == NULL) return;
    if (q->q != NULL) free(q->q);
}

thread_pool_t* create_pool(int queueSize) {
    thread_pool_t* pool = (thread_pool_t*) malloc(sizeof(thread_pool_t));
    if (pool == NULL) return NULL;

    pool->shutdown = false;
    
    pool->q = (queue_t*) malloc(sizeof(queue_t));
    if (pool->q == NULL) goto FREE_POOL;
    
    if (!init_queue(pool->q, queueSize)) goto FREE_QUEUE;
    if (pthread_mutex_init(&pool->lock, NULL) != 0) goto FREE_QUEUE;
    if (pthread_cond_init(&pool->cond, NULL) != 0) goto DESTROY_MUTEX;

    for (int i = 0; i < MAX_THREAD_COUNT; i++) {
        if (pthread_create(&(pool->thread[i]), NULL, start_pool, (void*) pool) != 0) {
            destroy_pool(pool, i);
            return NULL;
        }
    }
    
    printf("[info] Threadpool is created for %d threads\n", MAX_THREAD_COUNT);
    return pool;

    // Cleanups on failure
    DESTROY_MUTEX:
        pthread_mutex_destroy(&pool->lock);
    FREE_QUEUE:
        free_queue_heap(pool->q);
    FREE_POOL:
        free(pool);
    return NULL;
}

void destroy_pool(thread_pool_t* pool, int threadCount) {
    if (pool == NULL) return;

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    
    for (int i = 0; i < threadCount; i++) {
        pthread_join(pool->thread[i], NULL);
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    
    free_queue_heap(pool->q);
    free(pool);

    printf("[info] Threadpool is destrooyed\n");
}

bool push_task(thread_pool_t* pool, void (* func) (void* args), void* arg) {
    if (pool == NULL || func == NULL || arg == NULL) {
        return false;
    }

    pthread_mutex_lock(&pool->lock);
    if (pool->shutdown) goto NO_NEW_TASK;

    task_node_t node = {
        .func = func,
        .args = (void*) arg
    };

    if (!push(pool->q, node)) goto NO_NEW_TASK;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return true;

    // queue not accepting task
    NO_NEW_TASK:
        pthread_mutex_unlock(&pool->lock);
    return false;
}

void* start_pool(void* tPool) { 
    if (tPool == NULL) goto EXIT_WORKER;

    thread_pool_t* pool = (thread_pool_t*) (tPool);
    if (pool == NULL) goto EXIT_WORKER;

    task_node_t task;
    
    while(1) {
        pthread_mutex_lock(&pool->lock);

        while(is_empty(pool->q) && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }
        
        if (pool->shutdown && is_empty(pool->q)) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }
        
        bool ok = pop(pool->q, &task);
        pthread_mutex_unlock(&pool->lock);

        if (!ok) continue;
        
        // executing task...
        (*(task.func))(task.args);
    }

    EXIT_WORKER:
        pthread_exit(NULL);
    return NULL;
}

void handle_conn(void* arg) {
    (void) arg;
    // // set send, recv timeouts
    // struct timeval send_time_out = {.tv_sec = temp_server->send_timeout, .tv_usec = 0};
    // if(setsockopt(client.fd, SOL_SOCKET, SO_SNDTIMEO, &send_time_out, sizeof(send_time_out)) < 0) {
    //     printf("set send timeout error\n");
    //     return;
    // }

    // struct timeval recv_time_out = {.tv_sec = temp_server->recv_timeout, .tv_usec = 0};
    // if (setsockopt(client.fd, SOL_SOCKET, SO_RCVTIMEO, &recv_time_out, sizeof(recv_time_out)) < 0) {
    //     printf("set recv timeout error\n");
    //     return;
    // }

    // // client is ipv4
    // struct sockaddr_in *temp_client = (struct sockaddr_in*) &client.addr;
    // char clinet_ip[INET_ADDRSTRLEN];
    // inet_ntop(AF_INET, &temp_client, clinet_ip, INET_ADDRSTRLEN);

    // printf("[%d][%s:%d] Conn handling\n", client.fd, clinet_ip, ntohs(temp_client->sin_port));

    // while(1) {
    // }
}

int set_fd_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// SERVER CONFIG HERE
server_t server;

static void handle_signals(int sig) {
    (void) sig;
    if (sig == SIGINT || sig == SIGTERM || sig == SIGKILL) {
        // kill server .....
        // exit(0);
        server.shutdown_signal = true;
    }
}

typedef struct LinkedListNode {
    client_t client;
    struct LinkedListNode* prev;
    struct LinkedListNode* next;
} l_node_t;

typedef struct LinkedList {
    l_node_t* head;
    l_node_t* tail;
} list_t;

list_t init_list() {
    list_t list = {
        .head = NULL,
        .tail = NULL,
    };
    return list;
}

// insert client at the back, O(1) push_back
// if fails return 1, else 0
int push_back(list_t* list, client_t clinet) {
    l_node_t* node = (l_node_t*)malloc(sizeof(l_node_t));
    if (node == NULL) {
        // error 
        perror("Node malloc");
        return 1;
    }

    node->client = clinet;
    node->next = NULL;
    node->prev = list->tail;

    if (list->tail)
        list->tail->next = node;
    list->tail = node;

    if (!list->head)
        list->head = node;

    return 0;
}

// search via file descriptor, fd
// TC: O(n)
l_node_t* search(list_t* list, int fd) {
    for (l_node_t* node = list->head; node != NULL; node = node->next) {
        if (node->client.fd == fd) {
            return node;
        }
    }
    return NULL;
}

// delete via file descriptor, fd
// TC: O(n)
// fails on return 1, else 0
int delete(list_t* list, int fd) {
    l_node_t* node = search(list, fd);
    if (node == NULL) return 1;
    if (node->prev) node->prev->next = node->next;
    else list->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else list->tail = node->prev;
    free(node);
    return 0;
}

// // delete via l_node_t
// // TC: O(1)
// void delete(list_t* list, l_node_t* node) {}
// client fd are closed here
void free_list(list_t* list) {
    while (list->head != NULL) {
        l_node_t* node = list->head;
        list->head = list->head->next;
        if (node != NULL) {
            close(node->client.fd);
            free(node);
        }
    } 
}

void accept_client(listener_t ln, list_t* client_list) {
    while (1) {
        client_t conn = s_accept(ln);
        if (conn.fd == -1 || conn.err != 0) {
            perror("s_accept()");
            return;
        }

        struct sockaddr_in *temp_client = (struct sockaddr_in*) &conn.addr;
        char clinet_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &temp_client, clinet_ip, INET_ADDRSTRLEN);

        memcpy(&conn.c_addr.host, clinet_ip, sizeof(clinet_ip));
        conn.c_addr.host[sizeof(clinet_ip)] = '\0';
        conn.c_addr.port = ntohs(temp_client->sin_port);

        printf("[%d][%s:%d] Conn handling\n", conn.fd, conn.c_addr.host, conn.c_addr.port);

        if (set_fd_nonblock(conn.fd) == -1) {
            conn_close(conn);
            // no event on client, so need to remove it, just close the connection....
            continue;
        };

        push_back(client_list, conn);

        // create a event to read on client fd...
        struct kevent ev;
        EV_SET(&ev, conn.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

        // create a event to timeout client on 10sec....
        // EV_SET(&ev, conn.fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 10000, 0, NULL);
        l_node_t* node = search(client_list, conn.fd);
        if (node == NULL) {
            printf("No client found\n");
            return;
        }
        client_t* temp = &node->client;
        
        EV_SET(&ev, conn.fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 10000, temp);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
    }
}

void read_from_client(int client_fd, list_t* client_list) {
    l_node_t* node = search(client_list, client_fd);
    if (node == NULL) {
        printf("No client found\n");
        return;
    }
    client_t* conn = &node->client;
    (void) conn;

    while (1) {
        int n = recv(conn->fd, conn->buf, sizeof(conn->buf) - 1, 0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            // close connection.....
            break;
        }
        // client disconnected........
        if (n == 0) {
            // close the fd, and remove event from the event queue...
            printf("[FD=%d] Disconnected\n", conn->fd);
            close(conn->fd);
            break;
        }
        conn->buf[n] = '\0';
        printf("[%d][%s:%d] Conn Recv: %s\n", conn->fd, conn->c_addr.host, conn->c_addr.port, conn->buf);

        // create a write event ....
        struct kevent kv;
        EV_SET(&kv, conn->fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, conn);
        kevent(server.e_fd, &kv, 1, NULL, 0, NULL);

        // re timer
        EV_SET(&kv, conn->fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 10000, conn);
        kevent(server.e_fd, &kv, 1, NULL, 0, NULL);
    }
}

void write_to_client(int client_fd, list_t* client_list, client_t* client) {
    l_node_t* node = search(client_list, client_fd);
    if (node == NULL) {
        printf("No client found\n");
        return;
    }
    client_t* conn = &node->client;
    (void) conn;
    (void) client;

    if (strlen(conn->buf) == 0) {
        // nothing to write
        printf("ZERO LEN\n");
        return;
    }

    int n = send(client_fd, conn->buf, strlen(conn->buf), 0);
    if (n == (int) strlen(conn->buf)) {
        printf("[%d][%s:%d] All bytes are written\n", conn->fd, conn->c_addr.host, conn->c_addr.port);
        printf("[%d][%s:%d] msg= %s\n", conn->fd, conn->c_addr.host, conn->c_addr.port, client->buf);
    }
    
    // delete the write event...
    struct kevent kv;
    EV_SET(&kv, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, &kv, 1, NULL, 0, NULL);

    // re timer
    EV_SET(&kv, conn->fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 10000, conn);
    kevent(server.e_fd, &kv, 1, NULL, 0, NULL);
}

void handle_client_timeout(int client_fd, list_t* client_list, client_t* client) {
    (void) client_fd;
    (void) client_list;
    (void) client;

    l_node_t* node = search(client_list, client_fd);
    if (node == NULL) {
        printf("No client found\n");
        return;
    }
    client_t* conn = &node->client;
    (void) conn;
    (void) client;

    printf("[%d][%s:%d] TiMeOuT hIt\n", conn->fd, conn->c_addr.host, conn->c_addr.port);
}

int main() {

    list_t client_list = init_list();

    // Signal handling
    struct sigaction sa = {0};
    sa.sa_handler = handle_signals;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
    
    // // Threadpool
    // thread_pool_t* pool = create_pool(QUEUE_SIZE);
    // if (pool == NULL) {
    //     perror("Pool");
    //     return 0;
    // }

    // event queue fd
    int e_fd = kqueue();
    if (e_fd == -1) {
        perror("Kqueue");
        return 0;
    }

    // server socket
    listener_t ln;
    ln.addr.port = 8080;
    ln.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ln.fd == -1) {
        perror("Listener");
        return 0;
    }

    int opt = 1;
    if (setsockopt(ln.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsocket error = SO_REUSEADDR");
        return 0;
    }

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ln.addr.port);
    addr.sin_addr.s_addr = INADDR_ANY;
    memcpy(&(ln.s_addr), &addr, addr_len);
    if (bind(ln.fd, (struct sockaddr*) &(ln.s_addr), addr_len) == -1) {
        perror("bind error");
        return 0;
    }
    if (listen(ln.fd, BACKLOG) == -1) {
        perror("listen error");
        return 0;
    }

    if (set_fd_nonblock(ln.fd) == -1) {
        perror("listener fd nonblock");
        return 0;
    }

    // server and listen
    // create a event to listen on listener fd

    struct kevent ev;
    EV_SET(&ev, ln.fd, EVFILT_READ, EV_ADD|EV_CLEAR, 0, 0, NULL);
    kevent(e_fd, &ev, 1, NULL, 0, NULL);

    struct kevent events[MAX_EVENT_COUNT];
    // register_read_event(e_fd, ln.fd, EV_ADD | EV_CLEAR);

    // struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    struct timespec ts = {.tv_sec = 10, .tv_nsec = 0};

    printf("[info] EL= %d, Sock=%d\n", e_fd, ln.fd);

    // server config
    // server_t server = {
        server.e_fd = e_fd;
        server.ln = ln;
        // server.pool = pool;
        server.recv_timeout = 10;
        server.send_timeout = 10;
        server.shutdown_signal = false;
        // linked list later.... (client list) ....
    // };

    while (server.shutdown_signal == false) {
        int n = kevent(e_fd, NULL, 0, events, MAX_EVENT_COUNT, &ts);

        for (int i = 0; i < n; i++) {
            struct kevent* e = &events[i];
            int efd = e->ident;

            if (e->filter == EVFILT_READ) {
                if (e->flags & EV_EOF) {
                    // close conn
                    printf("[%d] DISCONNECT\n", efd);
                    continue;
                }
                if (efd == ln.fd) {
                    // listener fd, accept client
                    accept_client(ln, &client_list);
                } else {
                    // client fd, read
                    read_from_client(efd, &client_list);
                }
            } else if (e->filter == EVFILT_WRITE) {
                // write to client
                write_to_client(efd, &client_list, e->udata);
            } else if (e->filter == EVFILT_TIMER) {
                // client timeout
                handle_client_timeout(efd, &client_list, e->udata);
            } else {
                printf("UNKNOWN-FILTER: %d\n", (int)e->filter);
            }
        }
    }

    // clean ups
    // destroy_pool(pool, MAX_THREAD_COUNT);
    free_list(&client_list);
    close(e_fd);
    close(ln.fd);

    printf("ShutDown Server\n");

    return 0;
}