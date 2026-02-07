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
static uint64_t timer_counter = 0;

static void handle_signals(int sig) {
    (void) sig;
    if (sig == SIGINT || sig == SIGTERM) {
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
    sigaction(SIGPIPE, &sa, NULL);

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
    EV_SET(&ev, ln.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    kevent(e_fd, &ev, 1, NULL, 0, NULL);

    printf("[info] EL= %d, Sock=%d\n", e_fd, ln.fd);

    server.e_fd = e_fd;
    server.ln = ln;
    server.recv_timeout = 10;
    server.send_timeout = 10;
    server.shutdown_signal = false;

    return 1;
}


/* EVENT LOOPS */

void remove_client(int client_fd, char* funcc) {
    (void) client_fd;
    (void) funcc;

    struct kevent ev[3];
    l_node_t* node = search(&client_list, client_fd);
    if (node == NULL) {
        printf("FD=%d No client found\n", client_fd);
        return;
    }
    client_t* temp = &node->client;
    uintptr_t timer_id = temp->timer_id;

    EV_SET(&ev[0], client_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[1], client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[2], temp->timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, ev, 3, NULL, 0, NULL);
    close(client_fd);
    delete(&client_list, client_fd);

    printf("[%d][%lu][%s] REMOVE_CLIENT FD::)\n", client_fd, timer_id, funcc);

}

void accept_client(listener_t ln, list_t* client_list) {
    while (1) {
        client_t conn = s_accept(ln);
        if (conn.fd == -1) break;

        if (conn.err != 0) {
            conn_close(conn);
            break;
        }

        // SOMETHING TO SEE HERE, temp_client, temp_client.sin_addr
        struct sockaddr_in *temp_client = (struct sockaddr_in*) &conn.addr;
        char clinet_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &temp_client->sin_addr, clinet_ip, INET_ADDRSTRLEN);

        memcpy(&conn.c_addr.host, clinet_ip, sizeof(clinet_ip));
        conn.c_addr.host[sizeof(clinet_ip)] = '\0';
        conn.c_addr.port = ntohs(temp_client->sin_port);

        if (set_fd_nonblock(conn.fd) == -1) {
            conn_close(conn);
            // no event on client, so need to remove it, just close the connection....
            continue;
        };

        conn.timer_id = ++timer_counter;
        conn.last_active = time(NULL);

        push_back(client_list, conn);

        printf("[%d][%lu][%s:%d] Conn handling\n", conn.fd, conn.timer_id, conn.c_addr.host, conn.c_addr.port);


        // NEED TO THINK HERE....

        // create a event to read on client fd...
        struct kevent ev;
        EV_SET(&ev, conn.fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

        // create a event to timeout client on 10sec....
        EV_SET(&ev, conn.fd, EVFILT_TIMER, EV_ADD | EV_ENABLE, 10000, 0, NULL);
        l_node_t* node = search(client_list, conn.fd);
        if (node == NULL) {
            printf("No client found\n");
            break;;
        }
        client_t* temp = &node->client;

        EV_SET(&ev, conn.timer_id, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 10000, temp);
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
            remove_client(conn->fd, "read_from_client()");
            break;
        }
        // client disconnected........
        if (n == 0) {
            // close the fd, and remove event from the event queue...
            printf("[FD=%d] Disconnected\n", conn->fd);
            remove_client(conn->fd, "read_from_client()");
            break;
        }
        conn->buf[n] = '\0';
        printf("[%d][%s:%d] Conn Recv: %s\n", conn->fd, conn->c_addr.host, conn->c_addr.port, conn->buf);

        conn->last_active = time(NULL);

        // create a write event ....
        struct kevent ev;
        EV_SET(&ev, conn->fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, conn);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

        // DELETE AND READD...

        // delete
        EV_SET(&ev, conn->timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
        // re timer
        EV_SET(&ev, conn->timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 10000, conn);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
    }
}

void write_to_client(int client_fd, list_t* client_list, client_t* client) {
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

    client->last_active = time(NULL);
    
    // delete the write event...
    struct kevent ev;
    EV_SET(&ev, client->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);

    // // DELETE AND READD...

    // delete
    EV_SET(&ev, client->timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
    // re timer
    EV_SET(&ev, client->timer_id, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 10000, client);
    kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
}

void handle_client_timeout(int ident, list_t* client_list, client_t* client) {
    (void) ident;
    (void) client_list;
    (void) client;
    if (client->fd == 0) {
        struct kevent ev;
        EV_SET(&ev, ident, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        kevent(server.e_fd, &ev, 1, NULL, 0, NULL);
        return;
    }

    printf("[%lu:%lu][%lu]", client->last_active, time(NULL), time(NULL) - client->last_active);

    char buf[] = "TIMEOUT\n";
    send(client->fd, buf, sizeof(buf), 0);

    printf("[%d][%s:%d] TiMeOuT hIt\n", client->fd, client->c_addr.host, client->c_addr.port);
    
    remove_client(client->fd, "handle_client_timeout()");
}

int main() {
    printf("Initializeing.....\n");
    init();
    printf("Client can connect now\n");

    while (server.shutdown_signal == false) {
        int n = kevent(server.e_fd, NULL, 0, events, MAX_EVENT_COUNT, NULL);
        for (int i = 0; i < n; i++) {
            struct kevent* e = &events[i];
            int efd = e->ident;
            if (e->filter == EVFILT_READ) {
                if (e->flags & EV_EOF) {
                    printf("[%d] peer closed connection\n", efd);
                    remove_client(efd, "EV_EOF()");
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
                handle_client_timeout(efd, &client_list, e->udata);
            } else {
                printf("UNKNOWN-FILTER: %d\n", (int)e->filter);
            }
        }
    }

    printf("Shutting Down Server\n");
    
    free_list(&client_list);
    close(server.e_fd);
    close(server.ln.fd);

    printf("ShutDown Server\n");

    return 0;
}