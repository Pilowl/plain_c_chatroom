#include <time.h>

#include "common.h"
#include "server.h"
#include "utils.h"

#define USER_CONNECTED_MESSAGE_FMT    "User %s has joined."
#define USER_DISCONNECTED_MESSAGE_FMT "User %s has been disconnected."
#define USER_MESSAGE_PREFIX_FMT       "[%s]"

typedef struct
{
    uint id;
    char name[32];
    struct sockaddr_in addr;
    int sockfd;
    pthread_t tid;
} client;

void prepare_server(struct sockaddr_in* server_addr, int port) {
    (*server_addr).sin_family = AF_INET;
    (*server_addr).sin_addr.s_addr = htonl(INADDR_ANY);
    (*server_addr).sin_port = htons(port);
}

const uint MSG_META_OFFSET = 37;

const char TIME_FMT[] =     "%H:%M:%S";
const char DATETIME_FMT[] = "%d-%m-%Y %H:%M:%S";

int client_count = 0;
client *clients[MAX_CLIENTS];

pthread_mutex_t client_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_file_lock = PTHREAD_MUTEX_INITIALIZER;

void copy_range(const char *src, char *dst, size_t start, size_t count)
{
    int j = 0;
    for (int i = start; i < start + count; i++)
    {
        dst[j++] = src[i];
    }
}

void log_std(const char *msg)
{
    time_t timer;
    timer = time(NULL);
    char datetime[32];
    struct tm *tm_info = localtime(&timer);
    strftime(datetime, sizeof(datetime), DATETIME_FMT, tm_info);
    printf("[%s] <LOG> %s\n", datetime, msg);
}

void log_file(const char *msg)
{
    time_t timer;
    timer = time(NULL);
    char datetime[32];
    struct tm *tm_info = localtime(&timer);
    strftime(datetime, sizeof(datetime), DATETIME_FMT, tm_info);

    FILE *fp;

    fp = fopen(LOG_FILE_NAME, "a");
    fprintf(fp, "[%s] %s\n", datetime, msg);
    fclose(fp);
}

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
        case CMD_USER_CONNECTED:
            write(cli->sockfd, args, len);
            break;
        case CMD_USER_DISCONNECTED:
            write(cli->sockfd, args, len);
            break;
        default:
            printf("Error sending message. Unknown command code: %d\n", cmd);
    }
}

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
            uint sender_id = id_from_bytes(id_bytes);
            
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
            pthread_mutex_lock(&client_lock);
            len = sprintf(msg, USER_CONNECTED_MESSAGE_FMT, args);
            log_std(msg);
            log_file(msg);
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
            pthread_mutex_lock(&client_lock);
            len = sprintf(msg, USER_DISCONNECTED_MESSAGE_FMT, args);
            log_std(msg);
            log_file(msg);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL)
                {
                    send_to_cli(CMD_SERVER_MESSAGE, clients[i], msg, len);
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

void *handle(void *arg)
{
    char *buff = malloc(sizeof(char)*MSG_LENGTH);
    char msg[512] = {};
    client *cli = (client*) arg;
    
    char name_buff[64] = {};
    uint authorized = 0;
    send_to_cli(CMD_SERVER_MESSAGE, cli, SERVER_MSG_ENTER_NAME, sizeof(SERVER_MSG_ENTER_NAME));
    while (!authorized)
    {
        int receive = recv(cli->sockfd, buff, 32, 0);
        if (receive > 0)
        {
            if (strlen(buff) < 5 || strlen(buff) > 32)
            {
                send_to_cli(CMD_SERVER_MESSAGE, cli, SERVER_MSG_NAME_ERROR, sizeof(SERVER_MSG_NAME_ERROR));
                bzero(buff, sizeof(buff));
            }
            else
            {
                strncpy(cli->name, buff, NAME_LENGTH);
                trim(cli->name, NAME_LENGTH);
                authorized = 1;
            }
        }
        else
        {
            close(cli->sockfd);
            free(cli);
            free(buff);
            
            pthread_detach(pthread_self());
            return NULL;
        }
    }

    pthread_mutex_lock(&client_lock);
    cli->id = add_client(cli);
    pthread_mutex_unlock(&client_lock);
    broadcast(CMD_USER_CONNECTED, cli->name);

    id_to_bytes(msg, cli->id);
    for (int i = 0; i < sizeof(cli->name); i++)
    {
        msg[4+i] = cli->name[i];
        if (cli->name[i] == '\0')
            break;
    }

    for(;;)
    {
        bzero(buff, MSG_LENGTH);
        int receive = recv(cli->sockfd, buff, MSG_LENGTH, 0);
        if (receive > 0)
        {
            if (strlen(buff) > 0)
            {
                trim(buff, MSG_LENGTH);
                for (int i = 0; i < sizeof(buff); i++)
                {
                    msg[MSG_META_OFFSET + i] = buff[i];
                }
                broadcast(CMD_USER_MESSAGE, msg);
            }
        }
        else
        {
            break;
        }
        sleep(1);
    }

    pthread_mutex_lock(&client_lock);
    remove_client(cli->id);
    client_count--;
    pthread_mutex_unlock(&client_lock);
    
    broadcast(CMD_USER_DISCONNECTED, cli->name);

    close(cli->sockfd);
    free(cli);
    free(buff);
    
    pthread_detach(pthread_self());
}

int main()
{
    int sockfd, connfd;
    struct sockaddr_in server_addr, client_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        printf("Failed to create socket, exiting...\n");
        exit(1);
    }

    prepare_server(&server_addr, 1234);
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
    printf("Waiting for connections...\n");
  
    for(;;) 
    {
        socklen_t client_len = sizeof(client_addr);
        connfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        client *cli = (client *)malloc(sizeof(client));        
        cli->addr = client_addr;
        cli->sockfd = connfd;
        pthread_create(&cli->tid, NULL, &handle, (void*) cli);                
    } 
}
