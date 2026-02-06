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
#include"other.h"

#define MAX_EVENT_COUNT 16

// SERVER CONFIG HERE
server_t server;
list_t client_list;
struct kevent events[MAX_EVENT_COUNT];
// struct timespec ts = {.tv_sec = 10, .tv_nsec = 0};

static void handle_signals(int sig) {
    (void) sig;
    if (sig == SIGINT || sig == SIGTERM || sig == SIGKILL) {
        // kill server .....
        // exit(0);
        server.shutdown_signal = true;
    }
}

int init() {
    client_list = init_list();

    // Signal handling
    struct sigaction sa = {0};
    sa.sa_handler = handle_signals;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    // sigaction(SIGKILL, &sa, NULL);
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

    // register_read_event(e_fd, ln.fd, EV_ADD | EV_CLEAR);

    // struct timeval tv = {.tv_sec = 10, .tv_usec = 0};

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

    return 1;
}


/* EVENT LOOPS */

void remove_client(int client_fd) {
    (void) client_fd;
    struct kevent ev[3];
    EV_SET(&ev[0], client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[1], client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[2], client_fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, ev, 3, NULL, 0, NULL);
    close(client_fd);
    delete(&client_list, client_fd);
    printf("[%d] DISCONNECT\n", client_fd);
}

void accept_client(listener_t ln, list_t* client_list) {
    while (1) {
        client_t conn = s_accept(ln);
        if (conn.fd == -1) {
            return;
        }

        if (conn.err != 0) {
            conn_close(conn);
            perror("s_accept()");
            return;
        }

        // SOMETHING TO SEE HERE, temp_client, temp_client.sin_addr
        struct sockaddr_in *temp_client = (struct sockaddr_in*) &conn.addr;
        char clinet_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &temp_client->sin_addr, clinet_ip, INET_ADDRSTRLEN);

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

        // NEED TO THINK HERE....

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

        uintptr_t timer_id = (uintptr_t) conn.fd;
        EV_SET(&ev, timer_id, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 10000, temp);
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
            remove_client(conn->fd);
            break;
        }
        // client disconnected........
        if (n == 0) {
            // close the fd, and remove event from the event queue...
            printf("[FD=%d] Disconnected\n", conn->fd);
            // close(conn->fd);
            remove_client(conn->fd);
            break;
        }
        conn->buf[n] = '\0';
        printf("[%d][%s:%d] Conn Recv: %s\n", conn->fd, conn->c_addr.host, conn->c_addr.port, conn->buf);

        // create a write event ....
        struct kevent ev;
        EV_SET(&ev, conn->fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, conn);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

        // DELETE AND READD...

        // delete
        uintptr_t timer_id = (uintptr_t) conn->fd;
        EV_SET(&ev, timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
        // re timer
        EV_SET(&ev, timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 10000, conn);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
    }
}

void write_to_client(int client_fd, list_t* client_list, client_t* client) {
    // // We don't need to search for client on linked list via client fd;;;
    // l_node_t* node = search(client_list, client_fd);
    // if (node == NULL) {
    //     printf("No client found\n");
    //     return;
    // }
    // client_t* conn = &node->client;
    // (void) conn;
    (void) client;
    (void) client_list;
    (void) client_fd;

    if (strlen(client->buf) == 0) {
        // nothing to write
        printf("ZERO LEN\n");
        return;
    }

    int n = send(client->fd, client->buf, strlen(client->buf), 0);
    if (n == (int) strlen(client->buf)) {
        printf("[%d][%s:%d] All bytes are written\n", client->fd, client->c_addr.host, client->c_addr.port);
        printf("[%d][%s:%d] msg= %s\n", client->fd, client->c_addr.host, client->c_addr.port, client->buf);
    }
    
    // delete the write event...
    struct kevent ev;
    EV_SET(&ev, client->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

    // DELETE AND READD...

    // delete
    uintptr_t timer_id = (uintptr_t) client->fd;
    EV_SET(&ev, timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
    // re timer
    EV_SET(&ev, timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 10000, client);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
}

void handle_client_timeout(int client_fd, list_t* client_list, client_t* client) {
    (void) client_fd;
    (void) client_list;
    (void) client;
    char buf[] = "TIMEOUT\n";
    send(client->fd, buf, sizeof(buf), 0);
    printf("[%d][%s:%d] TiMeOuT hIt\n", client->fd, client->c_addr.host, client->c_addr.port);
    remove_client(client->fd);
}

int main() {
    printf("Initializeing.....\n");
    init();
    sleep(10);
    printf("Client can connect now\n");

    while (server.shutdown_signal == false) {
        int n = kevent(server.e_fd, NULL, 0, events, MAX_EVENT_COUNT, NULL);

        for (int i = 0; i < n; i++) {
            struct kevent* e = &events[i];
            int efd = e->ident;

            if (e->filter == EVFILT_READ) {
                if (e->flags & EV_EOF) {
                    // close conn
                    // printf("[%d] ERROR\n", efd);
                    // perror("ERROR---FD: ");
                    printf("[%d] peer closed connection\n", efd);
                    remove_client(efd);
                    continue;
                }
                if (efd == server.ln.fd) {
                    // listener fd, accept client
                    accept_client(server.ln, &client_list);
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
    close(server.e_fd);
    close(server.ln.fd);

    printf("ShutDown Server\n");

    return 0;
}