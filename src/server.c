#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define MAX_SESSIONS 100
#define UUID_LENGTH 37
#define PASSWORD_LENGTH 9
#define USERNAME_LENGTH 9
#define ADMIN_PASSWORD "admin123"

typedef struct {
    char uuid[UUID_LENGTH];
    char username[USERNAME_LENGTH];
    char password[PASSWORD_LENGTH];
    time_t start_time;
    int duration_minutes;
    int is_active;
    int extension_used;
} Session;

Session sessions[MAX_SESSIONS];
int session_count = 0;

// Validate UUID format (basic validation)
int is_valid_uuid(const char* uuid) {
    if (strlen(uuid) != 36) return 0;
    
    // Check for hyphens at positions 8, 13, 18, 23
    if (uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-') {
        return 0;
    }
    
    // Check that other characters are hex digits
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!((uuid[i] >= '0' && uuid[i] <= '9') || 
              (uuid[i] >= 'A' && uuid[i] <= 'F') || 
              (uuid[i] >= 'a' && uuid[i] <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

// Generate random password and username
void generate_credentials(char* username, char* password) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    // Generate 8-character username
    for (int i = 0; i < 8; i++) {
        username[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    username[8] = '\0';
    
    // Generate 8-character password
    for (int i = 0; i < 8; i++) {
        password[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    password[8] = '\0';
}

// Find session by UUID
int find_session(const char* uuid) {
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].uuid, uuid) == 0 && sessions[i].is_active) {
            return i;
        }
    }
    return -1;
}

// Create new session
int create_session(const char* uuid, int duration_minutes) {
    if (session_count >= MAX_SESSIONS) {
        return -1;
    }
    
    Session* session = &sessions[session_count];
    strcpy_s(session->uuid, sizeof(session->uuid), uuid);
    generate_credentials(session->username, session->password);
    session->start_time = time(NULL);
    session->duration_minutes = duration_minutes;
    session->is_active = 1;
    session->extension_used = 0;
    
    return session_count++;
}

// Handle client requests
void handle_client(SOCKET client_socket) {
    char buffer[1024] = {0};
    char response[1024] = {0};
    
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        printf("Client disconnected or recv failed: %d\n", WSAGetLastError());
        closesocket(client_socket);
        return;
    }
    
    buffer[bytes_received] = '\0';
    printf("Received: %s\n", buffer);
    
    // Parse request
    if (strncmp(buffer, "BOOK:", 5) == 0) {
        // Format: BOOK:UUID:DURATION_MINUTES
        char uuid[UUID_LENGTH];
        int duration;
          if (sscanf_s(buffer + 5, "%36s:%d", uuid, sizeof(uuid), &duration) == 2) {
            if (!is_valid_uuid(uuid)) {
                strcpy_s(response, sizeof(response), "ERROR:INVALID_UUID_FORMAT");
            } else if (duration <= 0 || duration > 480) { // Max 8 hours
                strcpy_s(response, sizeof(response), "ERROR:INVALID_DURATION");
            } else {
                int session_id = create_session(uuid, duration);
                if (session_id >= 0) {
                    Session* session = &sessions[session_id];
                    sprintf_s(response, sizeof(response), 
                        "SUCCESS:USERNAME:%s:PASSWORD:%s:DURATION:%d",
                        session->username, session->password, session->duration_minutes);
                    printf("Created session for UUID %s - Username: %s, Password: %s\n", 
                        uuid, session->username, session->password);
                } else {
                    strcpy_s(response, sizeof(response), "ERROR:SESSION_LIMIT_REACHED");
                }
            }
        } else {
            strcpy_s(response, sizeof(response), "ERROR:INVALID_FORMAT");
        }
    }
    else if (strncmp(buffer, "VALIDATE:", 9) == 0) {
        // Format: VALIDATE:USERNAME:PASSWORD
        char username[USERNAME_LENGTH], password[PASSWORD_LENGTH];
        
        if (sscanf_s(buffer + 9, "%8s:%8s", username, sizeof(username), password, sizeof(password)) == 2) {
            int found = 0;
            for (int i = 0; i < session_count; i++) {
                if (sessions[i].is_active && 
                    strcmp(sessions[i].username, username) == 0 && 
                    strcmp(sessions[i].password, password) == 0) {
                    
                    time_t current_time = time(NULL);
                    int elapsed_minutes = (int)(current_time - sessions[i].start_time) / 60;
                    int remaining_minutes = sessions[i].duration_minutes - elapsed_minutes;
                    
                    if (remaining_minutes > 0) {
                        sprintf_s(response, sizeof(response), 
                            "VALID:REMAINING:%d:UUID:%s", remaining_minutes, sessions[i].uuid);
                        found = 1;
                    } else {
                        sessions[i].is_active = 0;
                        strcpy_s(response, sizeof(response), "EXPIRED");
                    }
                    break;
                }
            }
            if (!found) {
                strcpy_s(response, sizeof(response), "INVALID");
            }
        } else {
            strcpy_s(response, sizeof(response), "ERROR:INVALID_FORMAT");
        }
    }
    else if (strncmp(buffer, "EXTEND:", 7) == 0) {
        // Format: EXTEND:UUID
        char uuid[UUID_LENGTH];
        
        if (sscanf_s(buffer + 7, "%36s", uuid, sizeof(uuid)) == 1) {
            int session_id = find_session(uuid);
            if (session_id >= 0 && !sessions[session_id].extension_used) {
                sessions[session_id].duration_minutes += 30;
                sessions[session_id].extension_used = 1;
                
                time_t current_time = time(NULL);
                int elapsed_minutes = (int)(current_time - sessions[session_id].start_time) / 60;
                int remaining_minutes = sessions[session_id].duration_minutes - elapsed_minutes;
                
                sprintf_s(response, sizeof(response), "EXTENDED:REMAINING:%d", remaining_minutes);
                printf("Extended session for UUID %s by 30 minutes\n", uuid);
            } else if (session_id >= 0 && sessions[session_id].extension_used) {
                strcpy_s(response, sizeof(response), "ERROR:ALREADY_EXTENDED");
            } else {
                strcpy_s(response, sizeof(response), "ERROR:SESSION_NOT_FOUND");
            }
        } else {
            strcpy_s(response, sizeof(response), "ERROR:INVALID_FORMAT");
        }
    }
    else if (strncmp(buffer, "STATUS:", 7) == 0) {
        // Format: STATUS:UUID
        char uuid[UUID_LENGTH];
        
        if (sscanf_s(buffer + 7, "%36s", uuid, sizeof(uuid)) == 1) {
            int session_id = find_session(uuid);
            if (session_id >= 0) {
                time_t current_time = time(NULL);
                int elapsed_minutes = (int)(current_time - sessions[session_id].start_time) / 60;
                int remaining_minutes = sessions[session_id].duration_minutes - elapsed_minutes;
                
                if (remaining_minutes > 0) {
                    sprintf_s(response, sizeof(response), 
                        "ACTIVE:REMAINING:%d:EXTENDED:%d", 
                        remaining_minutes, sessions[session_id].extension_used);
                } else {
                    sessions[session_id].is_active = 0;
                    strcpy_s(response, sizeof(response), "EXPIRED");
                }
            } else {
                strcpy_s(response, sizeof(response), "NOT_FOUND");
            }
        } else {
            strcpy_s(response, sizeof(response), "ERROR:INVALID_FORMAT");
        }
    }
    else {
        strcpy_s(response, sizeof(response), "ERROR:UNKNOWN_COMMAND");
    }
    
    send(client_socket, response, (int)strlen(response), 0);
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
      printf("Lab Management Server Starting...\n");
    
    // Seed random number generator once
    srand((unsigned int)time(NULL));
    
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
      // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed with error: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }
    
    printf("Server listening on port %d\n", PORT);
    printf("Commands:\n");
    printf("  BOOK:UUID:DURATION_MINUTES - Create new session\n");
    printf("  VALIDATE:USERNAME:PASSWORD - Validate credentials\n");
    printf("  EXTEND:UUID - Extend session by 30 minutes\n");
    printf("  STATUS:UUID - Check session status\n\n");
      // Accept connections
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed with error: %d\n", WSAGetLastError());
            continue;
        }
        
        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        // Handle client in separate thread (simplified version - handle synchronously)
        handle_client(client_socket);
    }
    
    closesocket(server_socket);
    WSACleanup();
    return 0;
}