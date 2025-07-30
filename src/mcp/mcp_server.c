#include "mcp_server.h"
#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../utils/paths.h"
#include "../utils/git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MCP_VERSION "2024-11-05"
#define SERVER_NAME "fractyl-mcp"
#define SERVER_VERSION "1.0.0"

// MCP message structure
typedef struct {
    char *method;
    cJSON *params;
    cJSON *id;
    int is_notification;
} mcp_message_t;

// Send a JSON-RPC response
static void send_response(cJSON *id, cJSON *result, cJSON *error) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    
    if (id) {
        cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, 1));
    }
    
    if (error) {
        cJSON_AddItemToObject(response, "error", error);
    } else if (result) {
        cJSON_AddItemToObject(response, "result", result);
    } else {
        cJSON_AddNullToObject(response, "result");
    }
    
    char *json_string = cJSON_Print(response);
    printf("%s\n", json_string);
    fflush(stdout);
    
    free(json_string);
    cJSON_Delete(response);
}

// Send a notification (no id) - for future use
__attribute__((unused)) static void send_notification(const char *method, cJSON *params) {
    cJSON *notification = cJSON_CreateObject();
    cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notification, "method", method);
    
    if (params) {
        cJSON_AddItemToObject(notification, "params", params);
    }
    
    char *json_string = cJSON_Print(notification);
    printf("%s\n", json_string);
    fflush(stdout);
    
    free(json_string);
    cJSON_Delete(notification);
}

// Create an error response
static cJSON* create_error(int code, const char *message, cJSON *data) {
    cJSON *error = cJSON_CreateObject();
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    
    if (data) {
        cJSON_AddItemToObject(error, "data", data);
    }
    
    return error;
}

// Handle initialize method
static void handle_initialize(cJSON *id, cJSON *params) {
    (void)params; // Unused for now
    
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", MCP_VERSION);
    
    // Server info
    cJSON *server_info = cJSON_CreateObject();
    cJSON_AddStringToObject(server_info, "name", SERVER_NAME);
    cJSON_AddStringToObject(server_info, "version", SERVER_VERSION);
    cJSON_AddItemToObject(result, "serverInfo", server_info);
    
    // Capabilities
    cJSON *capabilities = cJSON_CreateObject();
    
    // Tools capability
    cJSON *tools = cJSON_CreateObject();
    cJSON_AddFalseToObject(tools, "listChanged");
    cJSON_AddItemToObject(capabilities, "tools", tools);
    
    // Resources capability
    cJSON *resources = cJSON_CreateObject();
    cJSON_AddFalseToObject(resources, "subscribe");
    cJSON_AddFalseToObject(resources, "listChanged");
    cJSON_AddItemToObject(capabilities, "resources", resources);
    
    cJSON_AddItemToObject(result, "capabilities", capabilities);
    
    send_response(id, result, NULL);
}

