# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real Discord IPC framing against private peers, never the user's Discord."""
from pathlib import Path
import json
import os
import queue
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[3]
DRIVER=r'''
#include "discordrpc.h"
#include <iostream>
#include <string>
int main(int argc,char **argv){
 using namespace DiscordRPC;
 if(std::string(argv[1])=="--endpoints"){for(const auto &path:Endpoints())std::cout<<path<<std::endl;return 0;}
 Client client({argv[1]},std::chrono::milliseconds(100),std::chrono::milliseconds(700));
 client.Update(true,Workspace::Map);
 std::cout<<"ready"<<std::endl;
 std::string command;
 while(std::getline(std::cin,command)){
  if(command=="quit")break;
  if(command=="off")client.Update(false,Workspace::Map);
  if(command=="map")client.Update(true,Workspace::Map);
  if(command=="td")client.Update(true,Workspace::TextDraws);
  if(command=="code")client.Update(true,Workspace::Code);
  if(command=="cycle"){client.Update(true,Workspace::TextDraws);client.Update(true,Workspace::Code);}
  if(command=="repeat")for(int i=0;i<10000;i++)client.Update(true,Workspace::Map);
  std::cout<<"ok"<<std::endl;
 }
 client.Shutdown();
}
'''


class Peer:
    def __init__(self,path,ready=True):
        self.ready=ready;self.path=path;self.events=queue.Queue();self.lock=threading.Lock()
        self.stop=threading.Event();self.connection=None;self.errors=[]
        self.listener=socket.socket(socket.AF_UNIX)
        self.listener.bind(str(path));self.listener.listen(2);self.listener.settimeout(.1)
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()

    def send(self,opcode,payload,fragment=False):
        body=json.dumps(payload).encode() if isinstance(payload,dict) else payload
        wire=struct.pack('<II',opcode,len(body))+body
        with self.lock:
            if fragment:
                for part in (wire[:3],wire[3:7],wire[7:12],wire[12:]):
                    self.connection.sendall(part);time.sleep(.01)
            else:self.connection.sendall(wire)

    def run(self):
        while not self.stop.is_set():
            try:
                connection,_=self.listener.accept();connection.settimeout(.1)
            except socket.timeout:continue
            except OSError:break
            self.connection=connection;buffer=b''
            try:
                while not self.stop.is_set():
                    try:data=connection.recv(4096)
                    except socket.timeout:continue
                    if not data:break
                    buffer+=data
                    while len(buffer)>=8:
                        opcode,length=struct.unpack('<II',buffer[:8])
                        if len(buffer)<8+length:break
                        body=buffer[8:8+length];buffer=buffer[8+length:]
                        payload=json.loads(body) if opcode in (0,1) else body
                        self.events.put((opcode,payload,time.monotonic()))
                        if opcode==0 and self.ready:
                            self.send(1,{'cmd':'DISPATCH','data':{'evt':'ERROR','user':{'username':'private'}},'evt':'READY'},True)
                        elif opcode==1:
                            self.send(1,{'cmd':'SET_ACTIVITY','evt':None,'nonce':payload['nonce']})
            except (BrokenPipeError,ConnectionResetError,OSError):pass
            except Exception as error:self.errors.append(error)
            finally:
                connection.close();self.connection=None

    def event(self,opcode,timeout=3):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            event=self.events.get(timeout=max(.01,until-time.monotonic()))
            if event[0]==opcode:return event
        raise AssertionError('no frame with opcode '+str(opcode))

    def drop(self):
        with self.lock:
            if self.connection:self.connection.shutdown(socket.SHUT_RDWR)

    def close(self):
        self.stop.set();self.listener.close()
        try:self.drop()
        except OSError:pass
        self.thread.join(2)


