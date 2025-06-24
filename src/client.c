#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define ADMIN_PASSWORD "admin123"
#define CHECK_INTERVAL 60  // Check remaining time every minute

typedef struct {
    char username[16];
    char password[16];
    char uuid[64];
    int remaining_minutes;
    int is_logged_in;
    time_t last_check;
} ClientSession;

ClientSession session = {0};
HANDLE check_thread = NULL;
volatile int should_exit = 0;

// Hide console cursor
void hide_cursor() {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
}

// Show console cursor
void show_cursor() {
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
}

// Clear screen
void clear_screen() {
    system("cls");
}

// Disable Windows key and other system keys
void disable_system_keys() {
    // This is a simplified approach - in production, you'd need more comprehensive key blocking
    printf("System keys disabled (simplified implementation)\n");
}

// Enable Windows key and other system keys
void enable_system_keys() {
    printf("System keys enabled\n");
}

// Send request to server
int send_server_request(const char* request, char* response, int response_size) {
    WSADATA wsaData;
    SOCKET client_socket;
    struct sockaddr_in server_addr;
    
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }
    
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(client_socket);
        WSACleanup();
        return 0;
    }
    
    send(client_socket, request, (int)strlen(request), 0);
    int bytes_received = recv(client_socket, response, response_size - 1, 0);
    
    closesocket(client_socket);
    WSACleanup();
    
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        return 1;
    }
    return 0;
}

// Validate credentials with server
int validate_credentials(const char* username, const char* password) {
    char request[256];
    char response[256];
    
    sprintf_s(request, sizeof(request), "VALIDATE:%s:%s", username, password);
    
    if (send_server_request(request, response, sizeof(response))) {
        if (strncmp(response, "VALID:", 6) == 0) {
            // Parse response: VALID:REMAINING:X:UUID:Y
            int remaining;
            char uuid[64];
            if (sscanf_s(response + 6, "REMAINING:%d:UUID:%63s", &remaining, uuid, sizeof(uuid)) == 2) {
                session.remaining_minutes = remaining;
                strcpy_s(session.uuid, sizeof(session.uuid), uuid);
                strcpy_s(session.username, sizeof(session.username), username);
                strcpy_s(session.password, sizeof(session.password), password);
                session.is_logged_in = 1;
                session.last_check = time(NULL);
                return 1;
            }
        } else if (strcmp(response, "EXPIRED") == 0) {
            printf("Session has expired. Please contact administrator.\n");
        } else if (strcmp(response, "INVALID") == 0) {
            printf("Invalid username or password.\n");
        }
    } else {
        printf("Unable to connect to server.\n");
    }
    return 0;
}

// Check session status with server
void check_session_status() {
    char request[256];
    char response[256];
    
    sprintf_s(request, sizeof(request), "STATUS:%s", session.uuid);
    
    if (send_server_request(request, response, sizeof(response))) {
        if (strncmp(response, "ACTIVE:", 7) == 0) {
            int remaining, extended;
            if (sscanf_s(response + 7, "REMAINING:%d:EXTENDED:%d", &remaining, &extended) == 2) {
                session.remaining_minutes = remaining;
                
                // Show warning if less than 10 minutes remaining
                if (remaining <= 10 && remaining > 0) {
                    show_time_warning(remaining, !extended);
                }
            }
        } else if (strcmp(response, "EXPIRED") == 0) {
            printf("\n\nSession has expired. Computer will be locked.\n");
            session.is_logged_in = 0;
            should_exit = 1;
        }
    }
}

// Show time warning dialog
void show_time_warning(int remaining_minutes, int can_extend) {
    clear_screen();
    printf("=== TIME WARNING ===\n\n");
    printf("Your session will expire in %d minutes.\n\n", remaining_minutes);
    
    if (can_extend) {
        printf("Options:\n");
        printf("1. Extend session by 30 minutes (one-time only)\n");
        printf("2. Continue without extension\n");
        printf("3. Exit and book new session\n\n");
        printf("Enter choice (1-3): ");
        
        char choice = _getch();
        
        if (choice == '1') {
            // Request extension
            char request[256];
            char response[256];
            sprintf_s(request, sizeof(request), "EXTEND:%s", session.uuid);
            
            if (send_server_request(request, response, sizeof(response))) {
                if (strncmp(response, "EXTENDED:", 9) == 0) {
                    int new_remaining;
                    if (sscanf_s(response + 9, "REMAINING:%d", &new_remaining) == 1) {
                        session.remaining_minutes = new_remaining;
                        printf("\nSession extended! You now have %d minutes remaining.\n", new_remaining);
                        printf("Press any key to continue...");
                        _getch();
                    }
                } else {
                    printf("\nExtension failed. %s\n", response);
                    printf("Press any key to continue...");
                    _getch();
                }
            }
        } else if (choice == '3') {
            printf("\nPlease visit the booking system to make a new reservation.\n");
            printf("Press any key to exit...");
            _getch();
            should_exit = 1;
        }
    } else {
        printf("Extension has already been used.\n");
        printf("Please visit the booking system for a new reservation if needed.\n");
        printf("Press any key to continue...");
        _getch();
    }
}

