/*
 * https://github.com/SteveKChiu/bonjour-embedded
 *
 * Copyright 2017, Steve K. Chiu <steve.k.chiu@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the FreeBSD license as published by the FreeBSD
 * project.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the FreeBSD license along with this
 * program. If not, see <http://www.opensource.org/licenses/bsd-license>.
 */

#include "bonjour.h"
#include "mDNSEmbeddedAPI.h"

#include <stdio.h>
#include <stdlib.h>

#if TARGET_OS_WIN32
#include "mDNSWin32.h"
#endif

#if TARGET_OS_LINUX
#include "mDNSPosix.h"
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#define RR_CACHE_SIZE 500

static CacheEntity _rr_cache[RR_CACHE_SIZE];
static int _inited = 0;
static mDNS_PlatformSupport _platform;
static DNSResponderLogProcType _log;

const char ProgramName[] = "bonjour";
mDNS mDNSStorage;

DNSResponderLogProcType DNSResponderSetLogProc(DNSResponderLogProcType log)
{
    DNSResponderLogProcType old = _log;
    _log = log;
    return old;
}

DNSServiceErrorType DNSResponderInit(void)
{
    if (_inited) {
        return 0;
    }

    mDNSPlatformMemZero(&mDNSStorage, sizeof(mDNSStorage));
    mDNSPlatformMemZero(&_platform, sizeof(_platform));

#ifdef TARGET_OS_WIN32
    extern void DNSResponderReportStatus(int type, const char* msg, ...);
    _platform.reportStatusFunc = DNSResponderReportStatus;
#endif

    mStatus err = mDNS_Init(
            &mDNSStorage,
            &_platform,
            _rr_cache,
            RR_CACHE_SIZE,
            mDNS_Init_AdvertiseLocalAddresses,
            mDNS_Init_NoInitCallback,
            mDNS_Init_NoInitCallbackContext);

#if TARGET_OS_WIN32
    if (!err) {
        err = SetupInterfaceList(&mDNSStorage);
    }

    if (!err) {
        err = uDNS_SetupDNSConfig(&mDNSStorage);
    }
#endif

    if (err) {
        LogMsg("DNSResponderInit: failed errno %d", err);
    } else {
        _inited = 1;
    }

    return err;
}

void DNSResponderExit(void)
{
    if (_inited) {
        mDNS_Close(&mDNSStorage);
        _inited = 0;
    }
}

#if TARGET_OS_WIN32

void DNSResponderProcess(int ms)
{
    if (!_inited) {
        return;
    }

    mDNS_Execute(&mDNSStorage);

    extern mStatus mDNSPoll(DWORD msec);
    mDNSPoll(ms);
}

static void DNSResponderReportStatus(int type, const char* msg, ...)
{
    if (_log) {
        char tag;
        switch (type) {
        case EVENTLOG_ERROR_TYPE: tag = 'W'; break;
        case EVENTLOG_WARNING_TYPE: tag = 'I'; break;
        case EVENTLOG_INFORMATION_TYPE: tag = 'V'; break;
        default: tag = 'D'; break;
        }

        _log(tag, msg);
    }
}

#endif

#if TARGET_OS_LINUX

void DNSResponderProcess(int ms)
{
    int result;
    int nfds;
    struct timeval timeout;
    fd_set readfds;

    if (!_inited) {
        return;
    }

    nfds = 0;
    FD_ZERO(&readfds);
    timeout.tv_sec = ms / 1000;
    timeout.tv_usec = (ms % 1000) * 1000;

    mDNSPosixGetFDSet(&mDNSStorage, &nfds, &readfds, &timeout);

    result = select(nfds, &readfds, NULL, NULL, &timeout);
    if (result > 0) {
        mDNSPosixProcessFDSet(&mDNSStorage, &readfds);
    }
}

void mDNSPlatformWriteLogMsg(const char* ident, const char* msg, mDNSLogLevel_t loglevel)
{
    if (_log) {
        char tag;
        switch (loglevel) {
        case MDNS_LOG_MSG: tag = 'W'; break;
        case MDNS_LOG_OPERATION: tag = 'I'; break;
        case MDNS_LOG_SPS: tag = 'V'; break;
        case MDNS_LOG_INFO: tag = 'V'; break;
        default: tag = 'D'; break;
        }

        _log(tag, msg);
    }
}

void mDNSPlatformSourceAddrForDest(mDNSAddr* const src, const mDNSAddr* const dst)
{
    union
    {
        struct sockaddr s;
        struct sockaddr_in a4;
        struct sockaddr_in6 a6;
    } addr;

    socklen_t len = sizeof(addr);
    socklen_t inner_len = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    src->type = mDNSAddrType_None;

    if (sock == -1) {
        return;
    }

    if (dst->type == mDNSAddrType_IPv4) {
        inner_len = sizeof(addr.a4);
#ifndef NOT_HAVE_SA_LEN
        addr.a4.sin_len = inner_len;
#endif
        addr.a4.sin_family = AF_INET;
        addr.a4.sin_port = 1; // Not important, any port will do
        addr.a4.sin_addr.s_addr = dst->ip.v4.NotAnInteger;
    } else if (dst->type == mDNSAddrType_IPv6) {
        inner_len = sizeof(addr.a6);
#ifndef NOT_HAVE_SA_LEN
        addr.a6.sin6_len = inner_len;
#endif
        addr.a6.sin6_family = AF_INET6;
        addr.a6.sin6_flowinfo = 0;
        addr.a6.sin6_port = 1; // Not important, any port will do
        addr.a6.sin6_addr = *(struct in6_addr*)&dst->ip.v6;
        addr.a6.sin6_scope_id = 0;
    } else {
        return;
    }

    if (connect(sock, &addr.s, inner_len) < 0) {
        LogMsg("mDNSPlatformSourceAddrForDest: connect %#a failed errno %d (%s)", dst, errno, strerror(errno));
        goto exit;
    }

    if (getsockname(sock, &addr.s, &len) < 0) {
        LogMsg("mDNSPlatformSourceAddrForDest: getsockname failed errno %d (%s)", errno, strerror(errno));
        goto exit;
    }

    src->type = dst->type;
    if (dst->type == mDNSAddrType_IPv4) {
        src->ip.v4.NotAnInteger = addr.a4.sin_addr.s_addr;
    } else {
        src->ip.v6 = *(mDNSv6Addr*)&addr.a6.sin6_addr;
    }

exit:
    close(sock);
}

#endif
