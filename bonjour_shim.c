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

extern mDNS mDNSStorage;

//----------------------------------------------------------------------------

DNSServiceErrorType DNSServiceGetProperty(
        const char* property,
        void* result,
        uint32_t* size)
{
    if (!property || !result || !size) {
        return kDNSServiceErr_BadParam;
    }

    if (strcmp(property, kDNSServiceProperty_DaemonVersion) == 0) {
        *(uint32_t*)result = _DNS_SD_H;
        *size = 4;
    } else {
        *size = 0;
    }
    return kDNSServiceErr_NoError;
}

//----------------------------------------------------------------------------

typedef struct DNSServiceNATPortMappingRef
{
    void (*disposefn)(struct DNSServiceNATPortMappingRef*);
    DNSServiceFlags flags;
    NATTraversalInfo q;
    DNSServiceNATPortMappingReply callback;
    void* context;
} DNSServiceNATPortMappingRef;

static void DNSServiceNATPortMappingDispose(DNSServiceNATPortMappingRef* op)
{
    if (!op) {
        return;
    }

    LogOperation("%p: DNSServiceNATPortMappingCreate(%X, %u, %u, %d) STOP",
            op, op->q.Protocol, mDNSVal16(op->q.IntPort), mDNSVal16(op->q.RequestedPort), op->q.NATLease);

    mDNS_StopNATOperation(&mDNSStorage, &op->q);
    mDNSPlatformMemFree(op);
}

static void DNSServiceNATPortMappingCallback(mDNS* m, NATTraversalInfo* q)
{
    DNSServiceNATPortMappingRef* op;

    op = (DNSServiceNATPortMappingRef*)q->clientContext;
    if (!op) {
        return;
    }

    LogOperation("%p: DNSServiceNATPortMappingCreate(%X, %u, %u, %d) RESULT %.4a:%u TTL %u",
            op, op->q.Protocol, mDNSVal16(op->q.IntPort), mDNSVal16(op->q.RequestedPort), op->q.NATLease,
            &q->ExternalAddress, mDNSVal16(q->ExternalPort), q->Lifetime);

    if (!op->callback) {
        return;
    }

    op->callback(
            (DNSServiceRef)op,
            op->flags,
            mDNSPlatformInterfaceIndexfromInterfaceID(m, q->InterfaceID, mDNStrue),
            q->Result,
            q->ExternalAddress.NotAnInteger,
            q->Protocol,
            q->IntPort.NotAnInteger,
            q->ExternalPort.NotAnInteger,
            q->Lifetime,
            op->context);
}

DNSServiceErrorType DNSServiceNATPortMappingCreate(
        DNSServiceRef* sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceProtocol protocol,
        uint16_t internalPort,
        uint16_t externalPort,
        uint32_t ttl,
        DNSServiceNATPortMappingReply callback,
        void* context)
{
    mStatus err;
    mDNSInterfaceID interfaceID;
    DNSServiceNATPortMappingRef* op;

    interfaceID = mDNSPlatformInterfaceIDfromInterfaceIndex(&mDNSStorage, interfaceIndex);
    if (interfaceIndex && !interfaceID) {
        return mStatus_BadParamErr;
    }

    if (!protocol) {
        // (i.e. just request public address) then internalPort, externalPort, ttl must be zero too
        if (internalPort || externalPort || ttl) {
            return mStatus_BadParamErr;
        }
    } else {
        if (!internalPort) {
            return mStatus_BadParamErr;
        }
        if (!(protocol & (kDNSServiceProtocol_UDP | kDNSServiceProtocol_TCP))) {
            return mStatus_BadParamErr;
        }
    }

    op = (DNSServiceNATPortMappingRef*)mDNSPlatformMemAllocate(sizeof(DNSServiceNATPortMappingRef));
    if (!op) {
        return mStatus_NoMemoryErr;
    }

    mDNSPlatformMemZero(op, sizeof(DNSServiceNATPortMappingRef));
    op->disposefn = DNSServiceNATPortMappingDispose;
    op->flags = flags;
    op->callback = callback;
    op->context = context;

    op->q.InterfaceID = interfaceID;
    op->q.Protocol = !protocol ? NATOp_AddrRequest : (protocol == kDNSServiceProtocol_UDP) ? NATOp_MapUDP : NATOp_MapTCP;
    op->q.IntPort.NotAnInteger = internalPort; // internalPort is already in network byte order
    op->q.RequestedPort.NotAnInteger = externalPort; // externalPort is already in network byte order
    op->q.NATLease = ttl;
    op->q.clientCallback = DNSServiceNATPortMappingCallback;
    op->q.clientContext = op;

    LogOperation("%p: DNSServiceNATPortMappingCreate(%X, %u, %u, %d) START",
            op, protocol, mDNSVal16(op->q.IntPort), mDNSVal16(op->q.RequestedPort), ttl);

    err = mDNS_StartNATOperation(&mDNSStorage, &op->q);

    if (err) {
        LogMsg("ERROR: mDNS_StartNATOperation: %d", (int)err);
        mDNSPlatformMemFree(op);
        *sdRef = NULL;
    } else {
        *sdRef = (DNSServiceRef)op;
    }

    return err;
}

