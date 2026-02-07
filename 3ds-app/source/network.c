#include "network.h"
#include "cJSON.h"
#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define RECV_BUF_SIZE 4096
#define SEND_BUF_SIZE 1024

static int sock = -1;
static bool connected = false;
static bool ws_handshake_done = false;
static char recv_buf[RECV_BUF_SIZE];
static int recv_buf_len = 0;

// Simple WebSocket key (fixed for simplicity)
static const char* WS_KEY = "dGhlIHNhbXBsZSBub25jZQ==";

bool network_init(void) {
    // SOC service is needed for sockets on 3DS
    static u32* SOC_buffer = NULL;
    if (SOC_buffer == NULL) {
        SOC_buffer = (u32*)memalign(0x1000, 0x100000);
        if (SOC_buffer == NULL) {
            return false;
        }
        if (socInit(SOC_buffer, 0x100000) != 0) {
            free(SOC_buffer);
            SOC_buffer = NULL;
            return false;
        }
    }
    return true;
}

void network_exit(void) {
    network_disconnect();
    socExit();
}

bool network_connect(const char* host, int port) {
    if (sock >= 0) {
        network_disconnect();
    }

    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        printf("Failed to resolve host: %s\n", host);
        return false;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket\n");
        return false;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect\n");
        close(sock);
        sock = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // Send WebSocket handshake
    char handshake[512];
    snprintf(handshake, sizeof(handshake),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        host, port, WS_KEY);

    send(sock, handshake, strlen(handshake), 0);

    connected = true;
    ws_handshake_done = false;
    recv_buf_len = 0;

    return true;
}

void network_disconnect(void) {
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    connected = false;
    ws_handshake_done = false;
}

bool network_is_connected(void) {
    return connected && ws_handshake_done;
}

