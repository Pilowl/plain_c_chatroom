// Commands
typedef int command_t;

#define MAX_CLIENTS 64

#define SERVER_PREFIX         "<SERVER>"
#define SERVER_MSG_ENTER_NAME "Please, enter your name: "
#define SERVER_MSG_NAME_ERROR "Wrong name. Your name should have length from 4 to 31."  

#define CMD_USER_MESSAGE      (command_t)0x01
#define CMD_SERVER_MESSAGE    (command_t)0x02
#define CMD_USER_CONNECTED    (command_t)0x03
#define CMD_USER_DISCONNECTED (command_t)0x04

// Mesage meta
#define NAME_LENGTH 32

#define LOG_FILE_NAME "logs.txt"