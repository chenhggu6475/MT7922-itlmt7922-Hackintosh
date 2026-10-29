// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stdint.h>
// Caller serializes with txLock. Match the full generation-bearing token;
// DMA consumption alone is not a successful over-the-air transmission.
struct ItlTxReceipt {
    uint16_t token=0;
    uint8_t state=0; // idle, pending, success, failure
    void reset(){token=0;state=0;}
    void arm(uint16_t t){token=t;state=1;}
    void complete(uint16_t t,uint8_t status){if(state==1&&t==token)state=status?3:2;}
    bool finished() const{return state==2||state==3;}
    bool succeeded() const{return state==2;}
};
