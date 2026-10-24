// SPDX-License-Identifier: GPL-2.0-or-later
// A private Windows named-pipe peer. Run under Wine's isolated test prefix.
#define NOMINMAX
#include <windows.h>
#include "discordrpc.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static void readAll(HANDLE pipe,void *buffer,DWORD count)
{
 auto *out=(char*)buffer;
 while(count){DWORD n=0;assert(ReadFile(pipe,out,count,&n,nullptr)&&n);out+=n;count-=n;}
}
static void writeAll(HANDLE pipe,const std::string &bytes)
{
 size_t at=0;
 while(at<bytes.size()){
  DWORD n=0;assert(WriteFile(pipe,bytes.data()+at,(DWORD)(bytes.size()-at),&n,nullptr)&&n);at+=n;
 }
}
static std::string receive(HANDLE pipe,unsigned expected)
{
 unsigned op=0,count=0;readAll(pipe,&op,4);readAll(pipe,&count,4);assert(op==expected&&count<65536);
 std::string bytes(count,'\0');if(count)readAll(pipe,&bytes[0],count);return bytes;
}
static void send(HANDLE pipe,unsigned op,const std::string &bytes,bool fragmented=false)
{
 unsigned count=(unsigned)bytes.size();std::string wire((char*)&op,4);wire.append((char*)&count,4);wire+=bytes;
 if(fragmented){writeAll(pipe,wire.substr(0,3));Sleep(10);writeAll(pipe,wire.substr(3,4));Sleep(10);writeAll(pipe,wire.substr(7));}
 else writeAll(pipe,wire);
}
static void wait(std::atomic<int> &phase,int wanted)
{
 auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(phase<wanted&&std::chrono::steady_clock::now()<until)Sleep(10);
 assert(phase>=wanted);
}
int main()
{
 // A watchdog bounds even a regression that leaves the peer in ReadFile.
 std::thread([]{Sleep(12000);ExitProcess(99);}).detach();
 std::string name="\\\\?\\pipe\\haumea-presence-check-"+std::to_string(GetCurrentProcessId());
 HANDLE pipe=CreateNamedPipeA(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_WAIT,1,65536,65536,0,nullptr);
 assert(pipe!=INVALID_HANDLE_VALUE);
 std::atomic<int> phase{0};
 std::thread peer([&]{
  assert(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED);
  auto hello=receive(pipe,0);
  assert(hello.find("\"client_id\":\"1482423448121643200\"")!=std::string::npos);
  send(pipe,1,"{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}",true);
  auto map=receive(pipe,1);assert(map.find("\"state\":\"Map\"")!=std::string::npos);
  assert(map.find("\"pid\":"+std::to_string(GetCurrentProcessId()))!=std::string::npos);
  send(pipe,3,"keepalive");assert(receive(pipe,4)=="keepalive");phase=1;
  auto code=receive(pipe,1);assert(code.find("\"state\":\"Code\"")!=std::string::npos);phase=2;
  auto clear=receive(pipe,1);assert(clear.find("\"activity\":null")!=std::string::npos);phase=3;
  FlushFileBuffers(pipe);DisconnectNamedPipe(pipe);CloseHandle(pipe);
 });
 DiscordRPC::Client client({name},std::chrono::milliseconds(50),std::chrono::milliseconds(100));
 client.Update(true,DiscordRPC::Workspace::Map);
 wait(phase,1);client.Update(true,DiscordRPC::Workspace::Code);
 wait(phase,2);client.Update(false,DiscordRPC::Workspace::Code);
 wait(phase,3);client.Shutdown();peer.join();
 std::cout<<"Discord Windows IPC: handshake, fragmented READY, activity, ping/pong and clear passed\n";
}