// Handle tools/list method
static void handle_tools_list(cJSON *id, cJSON *params) {
    (void)params; // Unused
    
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    
    // Snapshot tool
    cJSON *snapshot_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot_tool, "name", "fractyl_snapshot");
    cJSON_AddStringToObject(snapshot_tool, "description", "Create a new Fractyl snapshot");
    
    cJSON *snapshot_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot_schema, "type", "object");
    cJSON *snapshot_props = cJSON_CreateObject();
    
    cJSON *message_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(message_prop, "type", "string");
    cJSON_AddStringToObject(message_prop, "description", "Snapshot description message");
    cJSON_AddItemToObject(snapshot_props, "message", message_prop);
    
    cJSON_AddItemToObject(snapshot_schema, "properties", snapshot_props);
    cJSON_AddItemToObject(snapshot_tool, "inputSchema", snapshot_schema);
    cJSON_AddItemToArray(tools, snapshot_tool);
    
    // List tool
    cJSON *list_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(list_tool, "name", "fractyl_list");
    cJSON_AddStringToObject(list_tool, "description", "List Fractyl snapshots");
    
    cJSON *list_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(list_schema, "type", "object");
    cJSON_AddItemToObject(list_schema, "properties", cJSON_CreateObject());
    cJSON_AddItemToObject(list_tool, "inputSchema", list_schema);
    cJSON_AddItemToArray(tools, list_tool);
    
    // Restore tool
    cJSON *restore_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(restore_tool, "name", "fractyl_restore");
    cJSON_AddStringToObject(restore_tool, "description", "Restore a Fractyl snapshot");
    
    cJSON *restore_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(restore_schema, "type", "object");
    cJSON *restore_props = cJSON_CreateObject();
    
    cJSON *snapshot_id_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot_id_prop, "type", "string");
    cJSON_AddStringToObject(snapshot_id_prop, "description", "Snapshot ID or prefix to restore");
    cJSON_AddItemToObject(restore_props, "snapshot_id", snapshot_id_prop);
    
    cJSON_AddItemToObject(restore_schema, "properties", restore_props);
    cJSON *restore_required = cJSON_CreateArray();
    cJSON_AddItemToArray(restore_required, cJSON_CreateString("snapshot_id"));
    cJSON_AddItemToObject(restore_schema, "required", restore_required);
    cJSON_AddItemToObject(restore_tool, "inputSchema", restore_schema);
    cJSON_AddItemToArray(tools, restore_tool);
    
    // Diff tool
    cJSON *diff_tool = cJSON_CreateObject();
    cJSON_AddStringToObject(diff_tool, "name", "fractyl_diff");
    cJSON_AddStringToObject(diff_tool, "description", "Compare two Fractyl snapshots");
    
    cJSON *diff_schema = cJSON_CreateObject();
    cJSON_AddStringToObject(diff_schema, "type", "object");
    cJSON *diff_props = cJSON_CreateObject();
    
    cJSON *snapshot_a_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot_a_prop, "type", "string");
    cJSON_AddStringToObject(snapshot_a_prop, "description", "First snapshot ID or prefix");
    cJSON_AddItemToObject(diff_props, "snapshot_a", snapshot_a_prop);
    
    cJSON *snapshot_b_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot_b_prop, "type", "string");
    cJSON_AddStringToObject(snapshot_b_prop, "description", "Second snapshot ID or prefix");
    cJSON_AddItemToObject(diff_props, "snapshot_b", snapshot_b_prop);
    
    cJSON_AddItemToObject(diff_schema, "properties", diff_props);
    cJSON *diff_required = cJSON_CreateArray();
    cJSON_AddItemToArray(diff_required, cJSON_CreateString("snapshot_a"));
    cJSON_AddItemToArray(diff_required, cJSON_CreateString("snapshot_b"));
    cJSON_AddItemToObject(diff_schema, "required", diff_required);
    cJSON_AddItemToObject(diff_tool, "inputSchema", diff_schema);
    cJSON_AddItemToArray(tools, diff_tool);
    
    cJSON_AddItemToObject(result, "tools", tools);
    send_response(id, result, NULL);
}

// Execute a tool call and capture output
static char* execute_tool_capture_output(const char *tool_name, const char **args, int arg_count) {
    // Create a pipe to capture output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return strdup("Error: Failed to create pipe");
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return strdup("Error: Failed to fork process");
    }
    
    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        dup2(pipefd[1], STDERR_FILENO); // Redirect stderr to pipe
        close(pipefd[1]);
        
        // Build argument array
        char **argv = malloc((arg_count + 2) * sizeof(char*));
        argv[0] = (char*)tool_name;
        for (int i = 0; i < arg_count; i++) {
            argv[i + 1] = (char*)args[i];
        }
        argv[arg_count + 1] = NULL;
        
        execv(tool_name, argv);
        exit(1); // If execv fails
    } else {
        // Parent process
        close(pipefd[1]); // Close write end
        
        // Read output
        char *output = malloc(8192);
        size_t total_read = 0;
        ssize_t bytes_read;
        
        while ((bytes_read = read(pipefd[0], output + total_read, 8192 - total_read - 1)) > 0) {
            total_read += bytes_read;
            if (total_read >= 8191) break; // Leave space for null terminator
        }
        
        output[total_read] = '\0';
        close(pipefd[0]);
        
        // Wait for child to finish
        int status;
        waitpid(pid, &status, 0);
        
        return output;
    }
}

