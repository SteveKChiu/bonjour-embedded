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

#ifndef DNS_SD_EMBEDED_H
#define DNS_SD_EMBEDED_H

#include "dns_sd.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef void(*DNSResponderLogProcType)(char, const char*);
DNSResponderLogProcType DNSResponderSetLogProc(DNSResponderLogProcType log);

DNSServiceErrorType DNSResponderInit(void);
void DNSResponderProcess(int ms);
void DNSResponderExit(void);

#ifdef  __cplusplus
}
#endif

#endif
