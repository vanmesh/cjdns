/* vim: set expandtab ts=4 sw=4: */
/*
 * You may redistribute this program and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "net/SessionTable.h"
#include "crypto/CryptoAuth.h"
#include "crypto/AddressCalc.h"
#include "memory/Allocator.h"
#include "util/Bits.h"
#include "util/events/Time.h"
#include "util/events/Timeout.h"
#include "wire/Error.h"
#include "wire/Headers.h"
#include "wire/Message.h"

#include <stdint.h>

/** The number of seconds of inactivity before a session should expire. */
#define SESSION_TIMEOUT_SECONDS 180

/** The number of seconds between cleanup cycles. */
#define CLEANUP_CYCLE_SECONDS 20

/** Number of milliseconds between state change announcements. */
#define STATE_UPDATE_TIME 8000

/** Handle numbers 0-3 are reserved for CryptoAuth nonces. */
#define MIN_FIRST_HANDLE 4

#define MAX_FIRST_HANDLE 100000

struct SessionTable_Session_pvt
{
    struct SessionTable_Session pub;
    struct SessionTable* const sm;
    Identity
};

struct Ip6
{
    uint8_t bytes[16];
};
#define Map_NAME OfSessionsByIp6
#define Map_KEY_TYPE struct Ip6
#define Map_VALUE_TYPE struct SessionTable_Session*
#define Map_ENABLE_HANDLES
#include "util/Map.h"


/**
 * A SessionTable is a mechanism for getting a crypto session based on a given key.
 */
struct SessionTable
{
    struct Map_OfSessionsByIp6 ifaceMap;

    struct Allocator* allocator;

    struct Timeout* cleanupInterval;

    struct CryptoAuth* cryptoAuth;

    /** The first handle number to start with, randomized at startup to reduce collisions. */
    uint32_t first;

    Identity
};
/*
static void cleanup(void* vsm)
{
    struct SessionTable* sm = (struct SessionTable*) vsm;
    uint64_t cutoffTime =
        Time_currentTimeMilliseconds(sm->eventBase) - (SESSION_TIMEOUT_SECONDS * 1000);
    for (uint32_t i = 0; i < sm->ifaceMap.count; i++) {
        struct SessionTable_Session* sess = &sm->ifaceMap.values[i]->pub;

        // Received a message since cutoff time.
        if (sess->timeOfLastIn > cutoffTime) { continue; }

        // Received a message (ever) or session is not older than cutoff time.
        if (sess->timeOfLastIn || sess->timeOfCreation > cutoffTime) { continue; }

        struct Allocator* ifAllocator = sess->external.allocator;
        Map_OfSessionsByIp6_remove(i, &sm->ifaceMap);
        Allocator_free(ifAllocator);
        i--;
    }
}*/

static int removeSession(struct Allocator_OnFreeJob* job)
{
    struct SessionTable_Session* s = (struct SessionTable_Session*) job->userData;
    struct SessionTable* st = Identity_check((struct SessionTable*) s->sessionTable);
    int index = Map_OfSessionsByIp6_indexForHandle(s->receiveHandle - st->first, &st->ifaceMap);
    Assert_true(index >= 0);
    Map_OfSessionsByIp6_remove(index, &st->ifaceMap);
    return 0;
}

