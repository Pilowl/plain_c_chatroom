#include <time.h>
#include <signal.h>

#include "common.h"
#include "utils.h"

#define SHUTDOWN_TIME 5
#define MAX_CLIENTS   64

// Message meta = encoded user id + user name 
#define MSG_META_OFFSET 37

// Message command types.
typedef const uint8_t command_t;
#define CMD_USER_MESSAGE      (command_t)0x01
#define CMD_SERVER_MESSAGE    (command_t)0x02
#define CMD_USER_CONNECTED    (command_t)0x03
#define CMD_USER_DISCONNECTED (command_t)0x04

#define NAME_LENGTH 32

const char *LOG_FILE_NAME = "logs.txt";

// Server message strings.
char *SERVER_MSG_ROOM_IS_FULL = "Chat room is full. Please, try again later.";
char *SERVER_PREFIX           = "<SERVER>";
char *SERVER_MSG_ENTER_NAME   = "Please, enter your name: ";
char *SERVER_MSG_NAME_ERROR   = "Wrong name. Your name should have length from 4 to 31.";

// Strings, used in formatted output.
const char *USER_CONNECTED_MESSAGE_FMT    = "User %s has joined.";
const char *USER_DISCONNECTED_MESSAGE_FMT = "User %s has been disconnected.";
const char *SERVER_SHUTDOWN_MESSAGE_FMT   = "Server is (almost) gracefully shutting down in %d";

// Connected client structure definition.
typedef struct
{
    uint id;
    char name[32];
    char *buff;
    struct sockaddr_in addr;
    int sockfd;
    pthread_t tid;
} client;

void prepare_server(struct sockaddr_in* server_addr, int port) {
    (*server_addr).sin_family = AF_INET;
    (*server_addr).sin_addr.s_addr = htonl(INADDR_ANY);
    (*server_addr).sin_port = htons(port);
}

// Time/datetime string formats.
const char TIME_FMT[] =     "%H:%M:%S";
const char DATETIME_FMT[] = "%d-%m-%Y %H:%M:%S";

int client_count = 0;
client *clients[MAX_CLIENTS];

pthread_mutex_t client_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_std_lock  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_file_lock = PTHREAD_MUTEX_INITIALIZER;

int sockfd;
// Making socket port reusable.
void set_socket_reusable(int sockfd)
{
    int socketoption = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &socketoption, sizeof(socketoption));
}

// Tracking server shutdown state.
static volatile int server_is_shutting_down = 0;

// Stdio logs (printf).
void log_std(const char *msg)
{  
    time_t timer;
    timer = time(NULL);
    char datetime[32];
    struct tm *tm_info = localtime(&timer);
    strftime(datetime, sizeof(datetime), DATETIME_FMT, tm_info);
    pthread_mutex_lock(&log_std_lock);
    printf("[%s] <LOG> %s\n", datetime, msg);
    pthread_mutex_unlock(&log_std_lock);
}

void log_file(const char *msg)
{
    time_t timer;
    timer = time(NULL);
    char datetime[32];
    struct tm *tm_info = localtime(&timer);
    strftime(datetime, sizeof(datetime), DATETIME_FMT, tm_info);

    pthread_mutex_lock(&log_file_lock);
    FILE *fp;
    fp = fopen(LOG_FILE_NAME, "a");
    fprintf(fp, "[%s] %s\n", datetime, msg);
    fclose(fp);
    pthread_mutex_unlock(&log_file_lock);
}

// Single client message sending.
void send_to_cli(command_t cmd, client *cli, char *args, int len)
{
    switch(cmd)
    {
        case CMD_USER_MESSAGE:
            write(cli->sockfd, args, len);
            break;
        case CMD_SERVER_MESSAGE: ;
            char fmt_msg[512] = {};
            len = sprintf(fmt_msg, "%s %s", SERVER_PREFIX, args);
            write(cli->sockfd, fmt_msg, len);
            break;
        default:
            log_std("Error sending message. Unknown command code: %d");
    }
}

