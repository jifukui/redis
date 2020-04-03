/* adlist.h - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */
/**双向链表节点结构
 * prev：前指针
 * next：后指针
 * value：参数值
*/
typedef struct listNode {
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;
/**链表迭代器结构
 * next：下一个节点
 * direction：迭代方向
*/
typedef struct listIter {
    listNode *next;
    int direction;
} listIter;
/**链表结构
 * head：链表的头节点
 * tail：链表的尾节点
 * dup:链表的拷贝函数
 * free:释放链表
 * match:链表
 * len：链表的长度
*/
typedef struct list {
    listNode *head;
    listNode *tail;
    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr, void *key);
    unsigned long len;
} list;

/* Functions implemented as macros */
/**获取链表的长度*/
#define listLength(l) ((l)->len)
/**获取链表头节点*/
#define listFirst(l) ((l)->head)
/**获取链表尾节点*/
#define listLast(l) ((l)->tail)
/**获取节点的上一个节点*/
#define listPrevNode(n) ((n)->prev)
/**获取节点的下一个节点*/
#define listNextNode(n) ((n)->next)
/**获取节点的值*/
#define listNodeValue(n) ((n)->value)
/**节点复制*/
#define listSetDupMethod(l,m) ((l)->dup = (m))
/**节点释放*/
#define listSetFreeMethod(l,m) ((l)->free = (m))
/**节点匹配*/
#define listSetMatchMethod(l,m) ((l)->match = (m))

/**获取节点复制函数*/
#define listGetDupMethod(l) ((l)->dup)
/**获取节点释放函数*/
#define listGetFree(l) ((l)->free)
/**获取节点匹配函数*/
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
list *listCreate(void);
void listRelease(list *list);
void listEmpty(list *list);
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
list *listInsertNode(list *list, listNode *old_node, void *value, int after);
void listDelNode(list *list, listNode *node);
listIter *listGetIterator(list *list, int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, long index);
void listRewind(list *list, listIter *li);
void listRewindTail(list *list, listIter *li);
void listRotate(list *list);
void listJoin(list *l, list *o);

/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
