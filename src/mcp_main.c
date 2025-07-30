#include "mcp/mcp_server.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // Run the MCP server
    return mcp_server_run();
}