static void parse_agent_status(const char* json, Agent* agents, int* agent_count) {
    cJSON* root = cJSON_Parse(json);
    if (root == NULL) return;

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || strcmp(type->valuestring, "agent_status") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON* agent_name = cJSON_GetObjectItem(root, "agent");
    cJSON* state = cJSON_GetObjectItem(root, "state");
    cJSON* progress = cJSON_GetObjectItem(root, "progress");
    cJSON* message = cJSON_GetObjectItem(root, "message");
    cJSON* pending = cJSON_GetObjectItem(root, "pendingCommand");

    if (agent_name == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Find or create agent slot
    int idx = -1;
    for (int i = 0; i < *agent_count; i++) {
        if (strcasecmp(agents[i].name, agent_name->valuestring) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && *agent_count < MAX_AGENTS) {
        idx = (*agent_count)++;
        strncpy(agents[idx].name, agent_name->valuestring, sizeof(agents[idx].name) - 1);
    }
    if (idx < 0) {
        cJSON_Delete(root);
        return;
    }

    // Update agent
    if (state) {
        const char* s = state->valuestring;
        if (strcmp(s, "working") == 0) agents[idx].state = STATE_WORKING;
        else if (strcmp(s, "waiting") == 0) agents[idx].state = STATE_WAITING;
        else if (strcmp(s, "error") == 0) agents[idx].state = STATE_ERROR;
        else if (strcmp(s, "done") == 0) agents[idx].state = STATE_DONE;
        else agents[idx].state = STATE_IDLE;
    }
    if (progress) agents[idx].progress = progress->valueint;
    if (message) strncpy(agents[idx].message, message->valuestring, sizeof(agents[idx].message) - 1);
    if (pending && pending->valuestring) {
        strncpy(agents[idx].pending_command, pending->valuestring, sizeof(agents[idx].pending_command) - 1);
    } else {
        agents[idx].pending_command[0] = '\0';
    }

    cJSON* context = cJSON_GetObjectItem(root, "contextPercent");
    agents[idx].context_percent = (context && cJSON_IsNumber(context)) ? context->valueint : 0;

    // Parse prompt fields
    cJSON* promptToolType = cJSON_GetObjectItem(root, "promptToolType");
    cJSON* promptToolDetail = cJSON_GetObjectItem(root, "promptToolDetail");
    cJSON* promptDescription = cJSON_GetObjectItem(root, "promptDescription");

    if (promptToolType && cJSON_IsString(promptToolType) && promptToolType->valuestring[0] != '\0') {
        agents[idx].prompt_visible = true;
        strncpy(agents[idx].prompt_tool_type, promptToolType->valuestring, sizeof(agents[idx].prompt_tool_type) - 1);
        agents[idx].prompt_tool_type[sizeof(agents[idx].prompt_tool_type) - 1] = '\0';
    } else {
        agents[idx].prompt_visible = false;
        agents[idx].prompt_tool_type[0] = '\0';
    }

    if (promptToolDetail && cJSON_IsString(promptToolDetail)) {
        strncpy(agents[idx].prompt_tool_detail, promptToolDetail->valuestring, sizeof(agents[idx].prompt_tool_detail) - 1);
        agents[idx].prompt_tool_detail[sizeof(agents[idx].prompt_tool_detail) - 1] = '\0';
    } else {
        agents[idx].prompt_tool_detail[0] = '\0';
    }

    if (promptDescription && cJSON_IsString(promptDescription)) {
        strncpy(agents[idx].prompt_description, promptDescription->valuestring, sizeof(agents[idx].prompt_description) - 1);
        agents[idx].prompt_description[sizeof(agents[idx].prompt_description) - 1] = '\0';
    } else {
        agents[idx].prompt_description[0] = '\0';
    }

    cJSON_Delete(root);
}

static void process_ws_frame(const unsigned char* data, int len, Agent* agents, int* agent_count) {
    if (len < 2) return;

    // Simple WebSocket frame parsing (assumes small, unfragmented, text frames)
    int opcode = data[0] & 0x0F;
    int payload_len = data[1] & 0x7F;
    int offset = 2;

    if (payload_len == 126) {
        if (len < 4) return;
        payload_len = (data[2] << 8) | data[3];
        offset = 4;
    }

    if (opcode == 0x01 && offset + payload_len <= len) {  // Text frame
        char json[RECV_BUF_SIZE];
        memcpy(json, data + offset, payload_len);
        json[payload_len] = '\0';
        parse_agent_status(json, agents, agent_count);
    }
}

void network_poll(Agent* agents, int* agent_count) {
    if (sock < 0) return;

    // Try to receive data
    int space = RECV_BUF_SIZE - recv_buf_len - 1;
    if (space > 0) {
        int n = recv(sock, recv_buf + recv_buf_len, space, 0);
        if (n > 0) {
            recv_buf_len += n;
            recv_buf[recv_buf_len] = '\0';
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Connection closed or error
            connected = false;
            return;
        }
    }

    // Check for WebSocket handshake response
    if (!ws_handshake_done) {
        char* end = strstr(recv_buf, "\r\n\r\n");
        if (end) {
            if (strstr(recv_buf, "101") != NULL) {
                ws_handshake_done = true;
                int handshake_len = (end - recv_buf) + 4;
                memmove(recv_buf, recv_buf + handshake_len, recv_buf_len - handshake_len);
                recv_buf_len -= handshake_len;
            } else {
                // Handshake failed
                network_disconnect();
                return;
            }
        }
        return;
    }

    // Process WebSocket frames
    while (recv_buf_len >= 2) {
        int payload_len = recv_buf[1] & 0x7F;
        int header_len = 2;
        if (payload_len == 126) header_len = 4;

        if (recv_buf_len < header_len) break;
        if (payload_len == 126) {
            payload_len = ((unsigned char)recv_buf[2] << 8) | (unsigned char)recv_buf[3];
        }

        int frame_len = header_len + payload_len;
        if (recv_buf_len < frame_len) break;

        process_ws_frame((unsigned char*)recv_buf, frame_len, agents, agent_count);

        memmove(recv_buf, recv_buf + frame_len, recv_buf_len - frame_len);
        recv_buf_len -= frame_len;
    }
}

static void send_ws_frame(const char* data) {
    if (sock < 0 || !ws_handshake_done) return;

    int len = strlen(data);
    unsigned char frame[SEND_BUF_SIZE];
    int offset = 0;

    frame[offset++] = 0x81;  // FIN + text opcode

    // Mask bit set (required from client), followed by length
    if (len < 126) {
        frame[offset++] = 0x80 | len;
    } else {
        frame[offset++] = 0x80 | 126;
        frame[offset++] = (len >> 8) & 0xFF;
        frame[offset++] = len & 0xFF;
    }

    // Masking key (just use zeros for simplicity, though spec says random)
    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    memcpy(frame + offset, mask, 4);
    offset += 4;

    // Masked payload
    for (int i = 0; i < len; i++) {
        frame[offset++] = data[i] ^ mask[i % 4];
    }

    send(sock, frame, offset, 0);
}

void network_send_action(const char* agent, const char* action) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"type\":\"action\",\"agent\":\"%s\",\"action\":\"%s\"}",
        agent, action);
    send_ws_frame(json);
}

void network_send_command(const char* agent, const char* command) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"type\":\"command\",\"agent\":\"%s\",\"command\":\"%s\"}",
        agent, command);
    send_ws_frame(json);
}