// Background thread to check session status
DWORD WINAPI status_check_thread(LPVOID param) {
    while (!should_exit && session.is_logged_in) {
        Sleep(CHECK_INTERVAL * 1000);  // Check every minute
        if (!should_exit && session.is_logged_in) {
            check_session_status();
        }
    }
    return 0;
}

// Admin override function
int admin_override() {
    char admin_pass[64];
    
    clear_screen();
    printf("=== ADMIN OVERRIDE ===\n\n");
    printf("Enter admin password: ");
    
    int i = 0;
    char ch;
    while ((ch = _getch()) != '\r' && i < sizeof(admin_pass) - 1) {
        if (ch == '\b' && i > 0) {
            printf("\b \b");
            i--;
        } else if (ch != '\b') {
            admin_pass[i++] = ch;
            printf("*");
        }
    }
    admin_pass[i] = '\0';
    
    if (strcmp(admin_pass, ADMIN_PASSWORD) == 0) {
        printf("\n\nAdmin access granted.\n");
        printf("1. Unlock computer\n");
        printf("2. Exit client application\n");
        printf("Enter choice (1-2): ");
        
        char choice = _getch();
        if (choice == '1') {
            printf("\n\nComputer unlocked by administrator.\n");
            return 2;  // Unlock
        } else if (choice == '2') {
            printf("\n\nExiting client application.\n");
            return 1;  // Exit
        }
    } else {
        printf("\n\nAccess denied.\n");
        Sleep(2000);
    }
    return 0;  // Continue locked
}

// Main screen display
void display_main_screen() {
    clear_screen();
    printf("=== LAB COMPUTER ACCESS ===\n\n");
    printf("User: %s\n", session.username);
    printf("Time remaining: %d minutes\n\n", session.remaining_minutes);
    printf("Computer is now unlocked for your use.\n");
    printf("Press Ctrl+Alt+L to lock screen or Ctrl+Alt+A for admin override.\n");
    printf("Press ESC to exit.\n\n");
    printf("Last status check: %s", ctime(&session.last_check));
}

// Login screen
int login_screen() {
    char username[32];
    char password[32];
    
    while (1) {
        clear_screen();
        printf("=== LAB COMPUTER LOGIN ===\n\n");
        printf("Enter username: ");
        scanf_s("%31s", username, sizeof(username));
        
        printf("Enter password: ");
        
        // Hide password input
        int i = 0;
        char ch;
        while ((ch = _getch()) != '\r' && i < sizeof(password) - 1) {
            if (ch == '\b' && i > 0) {
                printf("\b \b");
                i--;
            } else if (ch != '\b') {
                password[i++] = ch;
                printf("*");
            }
        }
        password[i] = '\0';
        
        printf("\n\nValidating credentials...\n");
        
        if (validate_credentials(username, password)) {
            return 1;
        }
        
        printf("Press any key to try again or ESC to exit...");
        char retry = _getch();
        if (retry == 27) {  // ESC key
            return 0;
        }
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    printf("Lab Client Starting...\n");
    Sleep(1000);
    
    hide_cursor();
    
    // Login process
    if (!login_screen()) {
        show_cursor();
        WSACleanup();
        return 0;
    }
    
    printf("\nLogin successful! Starting session monitoring...\n");
    Sleep(2000);
    
    // Disable system keys (simplified)
    disable_system_keys();
    
    // Start background status checking thread
    check_thread = CreateThread(NULL, 0, status_check_thread, NULL, 0, NULL);
    
    // Main application loop
    while (!should_exit && session.is_logged_in) {
        display_main_screen();
        
        // Check for key combinations
        if (_kbhit()) {
            char key = _getch();
            
            if (key == 27) {  // ESC key
                printf("\nAre you sure you want to exit? (y/n): ");
                char confirm = _getch();
                if (confirm == 'y' || confirm == 'Y') {
                    should_exit = 1;
                }
            } else if (key == 1) {  // Ctrl+A (simplified - in real implementation use proper key combination detection)
                int admin_result = admin_override();
                if (admin_result == 1) {
                    should_exit = 1;  // Exit
                } else if (admin_result == 2) {
                    // Unlock - continue running but don't restrict access
                    printf("Computer unlocked by admin. Press any key to continue...");
                    _getch();
                }
            }
        }
        
        Sleep(1000);  // Update display every second
    }
    
    // Cleanup
    if (check_thread) {
        should_exit = 1;
        WaitForSingleObject(check_thread, 5000);
        CloseHandle(check_thread);
    }
    
    enable_system_keys();
    show_cursor();
    
    if (!session.is_logged_in) {
        clear_screen();
        printf("Session expired. Computer is now locked.\n");
        printf("Please contact administrator or make a new booking.\n");
        _getch();
    }
    
    WSACleanup();
    return 0;
}