// Message broadcasting to clients.
void broadcast(command_t cmd, char *args)
{
    switch (cmd)
    {
        case CMD_USER_MESSAGE: ;
            time_t timer;
            timer = time(NULL);
            char time[20];
            struct tm *tm_info = localtime(&timer);
            strftime(time, sizeof(time), TIME_FMT, tm_info);
            
            char id_bytes[4] = {};
            copy_range(args, id_bytes, 0, 4);
            uint sender_id = uint_from_bytes(id_bytes);
            
            char name[32] = {};
            copy_range(args, name, 4, 32);
            
            char text[280] = {};
            copy_range(args, text, 37, MSG_LENGTH);

            char msg[512];
            int len = sprintf(msg, "[%s] <%s (id: %u)> %s", time, name, sender_id, text);
            log_std(msg);

            pthread_mutex_lock(&client_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL && clients[i]->id != sender_id)
                {
                    send_to_cli(CMD_USER_MESSAGE, clients[i], msg, len);
                }
            }
            pthread_mutex_unlock(&client_lock);

            break;
        case CMD_USER_CONNECTED:
            len = sprintf(msg, USER_CONNECTED_MESSAGE_FMT, args);
            log_std(msg);
            log_file(msg);
            pthread_mutex_lock(&client_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL)
                {
                    send_to_cli(CMD_SERVER_MESSAGE, clients[i], msg, len);
                }
            }
            pthread_mutex_unlock(&client_lock);

            break;
        case CMD_USER_DISCONNECTED:
            len = sprintf(msg, USER_DISCONNECTED_MESSAGE_FMT, args);
            log_std(msg);
            log_file(msg);
            pthread_mutex_lock(&client_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL)
                {
                    send_to_cli(CMD_SERVER_MESSAGE, clients[i], msg, len);
                }
            }
            pthread_mutex_unlock(&client_lock);

            break;
        case CMD_SERVER_MESSAGE:
            pthread_mutex_lock(&client_lock);
            log_std(args);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL)
                {
                    send_to_cli(CMD_SERVER_MESSAGE, clients[i], args, strlen(args));
                }
            }
            pthread_mutex_unlock(&client_lock);

            break;
        default:
            break;
    }
}

uint add_client(client *cli)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == NULL)
        {
            cli->id = i;
            clients[i] = cli;
            client_count++;
            return i;
        }
    }
}

void remove_client(uint id)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != NULL && id == clients[i]->id)
        {
            clients[i] = NULL;
            client_count--;
            return;
        }
    }
}