// Handle tools/call method
static void handle_tools_call(cJSON *id, cJSON *params) {
    cJSON *name = cJSON_GetObjectItem(params, "name");
    cJSON *arguments = cJSON_GetObjectItem(params, "arguments");
    
    if (!cJSON_IsString(name)) {
        cJSON *error = create_error(-32602, "Invalid params: name is required", NULL);
        send_response(id, NULL, error);
        return;
    }
    
    const char *tool_name = name->valuestring;
    char *output = NULL;
    
    if (strcmp(tool_name, "fractyl_snapshot") == 0) {
        // Handle snapshot command
        cJSON *message = cJSON_GetObjectItem(arguments, "message");
        const char *msg = cJSON_IsString(message) ? message->valuestring : "MCP snapshot";
        
        const char *args[] = {"snapshot", "-m", msg};
        output = execute_tool_capture_output("./frac", args, 3);
        
    } else if (strcmp(tool_name, "fractyl_list") == 0) {
        // Handle list command
        const char *args[] = {"list"};
        output = execute_tool_capture_output("./frac", args, 1);
        
    } else if (strcmp(tool_name, "fractyl_restore") == 0) {
        // Handle restore command
        cJSON *snapshot_id = cJSON_GetObjectItem(arguments, "snapshot_id");
        if (!cJSON_IsString(snapshot_id)) {
            cJSON *error = create_error(-32602, "Invalid params: snapshot_id is required", NULL);
            send_response(id, NULL, error);
            return;
        }
        
        const char *args[] = {"restore", snapshot_id->valuestring};
        output = execute_tool_capture_output("./frac", args, 2);
        
    } else if (strcmp(tool_name, "fractyl_diff") == 0) {
        // Handle diff command
        cJSON *snapshot_a = cJSON_GetObjectItem(arguments, "snapshot_a");
        cJSON *snapshot_b = cJSON_GetObjectItem(arguments, "snapshot_b");
        
        if (!cJSON_IsString(snapshot_a) || !cJSON_IsString(snapshot_b)) {
            cJSON *error = create_error(-32602, "Invalid params: snapshot_a and snapshot_b are required", NULL);
            send_response(id, NULL, error);
            return;
        }
        
        const char *args[] = {"diff", snapshot_a->valuestring, snapshot_b->valuestring};
        output = execute_tool_capture_output("./frac", args, 3);
        
    } else {
        cJSON *error = create_error(-32601, "Method not found", NULL);
        send_response(id, NULL, error);
        return;
    }
    
    // Create response
    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    
    cJSON *text_content = cJSON_CreateObject();
    cJSON_AddStringToObject(text_content, "type", "text");
    cJSON_AddStringToObject(text_content, "text", output ? output : "");
    cJSON_AddItemToArray(content, text_content);
    
    cJSON_AddItemToObject(result, "content", content);
    cJSON_AddFalseToObject(result, "isError");
    
    send_response(id, result, NULL);
    
    free(output);
}

// Handle resources/list method
static void handle_resources_list(cJSON *id, cJSON *params) {
    (void)params; // Unused
    
    cJSON *result = cJSON_CreateObject();
    cJSON *resources = cJSON_CreateArray();
    
    // Snapshot history resource
    cJSON *history_resource = cJSON_CreateObject();
    cJSON_AddStringToObject(history_resource, "uri", "fractyl://snapshots/history");
    cJSON_AddStringToObject(history_resource, "name", "Snapshot History");
    cJSON_AddStringToObject(history_resource, "description", "Complete history of Fractyl snapshots");
    cJSON_AddStringToObject(history_resource, "mimeType", "application/json");
    cJSON_AddItemToArray(resources, history_resource);
    
    cJSON_AddItemToObject(result, "resources", resources);
    send_response(id, result, NULL);
}

// Parse incoming JSON-RPC message
static mcp_message_t* parse_message(const char *json_str) {
    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        return NULL;
    }
    
    mcp_message_t *msg = malloc(sizeof(mcp_message_t));
    memset(msg, 0, sizeof(mcp_message_t));
    
    cJSON *method = cJSON_GetObjectItem(json, "method");
    if (cJSON_IsString(method)) {
        msg->method = strdup(method->valuestring);
    }
    
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (params) {
        msg->params = cJSON_Duplicate(params, 1);
    }
    
    cJSON *id = cJSON_GetObjectItem(json, "id");
    if (id) {
        msg->id = cJSON_Duplicate(id, 1);
        msg->is_notification = 0;
    } else {
        msg->is_notification = 1;
    }
    
    cJSON_Delete(json);
    return msg;
}

// Free message structure
static void free_message(mcp_message_t *msg) {
    if (!msg) return;
    
    free(msg->method);
    if (msg->params) cJSON_Delete(msg->params);
    if (msg->id) cJSON_Delete(msg->id);
    free(msg);
}

// Main MCP server loop
int mcp_server_run(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    
    // Send server ready notification (optional, but only in debug mode)
    #ifdef DEBUG
    fprintf(stderr, "Fractyl MCP server starting...\n");
    #endif
    
    while ((read = getline(&line, &len, stdin)) != -1) {
        // Remove trailing newline
        if (read > 0 && line[read-1] == '\n') {
            line[read-1] = '\0';
        }
        
        mcp_message_t *msg = parse_message(line);
        if (!msg || !msg->method) {
            if (msg && msg->id) {
                cJSON *error = create_error(-32700, "Parse error", NULL);
                send_response(msg->id, NULL, error);
            }
            free_message(msg);
            continue;
        }
        
        // Route message to appropriate handler
        if (strcmp(msg->method, "initialize") == 0) {
            handle_initialize(msg->id, msg->params);
        } else if (strcmp(msg->method, "tools/list") == 0) {
            handle_tools_list(msg->id, msg->params);
        } else if (strcmp(msg->method, "tools/call") == 0) {
            handle_tools_call(msg->id, msg->params);
        } else if (strcmp(msg->method, "resources/list") == 0) {
            handle_resources_list(msg->id, msg->params);
        } else {
            // Unknown method
            if (!msg->is_notification) {
                cJSON *error = create_error(-32601, "Method not found", NULL);
                send_response(msg->id, NULL, error);
            }
        }
        
        free_message(msg);
    }
    
    if (line) {
        free(line);
    }
    
    return 0;
}