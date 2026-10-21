#!/usr/bin/env python3
"""Check an installed Namaka bundle: its MCP server starts and lists the expected tools.

    .venv-agent/bin/python check_agent_bundle.py <haumea-mcp> --tools N

Run with the bundle's own Python, which has mcp. No editor is needed: the
server lists its tools before it connects to one.
"""
import argparse
import asyncio

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def list_tools(command):
    # Only the server's own environment: nothing of the runner's leaks in.
    server = StdioServerParameters(command=command, args=[], env={'PATH': '/usr/bin:/bin'})
    async with stdio_client(server) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            return sorted(tool.name for tool in (await session.list_tools()).tools)


parser = argparse.ArgumentParser()
parser.add_argument('mcp', help='the bundle\'s haumea-mcp command')
parser.add_argument('--tools', type=int, required=True, help='expected number of MCP tools')
args = parser.parse_args()
names = asyncio.run(list_tools(args.mcp))
print(f'{len(names)} MCP tools: ' + ', '.join(names))
if len(names) != args.tools:
    raise SystemExit(f'expected {args.tools} MCP tools, got {len(names)}: '
                     'update AGENT_MCP_TOOLS in .github/workflows/build-haumea.yml '
                     'if a tool was added or removed on purpose')
