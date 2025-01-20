#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define DEFAULT_PORT 8080
#define MIN_PORT 2001 // Minimum allowed port number
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct
{
    int socket;
    char username[BUFFER_SIZE];
    int username_set;     // Flag to check if username is set
    pthread_t thread_id;  // Thread ID for the client
    int removed_by_admin; // Flag to track if the client was removed by the admin
} client_info;

int server_socket;
client_info *clients[MAX_CLIENTS]; // Array of pointers to dynamically allocated client_info
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_running = 1; // Global flag to indicate server status

void *handle_client(void *arg);
void *handle_input(void *arg);
void broadcast_message(const char *message, int sender_socket);
void send_private_message(const char *message, int sender_socket, const char *recipient);
void send_server_private_message(const char *message, const char *recipient);
void list_clients(int client_socket);
void remove_client(int socket);
void admin_remove_client(const char *username);
int is_username_unique(const char *username);
void send_server_help();
void shutdown_server();

int main(int argc, char *argv[])
{
    // Ignore SIGPIPE signals
    signal(SIGPIPE, SIG_IGN);

    int port = DEFAULT_PORT; // Default port

    // Parse command-line arguments
    if (argc == 2)
    {
        port = atoi(argv[1]); // Use the provided port
        if (port <= MIN_PORT || port > 65535)
        {
            fprintf(stderr, "Invalid port number. Port must be greater than %d and less than or equal to 65535.\n", MIN_PORT);
            exit(EXIT_FAILURE);
        }
    }
    else if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        if (errno == EADDRINUSE)
        {
            fprintf(stderr, "Port %d is already in use.\n", port);
        }
        else
        {
            perror("Bind failed");
        }
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 3) < 0)
    {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    pthread_t admin_thread;
    if (pthread_create(&admin_thread, NULL, handle_input, NULL) != 0)
    {
        perror("Failed to create admin input thread");
        exit(EXIT_FAILURE);
    }

    // Accept incoming connections
    while (server_running)
    {
        int new_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (new_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("New connection accepted. Socket: %d\n", new_socket); // Debug line to track connections

        pthread_mutex_lock(&clients_mutex);
        if (client_count < MAX_CLIENTS)
        {
            client_info *new_client = malloc(sizeof(client_info));
            if (new_client == NULL)
            {
                perror("Malloc failed");
                close(new_socket);
                pthread_mutex_unlock(&clients_mutex);
                continue;
            }

            new_client->socket = new_socket;
            strncpy(new_client->username, "Anonymous", BUFFER_SIZE - 1);
            new_client->username_set = 0; // Username not set initially
            clients[client_count++] = new_client;

            printf("[%i] Clients connected to the server\n", client_count);

            // Prompt the client to set a username
            char prompt_message[BUFFER_SIZE];
            snprintf(prompt_message, sizeof(prompt_message), "[SERVER]: Please set your username using /username <name>");
            send(new_socket, prompt_message, strlen(prompt_message), 0);

            // Create a thread to handle the client
            pthread_t tid;
            if (pthread_create(&tid, NULL, handle_client, new_client) != 0)
            {
                perror("Thread creation failed");
                close(new_socket);
                free(new_client);
                client_count--;
            }
            else
            {
                new_client->thread_id = tid;                          // Store the thread ID
                printf("Thread created for client %d\n", new_socket); // Debug line to track thread creation
            }
        }
        else
        {
            printf("Max clients reached. Connection rejected.\n");
            close(new_socket);
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    close(server_socket);
    return 0;
}

// Handle client communication
void *handle_client(void *arg)
{
    client_info *client = (client_info *)arg;
    int socket = client->socket;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (server_running && (bytes_read = recv(socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        if (strncmp(buffer, "/username ", 10) == 0)
        {
            char *requested_username = buffer + 10;
            pthread_mutex_lock(&clients_mutex);
            if (is_username_unique(requested_username))
            {
                // Remove leading/trailing spaces from the requested username
                char cleaned_username[BUFFER_SIZE];
                snprintf(cleaned_username, sizeof(cleaned_username), "%s", requested_username);
                // Ensure the username does not exceed the buffer size and is properly null-terminated
                cleaned_username[BUFFER_SIZE - 1] = '\0';

                // Check if the username is valid (non-empty after trimming)
                if (strlen(cleaned_username) > 0)
                {
                    // Copy the cleaned and validated username into the client structure
                    strncpy(client->username, cleaned_username, BUFFER_SIZE - 1);
                    client->username[BUFFER_SIZE - 1] = '\0'; // Ensure null-termination
                    client->username_set = 1;                 // Username is now set

                    // Construct a success message
                    char success_message[BUFFER_SIZE + 50]; // Extra space for the prefix
                    snprintf(success_message, sizeof(success_message), "[SERVER]: Username set to %s", client->username);

                    // Send the success message to the client
                    if (send(socket, success_message, strlen(success_message), 0) < 0)
                    {
                        perror("send failed");
                        // Handle the error, e.g., close the socket or return
                    }
                    else
                    {
                        // Construct a notification message for the new user joining
                        char notification_message[BUFFER_SIZE + 50];
                        snprintf(notification_message, sizeof(notification_message), "[SERVER]: '%s' has joined the chat room.", client->username);

                        pthread_mutex_unlock(&clients_mutex);
                        // Broadcast to all clients except the new client
                        broadcast_message(notification_message, socket);
                        pthread_mutex_lock(&clients_mutex);
                    }
                }
                else
                {
                    char error_message[BUFFER_SIZE];
                    snprintf(error_message, sizeof(error_message), "[SERVER]: Invalid username. Please provide a non-empty username.");
                    if (send(socket, error_message, strlen(error_message), 0) < 0)
                    {
                        perror("send failed");
                        // Handle the error, e.g., close the socket or return
                    }
                }
            }
            else
            {
                char error_message[BUFFER_SIZE];
                snprintf(error_message, sizeof(error_message), "[SERVER]: The username is already taken.");
                if (send(socket, error_message, strlen(error_message), 0) < 0)
                {
                    perror("send failed");
                    // Handle the error, e.g., close the socket or return
                }
            }
            pthread_mutex_unlock(&clients_mutex);
        }
        else if (strcmp(buffer, "/help") == 0)
        {
            if (socket == server_socket)
            {
                send_server_help();
            }
            else
            {
                char error_message[BUFFER_SIZE];
                snprintf(error_message, sizeof(error_message), "[SERVER]: You do not have permission to see the server help.");
                send(socket, error_message, strlen(error_message), 0);
            }
        }
        else if (strncmp(buffer, "/private ", 8) == 0)
        {
            if (client->username_set)
            {
                char *recipient = strtok(buffer + 8, " ");
                char *message = strtok(NULL, "\0");
                if (recipient && message)
                {
                    send_private_message(message, socket, recipient);
                }
            }
            else
            {
                char error_message[BUFFER_SIZE];
                snprintf(error_message, sizeof(error_message), "[SERVER]: You must set a username before sending messages.");
                send(socket, error_message, strlen(error_message), 0);
            }
        }
        else if (strcmp(buffer, "/list") == 0)
        {
            list_clients(socket); // Send the list of usernames to the client
        }
        else if (strcmp(buffer, "/quit") == 0)
        {
            // Send the goodbye message to the client
            char goodbye_message[BUFFER_SIZE + 50];
            snprintf(goodbye_message, sizeof(goodbye_message), "[SERVER]: Goodbye, %s!", client->username);
            if (send(socket, goodbye_message, strlen(goodbye_message), 0) < 0)
            {
                perror("Send failed");
            }

            // Notify others about this client quitting
            char quit_message[BUFFER_SIZE + 50];
            snprintf(quit_message, sizeof(quit_message), "[SERVER]: %s has left the chat.", client->username);
            broadcast_message(quit_message, socket); // Broadcast the quit message

            // Remove the client
            close(socket);
            remove_client(socket);
            pthread_exit(NULL);
        }
        else if (strcmp(buffer, "/shutdown") == 0)
        {
            // Only the server can shut itself down
            char error_message[BUFFER_SIZE];
            snprintf(error_message, sizeof(error_message), "[SERVER]: You do not have permission to shut down the server.");
            send(socket, error_message, strlen(error_message), 0);
        }
        else
        {
            if (client->username_set)
            {
                // Use a larger buffer for the formatted message
                char formatted_message[BUFFER_SIZE * 2 + 10]; // Extra space for the prefix
                snprintf(formatted_message, sizeof(formatted_message), "[%s]: %s", client->username, buffer);

                // Broadcast the message to all clients
                broadcast_message(formatted_message, socket);

                // Log the broadcast message in the server console
                printf("%s\n", formatted_message); // Log in [username]: <message> format
            }
            else
            {
                char error_message[BUFFER_SIZE];
                snprintf(error_message, sizeof(error_message), "[SERVER]: You must set a username before sending messages.");
                send(socket, error_message, strlen(error_message), 0);
            }
        }
    }

    // Detect client disconnection
    if (bytes_read == 0)
    {
        printf("Client %s disconnected.\n", client->username);
    }
    else
    {
        perror("Receive failed");
    }

    // Check if the client was removed by the admin
    if (!client->removed_by_admin)
    {
        // Notify others about this client disconnecting
        char quit_message[BUFFER_SIZE + 50];
        snprintf(quit_message, sizeof(quit_message), "[SERVER]: %s disconnected.", client->username);
        broadcast_message(quit_message, socket); // Broadcast the disconnection message
    }

    close(socket);
    remove_client(socket);
    pthread_exit(NULL);
}

// Handle admin communication
void *handle_input(void *arg)
{
    char buffer[BUFFER_SIZE];
    while (server_running)
    {                                // Keep running as long as the server is active
        if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
        {
            buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

            // Process the command
            if (strcmp(buffer, "/help") == 0)
            {
                send_server_help();
            }
            else if (strcmp(buffer, "/shutdown") == 0)
            {
                shutdown_server();
            }
            else if (strcmp(buffer, "/list") == 0)
            {
                list_clients(server_socket);
            }
            else if (strncmp(buffer, "/remove ", 7) == 0)
            {
                char *recipient = strtok(buffer + 8, " ");
                if (recipient)
                {
                    admin_remove_client(recipient);
                    printf("%s Removed!\n", recipient);
                }
            }
            else if (strncmp(buffer, "/private ", 8) == 0)
            {
                char *recipient = strtok(buffer + 8, " ");
                char *message = strtok(NULL, "\0");
                if (recipient && message)
                {
                    send_server_private_message(message, recipient);
                }
            }
            else if (strncmp(buffer, "/message ", 8) == 0)
            {
                // Extract the message body (everything after "/message ")
                char *message_body = buffer + 8;

                // Format the message as "[SERVER]: <message_body>"
                char formatted_message[BUFFER_SIZE + 20]; // Extra space for "[SERVER]: "
                snprintf(formatted_message, sizeof(formatted_message), "[SERVER]: %s", message_body);

                // Broadcast the formatted message to all clients except the server
                broadcast_message(formatted_message, server_socket);
            }
            else
            {
                printf("Unknown command. Type /help for a list of commands.\n");
            }
        }
    }
    return NULL;
}

// Broadcast a message to all clients except the sender
void broadcast_message(const char *message, int sender_socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i]->socket != sender_socket)
        {
            if (send(clients[i]->socket, message, strlen(message), 0) < 0)
            {
                perror("Send failed");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Send a private message to a specific client
void send_private_message(const char *message, int sender_socket, const char *recipient)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i]->username, recipient) == 0)
        {
            // Find the sender's username
            char sender_username[BUFFER_SIZE];
            for (int j = 0; j < client_count; j++)
            {
                if (clients[j]->socket == sender_socket)
                {
                    strncpy(sender_username, clients[j]->username, BUFFER_SIZE - 1);
                    break;
                }
            }

            char formatted_message[BUFFER_SIZE * 2];
            snprintf(formatted_message, sizeof(formatted_message), "[Private from %s]: %s", sender_username, message);
            if (send(clients[i]->socket, formatted_message, strlen(formatted_message), 0) < 0)
            {
                perror("Send failed");
            }
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Send a private message from the server to a specific client
void send_server_private_message(const char *message, const char *recipient)
{
    pthread_mutex_lock(&clients_mutex);
    int recipient_found = 0;
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i]->username, recipient) == 0)
        {
            char formatted_message[BUFFER_SIZE * 2];
            snprintf(formatted_message, sizeof(formatted_message), "[Private from SERVER]: %s", message);
            if (send(clients[i]->socket, formatted_message, strlen(formatted_message), 0) < 0)
            {
                perror("Send failed");
            }
            recipient_found = 1;
            break;
        }
    }
    if (!recipient_found)
    {
        printf("[SERVER]: Recipient '%s' not found.", recipient);
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Send the list of connected clients to a specific client
void list_clients(int client_socket)
{
    if (client_socket == server_socket)
    {
        char list[BUFFER_SIZE * 3] = "Connected clients:\n";
        for (int i = 0; i < client_count; i++)
        {
            strcat(list, clients[i]->username);
            strcat(list, "\n");
        }
        printf("%s", list); // Print the list of connected clients
    }
    else
    {
        pthread_mutex_lock(&clients_mutex);
        char list[BUFFER_SIZE * 3] = "Connected clients:\n";
        for (int i = 0; i < client_count; i++)
        {
            strcat(list, clients[i]->username);
            strcat(list, "\n");
        }
        if (send(client_socket, list, strlen(list), 0) < 0)
        {
            perror("Send failed");
        }
        pthread_mutex_unlock(&clients_mutex);
    }
}

// Remove a client from the list
void remove_client(int socket)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i]->socket == socket)
        {
            free(clients[i]);                     // Free the dynamically allocated memory
            clients[i] = clients[--client_count]; // Replace with the last client
            clients[client_count] = NULL;         // Clear the last entry
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void admin_remove_client(const char *username)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i]->username, username) == 0)
        {
            // Send the goodbye message to the client
            char remove_message[BUFFER_SIZE + 50];
            snprintf(remove_message, sizeof(remove_message), "[SERVER]: You are kicked out by the admin!");
            if (send(clients[i]->socket, remove_message, strlen(remove_message), 0) < 0)
            {
                perror("Send failed");
            }

            // Mark the client as removed by the admin
            clients[i]->removed_by_admin = 1;

            free(clients[i]);                     // Free the dynamically allocated memory
            clients[i] = clients[--client_count]; // Replace with the last client
            clients[client_count] = NULL;         // Clear the last entry
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Check if a username is unique
int is_username_unique(const char *username)
{
    for (int i = 0; i < client_count; i++)
    {
        if (strcmp(clients[i]->username, username) == 0)
        {
            return 0; // Username is not unique
        }
    }
    return 1; // Username is unique
}

// Send server help instructions
void send_server_help()
{
    printf("\a"); // This will produce a beep sound in the terminal
    printf("\n[SERVER HELP]:\n"
           "/help - Show this help message\n"
           "/list - List all connected clients\n"
           "/message - Send a public message to all clients\n"
           "/private <username> <message> - Send a private message to a user\n"
           "/remove <username> - Remove the user with that username\n"
           "/shutdown - Shut down the server\n\n");
}

// Shut down the server gracefully
void shutdown_server()
{
    printf("Server is shutting down...\n");

    // Notify all clients that the server is shutting down
    char shutdown_message[BUFFER_SIZE];
    snprintf(shutdown_message, sizeof(shutdown_message), "[SERVER]: The server is shutting down. You will be disconnected.");
    broadcast_message(shutdown_message, -1); // Send to all clients

    // Close all client sockets
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        close(clients[i]->socket); // Close the socket
        free(clients[i]);          // Free the dynamically allocated memory
    }
    client_count = 0; // Reset the client count
    pthread_mutex_unlock(&clients_mutex);

    // Set the server_running flag to 0
    server_running = 0;

    // Close the server socket
    close(server_socket);

    printf("Server has been shut down.\n");
    exit(0); // Exit the server process
}