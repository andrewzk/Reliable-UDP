#ifndef EVENT_H
#define EVENT_H

/* event.h -*-mode: C; c-file-style:"cc-mode";-*- */
/*----------------------------------------------------------------------------
File: event.h
Description: RUDP event management: Input event handling, registering file 
descriptors, timers and event loop.
Authors: Olof Hagsand and Peter Sjödin
RUDP event management is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This software is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

*--------------------------------------------------------------------------*/
/*
* Register functions to be called either when input on a file descriptor,
* or as a a result of a timeout.
* The callback function has the following signature:
* int fn(int fd, void* arg)
* fd is the file descriptor where the input was received (for timeouts, this
* contains no information).
* arg is an argument given when the callback was registered.
* If the return value of the callback is < 0, it is treated as an unrecoverable
* error, and the program is terminated.
*/


/*
* Prototypes
*/
int event_timeout(struct timeval timer,
int (*callback)(int, void*), void *callback_arg, char *idstr);

int
event_periodic(int secs,
int (*callback)(int, void*),
void *callback_arg,
char *idstr);

int event_timeout_delete(int (*callback)(int, void*), void *callback_arg);
int event_fd_delete(int (*callback)(int, void*), void *callback_arg);
int event_fd(int fd, int (*callback)(int, void*), void *callback_arg, 
             char *idstr);
int eventloop();

#endif /* EVENT_H */
