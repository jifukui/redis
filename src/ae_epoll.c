/* Linux epoll(2) based ae.c module
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/epoll.h>
/**定义*/
typedef struct aeApiState 
{
    int epfd;
    struct epoll_event *events;
} aeApiState;
/**创建api*/
static int aeApiCreate(aeEventLoop *eventLoop) 
{
    /**创建对象*/
    aeApiState *state = zmalloc(sizeof(aeApiState));
    /**创建失败处理*/
    if (!state) 
    {
        return -1;
    }
    /**分配事件*/
    state->events = zmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    /**事件分配失败处理*/
    if (!state->events) 
    {
        zfree(state);
        return -1;
    }
    /**创建epoll*/
    state->epfd = epoll_create(1024); /* 1024 is just a hint for the kernel */
    if (state->epfd == -1) 
    {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    eventLoop->apidata = state;
    return 0;
}
/**调整大小*/
static int aeApiResize(aeEventLoop *eventLoop, int setsize) 
{
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events, sizeof(struct epoll_event)*setsize);
    return 0;
}
/**释放api*/
static void aeApiFree(aeEventLoop *eventLoop) 
{
    aeApiState *state = eventLoop->apidata;

    close(state->epfd);
    zfree(state->events);
    zfree(state);
}
/**添加事件*/
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) 
{
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    /**获取操作指令添加还是修改*/
    int op = eventLoop->events[fd].mask == AE_NONE ?EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    /**设置epoll事件*/
    ee.events = 0;
    /**获取操作指令*/
    mask |= eventLoop->events[fd].mask; /* Merge old events */
    /**可读操作设置可读*/
    if (mask & AE_READABLE) 
    {
        ee.events |= EPOLLIN;
    }
    /**可写设置可写*/
    if (mask & AE_WRITABLE) 
    {
        ee.events |= EPOLLOUT;
    }
    /**获取文件描述符*/
    ee.data.fd = fd;
    /**注册函数*/
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) 
    {
        return -1;
    }
    return 0;
}
/**事件删除*/
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) 
{
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) 
    {
        ee.events |= EPOLLIN;
    }
    if (mask & AE_WRITABLE) 
    {
        ee.events |= EPOLLOUT;
    }
    ee.data.fd = fd;
    if (mask != AE_NONE) 
    {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } 
    else 
    {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}
/**事件检测*/
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) 
{
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;
    /**事件是否产生*/
    retval = epoll_wait(state->epfd,state->events,eventLoop->setsize,tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    /**事件产生的处理*/
    if (retval > 0) 
    {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) 
        {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) 
            {
                mask |= AE_READABLE;
            }
            if (e->events & EPOLLOUT) 
            {
                mask |= AE_WRITABLE;
            }
            if (e->events & EPOLLERR) 
            {
                mask |= AE_WRITABLE;
            }
            if (e->events & EPOLLHUP) 
            {
                mask |= AE_WRITABLE;
            }
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }
    return numevents;
}

/**返回api的名字*/
static char *aeApiName(void) 
{
    return "epoll";
}
