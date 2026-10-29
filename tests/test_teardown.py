#!/usr/bin/env python3
"""Compile the actual teardown method with firmware calls replaced by a trace."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'src/itlmt7922_m3.cpp').read_text()
start = source.index('void itlmt7922::m3Teardown()')
brace = source.index('{',start)
depth=1; end=brace+1
while depth:
    if source[end]=='{': depth+=1
    if source[end]=='}': depth-=1
    end+=1
method=source[start:end]
stub=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <string>
void IOSleep(int) {}
void m3Hdr(uint8_t*,int,const uint8_t*,const uint8_t*) {}
void put16(uint8_t*,int) {}
class itlmt7922 {
public:
 bool m3linkUp=true,m3wantEapol=true,m3SessionActive=true,m3dataOn=true;
 bool firmwareFault=false,m3StaPresent=true,m3SetupRecoveryUsed=false,recoverWanted=false,stop=false;
 int m3cipher=4,staRC=0,bssRC=0,rocRC=0;
 uint8_t tgtBssid[6]={},mac[6]={},m3ptk[80];
 std::vector<std::string> trace;
 struct KeyState {bool cleared=false;void reset(){cleared=true;}} epRekey;
 itlmt7922(){memset(m3ptk,0xa5,sizeof(m3ptk));}
 bool shouldStop(){return stop;}
 void ethLink(bool up){assert(!up);trace.push_back("link-down");}
 void baReset(){trace.push_back("ba-reset");}
 void m3Tx(const uint8_t*,int,int,int){trace.push_back("deauth");}
 void m3Pump(int){trace.push_back("drain");}
 int m3Roc(bool on){assert(!on);trace.push_back("roc-release");return rocRC;}
 int m3StaRemove(){trace.push_back("sta-remove");return staRC;}
 int m3Bss(bool on){assert(!on);trace.push_back("bss-down");return bssRC;}
 void evLog(const char*,int,int,int){}
 void m3Teardown();
};
'''
checks=r'''
int main(){
 itlmt7922 d; d.m3Teardown();
 const std::vector<std::string> expected={"link-down","ba-reset","deauth","drain","roc-release","sta-remove","bss-down"};
 assert(d.trace==expected);
 assert(!d.m3linkUp && !d.m3StaPresent && !d.m3SessionActive && !d.m3dataOn && !d.m3wantEapol && !d.recoverWanted);
 assert(d.epRekey.cleared);
 for(auto x:d.m3ptk) assert(x==0);
 d.trace.clear(); d.m3Teardown(); assert(d.trace==std::vector<std::string>{"link-down"});
 for(int kind=0;kind<3;++kind){
   itlmt7922 e;
   if(kind==0)e.staRC=65539; if(kind==1)e.rocRC=-5; if(kind==2)e.bssRC=-110;
   e.m3Teardown(); assert(e.recoverWanted && e.m3SetupRecoveryUsed);
   e.recoverWanted=false; e.m3SessionActive=true; e.m3Teardown();
   assert(!e.recoverWanted); // no infinite reset request after cleanup keeps failing
 }
 itlmt7922 partial;partial.m3StaPresent=false;partial.m3Teardown();
 for(auto x:partial.trace)assert(x!="sta-remove");
 for(int kind=0;kind<2;++kind){
   itlmt7922 e; if(kind)e.stop=true;else e.firmwareFault=true;e.m3Teardown();
   assert(e.trace==std::vector<std::string>{"link-down"});assert(!e.m3dataOn);
 }
 puts("actual teardown: command order, removal, idempotence, partial setup, recovery bounds and stop/fault gates passed");
}
'''
with tempfile.TemporaryDirectory(prefix='mt7922-teardown-') as td:
    cpp=Path(td)/'test.cpp'; exe=Path(td)/'test'
    cpp.write_text(stub+method+checks)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
