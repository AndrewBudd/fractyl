#ifndef FRACTYL_MCP_SERVER_H
#define FRACTYL_MCP_SERVER_H

#include <cJSON.h>
#include <sys/wait.h>

// Main MCP server loop
int mcp_server_run(void);

#endif // FRACTYL_MCP_SERVER_H