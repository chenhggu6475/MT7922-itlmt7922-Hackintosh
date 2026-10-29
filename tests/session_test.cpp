// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../src/itl_session.h"
#include <assert.h>
#include <stdio.h>
#include <string>

int main()
{
    const uint8_t bssid[] = {2,3,4,5,6,7};
    uint8_t message[44]; memset(message, 0xa5, sizeof(message));
    assert(!itl_sta_remove_message(message, 39, bssid));
    assert(message[0] == 0xa5);
    assert(!itl_sta_remove_message(nullptr, 40, bssid));
    assert(!itl_sta_remove_message(message, 40, nullptr));
    assert(itl_sta_remove_message(message, sizeof(message), bssid) == 40);
    // Full golden firmware request: BASIC disconnect + empty WTBL reset.
    const uint8_t expected[40] = {
        0,1,2,0,1,0,0,0,
        0,0,20,0,2,0,1,0,0,0,0,0,2,3,4,5,6,7,1,0,
        13,0,12,0,1,1,0,0,0,0,0,0
    };
    assert(!memcmp(message, expected, 40));
    for (unsigned i=40; i<44; ++i) assert(message[i] == 0xa5);
    char log[64] = {}; size_t used=0;
    std::string all;
    for (unsigned i=0;i<10000;++i) {
        std::string line="record"+std::to_string(i);
        itl_log_append(log, sizeof(log), used, line.c_str());
        all += line + ";";
        while (all.size()+1>sizeof(log)) all.erase(0,all.find(';')+1);
        assert(used==strlen(log) && used<sizeof(log));
        assert(all==log);
    }
    const std::string before=log;
    itl_log_append(log, sizeof(log), used, std::string(64,'x').c_str());
    assert(before==log);
    char tiny[2]={}; size_t zero=0;
    itl_log_append(tiny, sizeof(tiny), zero, "");
    assert(zero==1 && !strcmp(tiny,";"));
    puts("station disconnect command and bounded recent-log tests passed");
}