//----------------------------------------------------------------------------

// Copied from dnssd_clientshim.c mDNS_DirectOP_Register, need to be identical
typedef struct DNSServiceRegisterRef
{
    void (*disposefn)(struct DNSServiceRegisterRef*);
    DNSServiceRegisterReply callback;
    void* context;
    mDNSBool autoname; // Set if this name is tied to the Computer Name
    mDNSBool autorename; // Set if we just got a name conflict and now need to automatically pick a new name
    domainlabel name;
    domainname host;
    ServiceRecordSet s;
} DNSServiceRegisterRef;

typedef struct DNSServiceRecordRef
{
    void (*disposefn)(struct DNSServiceRecordRef*);
} DNSServiceRecordRef;

DNSServiceErrorType DNSServiceRegisterRecord(
        DNSServiceRef sdRef,
        DNSRecordRef* RecordRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        const char* fullname,
        uint16_t rrtype,
        uint16_t rrclass,
        uint16_t rdlen,
        const void* rdata,
        uint32_t ttl,
        DNSServiceRegisterRecordReply callback,
        void* context)
{
    return kDNSServiceErr_Unsupported;
}

DNSServiceErrorType DNSServiceAddRecord(
        DNSServiceRef sdRef,
        DNSRecordRef* RecordRef,
        DNSServiceFlags flags,
        uint16_t rrtype,
        uint16_t rdlen,
        const void* rdata,
        uint32_t ttl)
{
    return kDNSServiceErr_Unsupported;
}

DNSServiceErrorType DNSServiceUpdateRecord(
        DNSServiceRef sdRef,
        DNSRecordRef RecordRef,
        DNSServiceFlags flags,
        uint16_t rdlen,
        const void* rdata,
        uint32_t ttl)
{
    return kDNSServiceErr_Unsupported;
}

DNSServiceErrorType DNSServiceRemoveRecord(
        DNSServiceRef sdRef,
        DNSRecordRef RecordRef,
        DNSServiceFlags flags)
{
    return kDNSServiceErr_Unsupported;
}

//----------------------------------------------------------------------------

DNSServiceErrorType DNSServiceReconfirmRecord(
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        const char* fullname,
        uint16_t rrtype,
        uint16_t rrclass,
        uint16_t rdlen,
        const void* rdata)
{
    return kDNSServiceErr_Unsupported;
}

//----------------------------------------------------------------------------

DNSServiceErrorType DNSServiceCreateConnection(DNSServiceRef* sdRef)
{
    return kDNSServiceErr_Unsupported;
}

//----------------------------------------------------------------------------

DNSServiceErrorType DNSServiceEnumerateDomains(
        DNSServiceRef* sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceDomainEnumReply callback,
        void* context)
{
    return kDNSServiceErr_Unsupported;
}
