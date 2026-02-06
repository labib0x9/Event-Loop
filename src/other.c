#include"other.h"

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

int set_fd_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}