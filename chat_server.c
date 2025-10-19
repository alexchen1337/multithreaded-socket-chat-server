// alex chen



#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "dllist.h"
#include "jrb.h"
#include "sockettome.h"

// holds chat room map
typedef struct {
    JRB room_map;
} ChatServer;

// tracks active chat room
typedef struct {
    char* title;
    Dllist clients;
    Dllist queue;
    pthread_t tid;
    pthread_mutex_t *mutex;
    pthread_cond_t *msg_ready;
} ChatRoom;

// tracks individual clients
typedef struct {
    ChatRoom* joined;
    pthread_t tid;
    int conn;
    FILE* in;
    FILE* out;
    char uname[100];
} User;

ChatServer* active_server;

// clean shutdown
void shutdown_handler(int unused) {
    signal(SIGINT, shutdown_handler);
    JRB r;
    jrb_traverse(r, active_server->room_map) {
        ChatRoom* rm = (ChatRoom*) r->val.v;
        pthread_detach(rm->tid);
        Dllist tmp;
        dll_traverse(tmp, rm->queue) free(tmp->val.s);
        dll_traverse(tmp, rm->clients) free((User*)tmp->val.v);
        free(rm->title);
        free(rm->mutex);
        free(rm->msg_ready);
        free_dllist(rm->queue);
        free_dllist(rm->clients);
        free(rm);
    }
    jrb_free_tree(active_server->room_map);
    free(active_server);
    exit(0);
}

void* manage_room(void *data) {
    ChatRoom* room = data;
    room->queue = new_dllist();
    
    while (1) {
        pthread_mutex_lock(room->mutex);
        while (dll_empty(room->queue)) pthread_cond_wait(room->msg_ready, room->mutex);
        
        Dllist c, m;
        dll_traverse(m, room->queue) {
            char* text = m->val.s;
            dll_traverse(c, room->clients) {
                User* p = (User*)c->val.v;
                fprintf(p->out, text);
                fflush(p->out);
            }
            free(m->val.s);
        }
        
        free_dllist(room->queue);
        room->queue = new_dllist();
        
        pthread_mutex_unlock(room->mutex);
    }
    return NULL;
}

ChatRoom* join_chatroom(User* user, char* title) {
    JRB node;
    jrb_traverse(node, active_server->room_map) {
        ChatRoom* rm = (ChatRoom*)node->val.v;
        if (strcmp(rm->title, title) == 0) {
            pthread_mutex_lock(rm->mutex);
            dll_append(rm->clients, new_jval_v((void*)user));
            pthread_mutex_unlock(rm->mutex);
            return rm;
        }
    }
    return NULL;
}

void queue_message(User* user, char* message) {
    pthread_mutex_lock(user->joined->mutex);
    dll_append(user->joined->queue, new_jval_s(strdup(message)));
    pthread_cond_signal(user->joined->msg_ready);
    pthread_mutex_unlock(user->joined->mutex);
}

void* handle_client(void *data) {
    User* p = data;
    p->in = fdopen(p->conn, "r");
    if (!p->in) exit(1);
    p->out = fdopen(p->conn, "w");
    if (!p->out) exit(1);
    JRB j;
    Dllist d;
    char tmp[350];
    fprintf(p->out, "Chat Rooms:\n\n");
    jrb_traverse(j, active_server->room_map) {
        ChatRoom* rm = (ChatRoom*)j->val.v;
        sprintf(tmp, "%s:", rm->title);
        pthread_mutex_lock(rm->mutex);
        dll_traverse(d, rm->clients) {
            User* c = (User*)d->val.v;
            strcat(tmp, " ");
            strcat(tmp, c->uname);
        }
        pthread_mutex_unlock(rm->mutex);
        strcat(tmp, "\n");
        fprintf(p->out, tmp);
    }
    fprintf(p->out, "\nEnter your chat name (no spaces):\n");
    fflush(p->out);
    if (fscanf(p->in, "%s", p->uname) != 1) exit(0);
    do {
        fprintf(p->out, "Enter chat room:\n");
        fflush(p->out);
        if (fscanf(p->in, "%s", tmp) != 1) exit(0);
        p->joined = join_chatroom(p, tmp);
        if (!p->joined) fprintf(p->out, "No chat room %s.\n", tmp);
    } while (!p->joined);

    char line[300];
    sprintf(tmp, "%s has joined\n", p->uname);
    queue_message(p, tmp);
    while (fgets(line, 300, p->in) != NULL) {
        if (strcmp(line, "\n") == 0) continue;
        sprintf(tmp, "%s: %s", p->uname, line);
        queue_message(p, tmp);
    }
    pthread_mutex_lock(p->joined->mutex);
    dll_traverse(d, p->joined->clients) {
        User* q = (User*)d->val.v;
        if (strcmp(q->uname, p->uname) == 0) {
            dll_delete_node(d);
            break;
        }
    }
    sprintf(tmp, "%s has left\n", p->uname);
    dll_append(p->joined->queue, new_jval_s(strdup(tmp)));
    pthread_cond_signal(p->joined->msg_ready);
    pthread_mutex_unlock(p->joined->mutex);
    close(p->conn);
    fclose(p->in);
    fclose(p->out);
    free(p);
    return NULL;
}

// Function to create a new chat room
ChatRoom* create_chat_room(const char* title) {
    ChatRoom* r = malloc(sizeof(ChatRoom));
    r->title = strdup(title);
    r->clients = new_dllist();
    r->mutex = malloc(sizeof(pthread_mutex_t));
    r->msg_ready = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(r->mutex, NULL);
    pthread_cond_init(r->msg_ready, NULL);
    return r;
}

// Function to display server information
void print_server_info(int port) {
    char host[100];
    gethostname(host, 100);
    printf("port: %d\n", port);
}

int main(int argc, char** argv) {
    // Check command line arguments
    if (argc < 3) {
        fprintf(stderr, "error", argv[0]);
        exit(1);
    }
    
    // Get port number
    int port = atoi(argv[1]);
    
    // Initialize server
    active_server = malloc(sizeof(ChatServer));
    active_server->room_map = make_jrb();
    
    // Set up signal handler
    signal(SIGINT, shutdown_handler);
    
    // Create chat rooms
    for (int i = 0; i < argc - 2; i++) {
        ChatRoom* r = create_chat_room(argv[2 + i]);
        jrb_insert_str(active_server->room_map, r->title, new_jval_v((void*)r));
        if (pthread_create(&r->tid, NULL, manage_room, r) != 0) exit(1);
    }
    
    // Display connection information
    print_server_info(port);
    
    // Start server socket and accept connections
    int sock = serve_socket(port);
    
    // Main server loop
    while (1) {
        int fd = accept_connection(sock);
        User* p = malloc(sizeof(User));
        p->conn = fd;
        if (pthread_create(&p->tid, NULL, handle_client, p) != 0) exit(1);
    }
    
    return 0;
}
