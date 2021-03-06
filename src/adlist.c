/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
/**创建链表
 * 返回链表对象
 * 为此链表分配内存空间
 * 设置头尾节点为NULL
 * 链表长度为0
 * 复制函数为空
 * 释放函数为空
 * 匹配函数为空
*/
list *listCreate(void)
{
    struct list *list;

    if ((list = zmalloc(sizeof(*list))) == NULL)
    {
        return NULL;
    }
    /**初始化链表对象的相关参数*/
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/* Remove all the elements from the list without destroying the list itself. */
/**设置链表为空链表
 * 从链表的头部开始逐个获取链表的下一个节点
 * 如果下一个节点的释放函数存在调用次释放函数释放当前值
 * 释放此节点申请的内存空间
 * 设置头尾节点为空节点
 * 设置连表长度为0
*/
void listEmpty(list *list)
{
    unsigned long len;
    listNode *current, *next;
    /**当前操作的节点*/
    current = list->head;
    /**链表的长度*/
    len = list->len;
    /**根据链表的长度释放节点数据申请的内存空间*/
    while(len--) 
    {
        next = current->next;
        /**链表对象的释放函数存在的处理函数*/
        if (list->free) 
        {
            list->free(current->value);
        }
        zfree(current);
        current = next;
    }
    list->head = list->tail = NULL;
    list->len = 0;
}

/* Free the whole list.
 *
 * This function can't fail. */
/**释放链表，即释放链表的所有申请的空间
 * 设置链表为空链表
 * 释放链表申请的内存空间
*/
void listRelease(list *list)
{
    listEmpty(list);
    zfree(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
/**为链表添加头结点
 * 返回链表对象
 * 在链表的头部插入节点
*/
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    /**内存申请失败*/
    if ((node = zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }
    /**设置节点参数*/
    node->value = value;
    /**对于链表为空链表的处理
     * 设置头结点和尾节点都为此节点
     * 设置此节点的上一个节点和下一个节点为空
    */
    if (list->len == 0) 
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } 
    /**对于链表不是空节点的处理
     * 设置此节点的上一个节点为空
     * 设置此节点的下一个节点为原来的头结点
     * 设置原头结点的上一个节点为此节点
     * 设置头结点为此节点
    */
    else 
    {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
/**连表添加尾节点的处理
 * 在链表的尾部插入节点
*/
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }
    node->value = value;
    if (list->len == 0) 
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } 
    else 
    {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}
/**链表插入节点
 * list：链表
 * old_node：节点
 * value：参数
 * after:向后添加标志
*/
list *listInsertNode(list *list, listNode *old_node, void *value, int after) 
{
    listNode *node;
    /**为节点创建分配内存空间，内存分配失败*/
    if ((node = zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }
    /**设置节点参数*/
    node->value = value;
    /**添加到old_node节点的后面
     * 设置此节点的前一个节点为old节点
     * 设置此节点的后一个节点为old节点之前的后一个节点
     * 如果old节点为链表尾，设置此节点为链表尾
    */
    if (after) 
    {
        node->prev = old_node;
        node->next = old_node->next;
        if (list->tail == old_node) 
        {
            list->tail = node;
        }
    } 
    /**添加到old节点的前面
     * 设置此节点的下一个节点为old节点
     * 设置此节点的上一个节点为old节点的上一个节点
     * 如果old节点为头结点设置此节点为头结点
    */
    else 
    {
        node->next = old_node;
        node->prev = old_node->prev;
        if (list->head == old_node) 
        {
            list->head = node;
        }
    }
    /**如果此节点的上一个节点不为空
     * 设置此节点的前一个节点的下一个节点为此节点
    */
    if (node->prev != NULL) 
    {
        node->prev->next = node;
    }
    /**如果此节点的上一个节点不为空
     * 设置此节点的下一个节点的上一个节点为此节点
    */
    if (node->next != NULL) 
    {
        node->next->prev = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
/**删除节点*/
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
    {
        node->prev->next = node->next;
    }
    else
    {
        list->head = node->next;
    }
    if (node->next)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }
    if (list->free)
    {
        list->free(node->value);
    }
    zfree(node);
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
/**获取迭代器
 * direction：迭代方向
*/
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;
    /**创建迭代器*/
    if ((iter = zmalloc(sizeof(*iter))) == NULL)
    {
        return NULL;
    }
    /**根据direction的值
     * 设置迭代器下一个处理的节点
    */
    if (direction == AL_START_HEAD)
    {
        iter->next = list->head;
    }
    else
    {
        iter->next = list->tail;
    }
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
/**释放迭代器*/
void listReleaseIterator(listIter *iter) 
{
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
/**设置迭代器的下一个处理节点和方向为重头节点开始的正向迭代*/
void listRewind(list *list, listIter *li) 
{
    li->next = list->head;
    li->direction = AL_START_HEAD;
}
/**设置反向迭代*/
void listRewindTail(list *list, listIter *li) 
{
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
/**从迭代器获取处理节点*/
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) 
    {
        if (iter->direction == AL_START_HEAD)
        {
            iter->next = current->next;
        }
        else
        {
            iter->next = current->prev;
        }
    }
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
/**链表复制*/
list *listDup(list *orig)
{
    list *copy;
    listIter iter;
    listNode *node;
    /**创建链表*/
    if ((copy = listCreate()) == NULL)
    {
        return NULL;
    }
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    /**正向迭代链表*/
    listRewind(orig, &iter);
    while((node = listNext(&iter)) != NULL) 
    {
        void *value;

        if (copy->dup) 
        {
            value = copy->dup(node->value);
            if (value == NULL) 
            {
                listRelease(copy);
                return NULL;
            }
        } 
        else
        {
            value = node->value;
        }
        /**对于链表尾端添加数据失败处理释放整个链表*/
        if (listAddNodeTail(copy, value) == NULL) 
        {
            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
/**获取键值
 * 对于存在此key返回此key的节点
 * 对于没有匹配的值返回空
*/
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;
    /**正向迭代*/
    listRewind(list, &iter);
    while((node = listNext(&iter)) != NULL) 
    {
        if (list->match) 
        {
            if (list->match(node->value, key)) 
            {
                return node;
            }
        } 
        /**如果key的值等于此节点的值返回此节点*/
        else 
        {
            if (key == node->value) 
            {
                return node;
            }
        }
    }
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
/**根据索引值获取此链表的节点
 * 0为头结点
 * -1为尾节点
*/
listNode *listIndex(list *list, long index) 
{
    listNode *n;

    if (index < 0) 
    {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) 
        {
            n = n->prev;
        }
    } 
    else 
    {
        n = list->head;
        while(index-- && n) 
        {
            n = n->next;
        }
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
/**删除尾节点并更新到头结点*/
void listRotate(list *list) 
{
    listNode *tail = list->tail;
    /**对于链表长度为1的处理直接返回*/
    if (listLength(list) <= 1) 
    {
        return;
    }

    /* Detach current tail */
    list->tail = tail->prev;
    list->tail->next = NULL;
    /* Move it as head */
    list->head->prev = tail;
    tail->prev = NULL;
    tail->next = list->head;
    list->head = tail;
}

/* Add all the elements of the list 'o' at the end of the
 * list 'l'. The list 'other' remains empty but otherwise valid. */
/**链表合并,将链表o合并到链表l后面
 * l:
 * o:
*/
void listJoin(list *l, list *o) 
{
    if (o->head)
    {
        o->head->prev = l->tail;
    }

    if (l->tail)
    {
        l->tail->next = o->head;
    }
    else
    {
        l->head = o->head;
    }

    if (o->tail) 
    {
        l->tail = o->tail;
    }
    l->len += o->len;

    /* Setup other as an empty list. */
    o->head = o->tail = NULL;
    o->len = 0;
}