static void check(struct SessionTable* st, int mapIndex)
{
    Assert_true(st->ifaceMap.keys[mapIndex].bytes[0] == 0xfc);
    uint8_t* herPubKey = st->ifaceMap.values[mapIndex].caSession->herPublicKey;
    if (!Bits_isZero(herPubKey, 32)) {
        uint8_t ip6[16];
        AddressCalc_addressForPublicKey(ip6, herPubKey);
        Assert_true(!Bits_memcmp(&st->ifaceMap.keys[mapIndex], ip6, 16));
    }
}
/*
static void stateChange(struct SessionTable_Session_pvt* ss,
                        uint64_t prevTimeOfLastIn,
                        uint64_t prevTimeOfLastOut,
                        int prevCryptoAuthState)
{
}

static uint8_t sendMessage(struct Message* msg, struct Iface* iface)
{
    struct SessionTable_Session_pvt* ss =
        Identity_check((struct SessionTable_Session_pvt*)iface);

    uint64_t timeOfLastOut = ss->pub.timeOfLastOut;
    ss->pub.timeOfLastOut = Time_currentTimeMilliseconds(ss->sm->eventBase);
    int prevState = ss->pub.cryptoAuthState;
    ss->pub.cryptoAuthState = CryptoAuth_getState(ss->pub.internal);
    if ((ss->pub.timeOfLastOut - timeOfLastOut) > STATE_UPDATE_TIME
        || prevState != ss->pub.cryptoAuthState)
    {
        stateChange(ss, ss->pub.timeOfLastIn, timeOfLastOut, prevState);
    }

    return Interface_sendMessage(&ss->sm->iface, msg);
}

static uint8_t receiveMessage(struct Message* msg, struct Iface* iface)
{
    struct SessionTable_Session_pvt* ss =
        Identity_check((struct SessionTable_Session_pvt*)iface->receiverContext);

    uint64_t timeOfLastIn = ss->pub.timeOfLastIn;
    ss->pub.timeOfLastIn = Time_currentTimeMilliseconds(ss->sm->eventBase);
    int prevState = ss->pub.cryptoAuthState;
    ss->pub.cryptoAuthState = CryptoAuth_getState(ss->pub.internal);
    if ((ss->pub.timeOfLastIn - timeOfLastIn) > STATE_UPDATE_TIME
        || prevState != ss->pub.cryptoAuthState)
    {
        stateChange(ss, timeOfLastIn, ss->pub.timeOfLastOut, prevState);
    }

    return Iface_send(&ss->sm->iface, msg);
}*/

struct SessionTable_Session* SessionTable_sessionForIp6(uint8_t* ip6, struct SessionTable* st)
{
    int ifaceIndex = Map_OfSessionsByIp6_indexForKey((struct Ip6*)ip6, &st->ifaceMap);
    if (ifaceIndex == -1) { return NULL; }
    check(st, ifaceIndex);
    return st->ifaceMap.values[ifaceIndex];
}

void SessionTable_regSession(uint8_t* ip6,
                             uint8_t cryptoKey[32],
                             struct SessionTable_Session* session,
                             struct Allocator* alloc,
                             struct SessionTable* st)
{
    Assert_true(cryptoKey);
    Assert_true(!SessionTable_sessionForIp6(ip6, st));
    session->sessionTable = st;
    session->caSession = CryptoAuth_newSession(cryptoKey, ip6, false, "inner", st->cryptoAuth);
    int ifaceIndex = Map_OfSessionsByIp6_put((struct Ip6*)ip6, &ss, &st->ifaceMap);
    session->receiveHandle = st->ifaceMap.handles[ifaceIndex] + st->first;
    check(st, ifaceIndex);
    Allocator_onFree(alloc, removeSession, session);
    return &Identity_check(sm->ifaceMap.values[ifaceIndex])->pub;
}

struct SessionTable_Session* SessionTable_sessionForHandle(uint32_t handle, struct SessionTable* st)
{
    int index = Map_OfSessionsByIp6_indexForHandle(handle - st->first, &st->ifaceMap);
    if (index < 0) { return NULL; }
    check(st, index);
    return st->ifaceMap.values[index];
}

struct SessionTable_HandleList* SessionTable_getHandleList(struct SessionTable* st,
                                                           struct Allocator* alloc)
{
    struct SessionTable_HandleList* out =
        Allocator_malloc(alloc, sizeof(struct SessionTable_HandleList));
    uint32_t* buff = Allocator_malloc(alloc, 4 * st->ifaceMap.count);
    Bits_memcpy(buff, st->ifaceMap.handles, 4 * st->ifaceMap.count);
    out->handles = buff;
    out->count = st->ifaceMap.count;
    for (int i = 0; i < (int)out->count; i++) {
        buff[i] += st->first;
    }
    return out;
}

struct SessionTable* SessionTable_new(struct CryptoAuth* cryptoAuth,
                                      struct Random* rand,
                                      struct Allocator* allocator)
{
    struct SessionTable* st = Allocator_calloc(allocator, sizeof(struct SessionTable), 1);
    Identity_set(st);
    st->ifaceMap.allocator = allocator;
    st->cryptoAuth = cryptoAuth;
    st->allocator = allocator;
    st->first = (Random_uint32(rand) % (MAX_FIRST_HANDLE - MIN_FIRST_HANDLE)) + MIN_FIRST_HANDLE;
  //  st->cleanupInterval =
//        Timeout_setInterval(cleanup, sm, 1000 * CLEANUP_CYCLE_SECONDS, eventBase, allocator);
    return st;
}