// Client handling.
void *handle(void *arg)
{
    client *cli = (client*) arg;

    // Check if space in chatroom is available.
    pthread_mutex_lock(&client_lock);
    int room_full = client_count >= MAX_CLIENTS;
    pthread_mutex_unlock(&client_lock);

    if (room_full)
    {
        send_to_cli(CMD_SERVER_MESSAGE, cli, SERVER_MSG_ROOM_IS_FULL, sizeof(SERVER_MSG_ROOM_IS_FULL));
        close(cli->sockfd);
        free(cli);
        
        pthread_exit(NULL);
        return NULL;
    }
    
    // Initializing buffer for client message receiving.
    cli->buff = malloc(sizeof(char) * MSG_LENGTH);

    // Client name buffer.
    char name_buff[64] = {};
    uint authorized = 0;
    send_to_cli(CMD_SERVER_MESSAGE, cli, SERVER_MSG_ENTER_NAME, sizeof(SERVER_MSG_ENTER_NAME));

    // Loop for client name getting.
    while (!authorized)
    {
        int receive = recv(cli->sockfd, cli->buff, 32, 0);
        if (receive > 0)
        {
            // Server-sided name length validation.
            if (strlen(cli->buff) < 5 || strlen(cli->buff) > 32)
            {
                send_to_cli(CMD_SERVER_MESSAGE, cli, SERVER_MSG_NAME_ERROR, sizeof(SERVER_MSG_NAME_ERROR));
                bzero(cli->buff, sizeof(cli));
            }
            else
            {
                strncpy(cli->name, cli->buff, NAME_LENGTH);
                trim(cli->name, NAME_LENGTH);
                authorized = 1;
            }
        }
        else
        {
            close(cli->sockfd);
            free(cli->buff);
            free(cli);
            
            pthread_exit(NULL);
            return NULL;
        }
    }

    pthread_mutex_lock(&client_lock);
    cli->id = add_client(cli);
    pthread_mutex_unlock(&client_lock);

    // "User connected" message broadcasting to all users.
    broadcast(CMD_USER_CONNECTED, cli->name);

    // Array for storing client message along with space for client metadata (if needed).
    char msg[512] = {};
    // Converting id to char[] for storing in message metadata.
    uint_to_bytes(msg, cli->id);
    // Filling in client's name in message meta.
    for (int i = 0; i < sizeof(cli->name); i++)
    {
        msg[4+i] = cli->name[i];
        if (cli->name[i] == '\0')
            break;
    }

    // Message receiving loop.
    for(;;)
    {
        bzero_range(msg, MSG_META_OFFSET, strlen(cli->buff));
        bzero(cli->buff, MSG_LENGTH);
        int receive = recv(cli->sockfd, cli->buff, MSG_LENGTH, 0);
        if (receive > 0)
        {
            if (strlen(cli->buff) > 0)
            {
                trim(cli->buff, MSG_LENGTH);
                for (int i = 0; i < strlen(cli->buff); i++)
                {
                    msg[MSG_META_OFFSET + i] = cli->buff[i];
                }
                broadcast(CMD_USER_MESSAGE, msg);
            }
        }
        else
        {
            // Client disconnected.
            break;
        }
        sleep(1);
    }

    pthread_mutex_lock(&client_lock);
    remove_client(cli->id);
    pthread_mutex_unlock(&client_lock);
    
    // Broadcasting 'User disconnected' message to all users
    broadcast(CMD_USER_DISCONNECTED, cli->name);

    close(cli->sockfd);
    free(cli);
    free(cli->buff);
    
    pthread_exit(NULL);
}

/*
 * Graceful shutdown with freeing (almost) all allocted memory and closing (almost) all sockets.
 * If user is not in 'clients' array, possible memory leak :(
 * TODO: fix memory leak possibilities
*/
void graceful_shutdown(int flag)
{
    server_is_shutting_down = 1;
    for (int i = 0; i < SHUTDOWN_TIME; i++)
    {
        char msg[64] = {};
        sprintf(msg, SERVER_SHUTDOWN_MESSAGE_FMT, SHUTDOWN_TIME-i);
        broadcast(CMD_SERVER_MESSAGE, msg);
        sleep(1);
    }

    pthread_mutex_lock(&client_lock);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != NULL)
        {
            pthread_cancel(clients[i]->tid);
            close(clients[i]->sockfd);
            free(clients[i]->buff);
            free(clients[i]);
            client_count--;
        }
    }
    pthread_mutex_unlock(&client_lock);
    
    close(sockfd);
    printf("Server is shutted down.\n");
    exit(flag);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: ./server <server_port>\n");
        exit(0);
    }

    int port = atoi(argv[1]);

    struct sockaddr_in server_addr, client_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("Failed to create socket, exiting...\n");
        exit(1);
    }
    set_socket_reusable(sockfd);

    prepare_server(&server_addr, port);
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0)
    {
        printf("Failed to bind socket, exiting...\n");
        exit(1);
    }
    
    if (listen(sockfd, 10) != 0) 
    {
        printf("Failed to listen, exiting...\n");
        exit(1);
    }

    signal(SIGINT, graceful_shutdown);

    printf("Waiting for connections...\n");

    int connfd;
    for(;;)
    {
        socklen_t client_len = sizeof(client_addr);
        connfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (!server_is_shutting_down)
        {
            client *cli = (client *)malloc(sizeof(client));        
            cli->addr = client_addr;
            cli->sockfd = connfd;
            pthread_create(&cli->tid, NULL, &handle, (void*) cli);
        }
        else
        {
            close(connfd);
        }
    }
}
