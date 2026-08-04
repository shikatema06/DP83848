/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * This file is part of the lwIP TCP/IP stack.
 */
#ifndef __TCP_ECHOSERVER_H__
#define __TCP_ECHOSERVER_H__

void tcp_echoserver_init(void);
void tcp_echoserver_spi_stream_poll(void);
void tcp_echoserver_tim7_isr(void);

#endif /* __TCP_ECHOSERVER */