@unittest.skipUnless(shutil.which('g++') and hasattr(socket,'AF_UNIX') and sys.platform!='win32','needs g++ and Unix IPC')
class DiscordRPCTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='haumea-discord-build-')
        source=Path(cls.build.name,'driver.cpp');source.write_text(DRIVER)
        cls.binary=Path(cls.build.name,'driver')
        subprocess.run(['g++','-std=c++17','-pthread','-I',str(ROOT/'tools/haumea'),str(source),
                        str(ROOT/'tools/haumea/discordrpc.cpp'),'-o',str(cls.binary)],check=True,capture_output=True)

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def setUp(self):
        # AF_UNIX limits are 104 bytes on macOS; TMPDIR can be much longer.
        self.folder=tempfile.TemporaryDirectory(prefix='hdc-',dir='/tmp')
        self.addCleanup(self.folder.cleanup);self.path=Path(self.folder.name,'d.sock')

    def start(self,peer=True,ready=True):
        self.peer=Peer(self.path,ready) if peer else None
        if self.peer:self.addCleanup(self.peer.close)
        self.process=subprocess.Popen([str(self.binary),str(self.path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,text=True)
        self.addCleanup(self.finish)
        self.assertEqual(self.line(),'ready')

    def line(self):
        self.assertTrue(select.select([self.process.stdout],[],[],3)[0],'client/main thread stopped responding')
        return self.process.stdout.readline().strip()

    def command(self,command):
        self.process.stdin.write(command+'\n');self.process.stdin.flush()
        self.assertEqual(self.line(),'ok')

    def finish(self):
        if self.process.poll() is None:
            self.process.stdin.write('quit\n');self.process.stdin.flush()
            try:self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill();self.process.wait();self.fail('shutdown did not finish')
        error=self.process.stderr.read()
        self.assertEqual(self.process.returncode,0,error)
        for stream in (self.process.stdin,self.process.stdout,self.process.stderr):stream.close()

    def activity(self):return self.peer.event(1)[1]['args']['activity']

    def test_handshake_fragmented_ready_workspace_and_clear(self):
        self.start()
        handshake=self.peer.event(0)[1]
        self.assertEqual(handshake,{'v':1,'client_id':'1482423448121643200'})
        first=self.activity()
        self.assertEqual(first['state'],'Map');self.assertEqual(first['details'],'Editing a map')
        self.assertEqual(set(first),{'type','state','details','timestamps','instance'})
        self.assertLess(abs(first['timestamps']['start']-time.time()),10)
        self.command('td');second=self.activity()
        self.assertEqual(second['state'],'TextDraws')
        self.command('code');third=self.activity()
        self.assertEqual(third['state'],'Code');self.assertEqual(third['timestamps'],first['timestamps'])
        self.command('off');self.assertIsNone(self.activity())

    def test_no_flood_and_latest_workspace_wins(self):
        self.start();self.activity();self.command('repeat')
        with self.assertRaises(queue.Empty):self.peer.events.get(timeout=.15)
        self.command('cycle');value=self.activity()
        self.assertEqual(value['state'],'Code')
        with self.assertRaises(queue.Empty):self.peer.events.get(timeout=.15)

    def test_ping_and_reconnect(self):
        self.start();first=self.activity()
        self.peer.send(3,b'keepalive\x00')
        self.assertEqual(self.peer.event(4)[1],b'keepalive\x00')
        self.command('code');self.peer.drop()
        self.peer.event(0)
        second=self.activity()
        self.assertEqual(second['state'],'Code');self.assertEqual(second['timestamps'],first['timestamps'])

    def test_invalid_frame_and_server_error_are_recoverable(self):
        self.start();self.activity()
        with self.peer.lock:self.peer.connection.sendall(struct.pack('<II',1,65537))
        self.peer.event(0);self.assertEqual(self.activity()['state'],'Map')
        self.peer.send(1,{'cmd':'SET_ACTIVITY','evt':'ERROR','data':{'code':4000,'message':'error'}})
        self.peer.event(0);self.assertEqual(self.activity()['state'],'Map')

    def test_no_activity_before_ready_and_fast_shutdown(self):
        self.start(ready=False);self.peer.event(0)
        self.peer.send(1,{'cmd':'DISPATCH','data':{'evt':'READY'},'evt':'NOT_READY'})
        self.peer.send(3,b'ping')
        self.assertEqual(self.peer.event(4)[1],b'ping')
        with self.assertRaises(queue.Empty):self.peer.events.get(timeout=.2)
        self.command('off')

    def test_discord_absent_does_not_block_updates_or_exit(self):
        self.start(peer=False)
        start=time.monotonic()
        for command in ('td','off','map','code'):self.command(command)
        self.assertLess(time.monotonic()-start,.5)

    def test_shutdown_clears_and_toggle_reconnects(self):
        self.start();first=self.activity()
        self.command('off');self.assertIsNone(self.activity())
        self.command('code');self.peer.event(0)
        next_activity=self.activity()
        self.assertEqual(next_activity['state'],'Code')
        self.assertEqual(next_activity['timestamps'],first['timestamps'])
        self.process.stdin.write('quit\n');self.process.stdin.flush()
        self.process.wait(timeout=2)
        self.assertIsNone(self.activity())

    def test_discovery_includes_native_and_sandboxed_clients(self):
        base=str(Path(self.folder.name))
        env=dict(os.environ,XDG_RUNTIME_DIR=base+'/',TMPDIR=base,TMP=base+'/tmp',TEMP=base+'/temp')
        paths=subprocess.check_output([str(self.binary),'--endpoints'],env=env,text=True).splitlines()
        self.assertEqual(len(paths),len(set(paths)))
        for directory in (base,base+'/tmp',base+'/temp',base+'/app/com.discordapp.Discord',base+'/snap.discord','/tmp'):
            for n in range(10):self.assertIn(directory+'/discord-ipc-'+str(n),paths)


# Settings persist through a real editor restart, with Discord disabled by
# both the automated-run guard and the isolated test environment.
HERE=Path(__file__).resolve().parent
GAME=os.environ.get('HAUMEA_GAME_DIR')
BINARY=os.environ.get('HAUMEA_BINARY') or next(iter(sorted(ROOT.glob('bin/*/Release/haumea'))),None)


@unittest.skipUnless(GAME and BINARY and (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')),
                     'needs the editor, game and private display')
class DiscordPreferenceTests(unittest.TestCase):
    def test_toggle_survives_restart(self):
        from haumea_test_env import editor_env,run_editor
        with tempfile.TemporaryDirectory(prefix='haumea-discord-ui-') as tmp:
            folder=Path(tmp);env=editor_env(tmp)
            env['HAUMEA_CONFIG_DIR']=str(folder/'config')
            script=folder/'toggle.uis'
            body="shortcut window_preferences\nwait 5\nclick_item 'Preferences' 'Privacy'\nwait 3\nclick_item 'Preferences' 'Show activity on Discord'\nwait 5\n"
            for expected in (0,1):
                script.write_text(body+f'assert file_contains "$UI_CONFIG_DIR/savesettings.txt" "discord_presence {expected}"\n')
                result=run_editor([str(BINARY),'--game',GAME,'--ui-script',str(script)],cwd=folder,env=env,
                                  capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout[-3000:]+result.stderr[-1000:])
                self.assertIn(f'discord_presence {expected}',(folder/'config/savesettings.txt').read_text())
