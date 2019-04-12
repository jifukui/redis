/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#ifndef DICT_BENCHMARK_MAIN
#include "redisassert.h"
#else
#include <assert.h>
#endif

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio. */
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static long _dictKeyIndex(dict *ht, const void *key, uint64_t hash, dictEntry **existing);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */
/**定义字典hash功能的种子个数*/
static uint8_t dict_hash_function_seed[16];
/**设置字典hash功能种子的种子值*/
void dictSetHashFunctionSeed(uint8_t *seed) 
{
    memcpy(dict_hash_function_seed,seed,sizeof(dict_hash_function_seed));
}
/**获取hash功能种子的首地址*/
uint8_t *dictGetHashFunctionSeed(void) 
{
    return dict_hash_function_seed;
}

/* The default hashing function uses SipHash implementation
 * in siphash.c. */

uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);
/***/
uint64_t dictGenHashFunction(const void *key, int len) 
{
    return siphash(key,len,dict_hash_function_seed);
}
/***/
uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len) 
{
    return siphash_nocase(buf,len,dict_hash_function_seed);
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy(). */
/**复位字典，只能在ht_destory函数中使用
 * 设置表项为空
 * 设置表尺寸为0
 * 设置尺寸掩码为0
 * 设置使用为0
*/
static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table */
/**创建hash表*/
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    /**为字典对象分配内存空间*/
    dict *d = zmalloc(sizeof(*d));
    /**初始化字典对象*/
    _dictInit(d,type,privDataPtr);
    return d;
}

/* Initialize the hash table */
/**初始化字典hash表*/
int _dictInit(dict *d, dictType *type,void *privDataPtr)
{
    /**复位第一个hash表*/
    _dictReset(&d->ht[0]);
    /**复位第二个hash表*/
    _dictReset(&d->ht[1]);
    /**设置字典类型*/
    d->type = type;
    /**设置私有数据*/
    d->privdata = privDataPtr;
    /**设置索引值为-1*/
    d->rehashidx = -1;
    /**设置迭代器参数为0*/
    d->iterators = 0;
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1 */
 /**对字典进程重新设置大小
  * 如果设置不能进行重新设置或者此字典已经重新设置了大小返回错误
  * 
 */
int dictResize(dict *d)
{
    int minimal;

    if (!dict_can_resize || dictIsRehashing(d)) 
    {
        return DICT_ERR;
    }
    minimal = d->ht[0].used;
    /**对字典的已有节点小于设置的最下初始化大小进行处理*/
    if (minimal < DICT_HT_INITIAL_SIZE)
    {
        minimal = DICT_HT_INITIAL_SIZE;
    }
    /**将此字典进行扩展*/
    return dictExpand(d, minimal);
}

/* Expand or create the hash table */
/**
 * 扩展或者是创建hash表
 * d：为字典对象
 * size：定义的大小
*/
int dictExpand(dict *d, unsigned long size)
{
    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table */
    /**对于字典hash表散列过或者字典的hash表的已有节点大于设置的大小处理
     * 返回错误
    */
    if (dictIsRehashing(d) || d->ht[0].used > size)
    {
        return DICT_ERR;
    }

    dictht n; /* the new hash table */
    /**获取size值的最小2的次方数*/
    unsigned long realsize = _dictNextPower(size);

    /* Rehashing to the same table size is not useful. */
    /**如果获取的值等于当前hash表已经使用的节点数的处理
     * 返回失败
    */
    if (realsize == d->ht[0].size)
    {
        return DICT_ERR;
    }

    /* Allocate the new hash table and initialize all pointers to NULL */
    /**设置hash表的大小*/
    n.size = realsize;
    /**设置hash表的掩码*/
    n.sizemask = realsize-1;
    /**内配字典实例空间*/
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    /**设置已经使用节点的为0*/
    n.used = 0;

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. */
    /**对字典的hash表0的表对象的值为空的处理
     * 设置表0的值为新建的hash表的值
     * 返回成功
    */
    if (d->ht[0].table == NULL) 
    {
        d->ht[0] = n;
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing */
    /**将hash表n设置为字典hash表2的值，并设置可散列次数为0*/
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time. */
/**字典重新散列
 * d:为字典
 * n：
*/
int dictRehash(dict *d, int n) 
{
    int empty_visits = n*10; /* Max number of empty buckets to visit. */
    /**对于此hash已经重新散列过的处理
     * 返回0
    */
    if (!dictIsRehashing(d)) 
    {
        return 0;
    }
    /**对于n不为0且字典的hash0的已有节点不为0的处理*/
    while(n-- && d->ht[0].used != 0) 
    {
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0 */
        /**对于字典hash0的表大小大于字典的可散列次数进行校验*/
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        /**寻找字典从可重散列次数的值处寻找第一个不为空的项
         * 直到empty_visits的值为0
         * 如果没哟找到你返回1
        */
        while(d->ht[0].table[d->rehashidx] == NULL) 
        {
            d->rehashidx++;
            if (--empty_visits == 0) 
            {
                return 1;
            }
        }
        /**得到这个不为空的字典实例*/
        de = d->ht[0].table[d->rehashidx];
        /* Move all the keys in this bucket from the old to the new hash HT */
        /**将重此处开始的值从hash表0中写入到hash表1中*/
        while(de) 
        {
            uint64_t h;
            /**指向字典实例的下一个实例*/
            nextde = de->next;
            /* Get the index in the new hash table */
            /***/
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            /**设置当前字典实例的下一个字典实例的下一个字典实例指向字典hash表1的h字典实例*/
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;
            d->ht[0].used--;
            d->ht[1].used++;
            de = nextde;
        }
        /**清除*/
        d->ht[0].table[d->rehashidx] = NULL;
        /**设置可散列次数加一*/
        d->rehashidx++;
    }

    /* Check if we already rehashed the whole table... */
    /**对于hash表0的已使用节点数量为0的处理
     * 释放散列表0的表空间
     * 设置散列表0的值为散列表1的值
     * 复位散列表1
     * 设置可散列次数为-1
     * 返回0
    */
    if (d->ht[0].used == 0) 
    {
        zfree(d->ht[0].table);
        d->ht[0] = d->ht[1];
        _dictReset(&d->ht[1]);
        d->rehashidx = -1;
        return 0;
    }

    /* More to rehash... */
    return 1;
}
/**返回当前时间单位毫秒*/
long long timeInMilliseconds(void) 
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms+1 milliseconds */
/**对字典进行扩展128个进行处理返回散列用时超过指定时间的次数*/
int dictRehashMilliseconds(dict *d, int ms) 
{
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while(dictRehash(d,100)) 
    {
        rehashes += 100;
        if (timeInMilliseconds()-start > ms) 
        {
            break;
        }
    }
    return rehashes;
}

/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. */
/**字典重散列步骤*/
static void _dictRehashStep(dict *d) 
{
    if (d->iterators == 0) 
    {
        dictRehash(d,1);
    }
}

/* Add an element to the target hash table */
/**向字典中添加数据*/
int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d,key,NULL);

    if (!entry) 
    {
        return DICT_ERR;
    }
    dictSetVal(d, entry, val);
    return DICT_OK;
}

/* Low level add or find:
 * This function adds the entry but instead of setting a value returns the
 * dictEntry structure to the user, that will make sure to fill the value
 * field as he wishes.
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey,NULL);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned, and "*existing" is populated
 * with the existing entry if existing is not NULL.
 *
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */
 /**返回字典实例的对象*/
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    long index;
    dictEntry *entry;
    dictht *ht;
    /**对于可散列值不为0的处理
     * 进行散列
    */
    if (dictIsRehashing(d)) 
    {
        _dictRehashStep(d);
    }

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    /**对于没有找到键值的处理
     * 返回空
    */
    if ((index = _dictKeyIndex(d, key, dictHashKey(d,key), existing)) == -1)
    {
        return NULL;
    }

    /* Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently. */
    /**根据可散列状态获取对应的hash表
     * 并获取其中的字典实例
     * 设置hash的使用已使用加一
    */
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    /* Set the hash entry fields. */
    /**设置此实例的键值*/
    dictSetKey(d, entry, key);
    return entry;
}

/* Add or Overwrite:
 * Add an element, discarding the old value if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */
/**根据键值设置value值*/
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry;
    dictEntry *existing;
    dictEntry auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will succeed. */
    /**获取操作字典实例*/
    entry = dictAddRaw(d,key,&existing);
    /**对于字典实例存在的处理设置实例的value值*/
    /**对于字典实例存在的处理设置实例的value值返回1*/
    if (entry) 
    {
        dictSetVal(d, entry, val);
        return 1;
    }

    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    /**对于实例不存在的处理*/
    auxentry = *existing;
    dictSetVal(d, existing, val);
    dictFreeVal(d, &auxentry);
    return 0;
}

/* Add or Find:
 * dictAddOrFind() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information. */
/**返回实例对象，由此可以对字典进程添加和查找*/
dictEntry *dictAddOrFind(dict *d, void *key) 
{
    dictEntry *entry, *existing;
    entry = dictAddRaw(d,key,&existing);
    return entry ? entry : existing;
}

/* Search and remove an element. This is an helper function for
 * dictDelete() and dictUnlink(), please check the top comment
 * of those functions. */
/**删除字典元素
 * d：字典
 * key：键值
 * nofree：是佛释放
*/
static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree) 
{
    uint64_t h, idx;
    dictEntry *he;
    dictEntry *prevHe;
    int table;
    /**对于字典为空的处理
     * 返回空
    */
    if (d->ht[0].used == 0 && d->ht[1].used == 0) 
    {
        return NULL;
    }
    /**对于字典可进行散列的处理
     * 进行散列
    */
    if (dictIsRehashing(d)) 
    {
        _dictRehashStep(d);
    }
    /**根据键值获取hash值*/
    h = dictHashKey(d, key);
    /**遍历表*/
    for (table = 0; table <= 1; table++) 
    {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        prevHe = NULL;
        /**遍历表中可用实例*/
        while(he) 
        {
            /**对于键值地址相等的处理
             * 对于prevHe不为空设置prevHe->next = he->next
             * 对于prevHe为空设置此hash表指向he->next
             * 对于需要释放的设置释放此节点返回此节点的指针设置此表的字典实例减一
            */
            if (key==he->key || dictCompareKeys(d, key, he->key)) 
            {
                /* Unlink the element from the list */
                if (prevHe)
                {
                    prevHe->next = he->next;
                }
                else
                {
                    d->ht[table].table[idx] = he->next;
                }
                if (!nofree) 
                {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                    zfree(he);
                }
                d->ht[table].used--;
                return he;
            }
            prevHe = he;
            he = he->next;
        }
        if (!dictIsRehashing(d)) 
        {
            break;
        }
    }
    return NULL; /* not found */
}

/* Remove an element, returning DICT_OK on success or DICT_ERR if the
 * element was not found. */
/**删除字典元素*/
int dictDelete(dict *ht, const void *key) 
{
    return dictGenericDelete(ht,key,0) ? DICT_OK : DICT_ERR;
}

/* Remove an element from the table, but without actually releasing
 * the key, value and dictionary entry. The dictionary entry is returned
 * if the element was found (and unlinked from the table), and the user
 * should later call `dictFreeUnlinkedEntry()` with it in order to release it.
 * Otherwise if the key is not found, NULL is returned.
 *
 * This function is useful when we want to remove something from the hash
 * table but want to use its value before actually deleting the entry.
 * Without this function the pattern would require two lookups:
 *
 *  entry = dictFind(...);
 *  // Do something with entry
 *  dictDelete(dictionary,entry);
 *
 * Thanks to this function it is possible to avoid this, and use
 * instead:
 *
 * entry = dictUnlink(dictionary,entry);
 * // Do something with entry
 * dictFreeUnlinkedEntry(entry); // <- This does not need to lookup again.
 */
/**删除字典中的连接*/
dictEntry *dictUnlink(dict *ht, const void *key) 
{
    return dictGenericDelete(ht,key,1);
}

/* You need to call this function to really free the entry after a call
 * to dictUnlink(). It's safe to call this function with 'he' = NULL. */
/**释放字典中没有连接的实例*/
void dictFreeUnlinkedEntry(dict *d, dictEntry *he) 
{
    if (he == NULL) 
    {
        return;
    }
    dictFreeKey(d, he);
    dictFreeVal(d, he);
    zfree(he);
}

/* Destroy an entire dictionary */
/**清除字典*/
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) 
{
    unsigned long i;

    /* Free all the elements */
    for (i = 0; i < ht->size && ht->used > 0; i++) 
    {
        dictEntry *he, *nextHe;

        if (callback && (i & 65535) == 0) 
        {
            callback(d->privdata);
        }

        if ((he = ht->table[i]) == NULL) 
        {
            continue;
        }
        while(he) 
        {
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure */
    zfree(ht->table);
    /* Re-initialize the table */
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */
/**释放字典对象*/
void dictRelease(dict *d)
{
    _dictClear(d,&d->ht[0],NULL);
    _dictClear(d,&d->ht[1],NULL);
    zfree(d);
}
/**根据传入的键值返回可操作的实例对象*/
dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    uint64_t h, idx, table;

    if (d->ht[0].used + d->ht[1].used == 0) 
    {
        return NULL; /* dict is empty */
    }
    if (dictIsRehashing(d)) 
    {
        _dictRehashStep(d);
    }
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++) 
    {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        while(he) 
        {
            if (key==he->key || dictCompareKeys(d, key, he->key))
            {
                return he;
            }
            he = he->next;
        }
        if (!dictIsRehashing(d)) 
        {
            return NULL;
        }
    }
    return NULL;
}
/**根据键值获取字典实例的参数值*/
void *dictFetchValue(dict *d, const void *key) 
{
    dictEntry *he;

    he = dictFind(d,key);
    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */
/***/
long long dictFingerprint(dict *d) 
{
    long long integers[6], hash = 0;
    int j;
    
    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) 
    {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}
/**获取字典迭代*/
dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}
/**设置以安全模式进程迭代*/
dictIterator *dictGetSafeIterator(dict *d) 
{
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;
    return i;
}
/**返回有效的字典实例*/
dictEntry *dictNext(dictIterator *iter)
{
    while (1) 
    {
        if (iter->entry == NULL) 
        {
            dictht *ht = &iter->d->ht[iter->table];
            if (iter->index == -1 && iter->table == 0) 
            {
                if (iter->safe)
                {
                    iter->d->iterators++;
                }
                else
                {
                    iter->fingerprint = dictFingerprint(iter->d);
                }
            }
            iter->index++;
            if (iter->index >= (long) ht->size) 
            {
                if (dictIsRehashing(iter->d) && iter->table == 0) 
                {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } 
                else 
                {
                    break;
                }
            }
            iter->entry = ht->table[iter->index];
        } 
        else 
        {
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) 
        {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}
/**释放迭代器*/
void dictReleaseIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0)) 
    {
        if (iter->safe)
        {
            iter->d->iterators--;
        }
        else
        {
            assert(iter->fingerprint == dictFingerprint(iter->d));
        }
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
/***/
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned long h;
    int listlen, listele;

    if (dictSize(d) == 0) 
    {
        return NULL;
    }
    if (dictIsRehashing(d)) 
    {
        _dictRehashStep(d);
    }
    if (dictIsRehashing(d)) 
    {
        do {
            /* We are sure there are no elements in indexes from 0
             * to rehashidx-1 */
            h = d->rehashidx + (random() % (d->ht[0].size +
                                            d->ht[1].size -
                                            d->rehashidx));
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                      d->ht[0].table[h];
        } while(he == NULL);
    } 
    else 
    {
        do 
        {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    listlen = 0;
    orighe = he;
    while(he) 
    {
        he = he->next;
        listlen++;
    }
    listele = random() % listlen;
    he = orighe;
    while(listele--) 
    {
        he = he->next;
    }
    return he;
}

/* This function samples the dictionary to return a few keys from random
 * locations.
 *
 * It does not guarantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in a reasonable amount of
 * steps.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements. */
/****/
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count) 
{
    unsigned long j; /* internal hash table id, 0 or 1. */
    unsigned long tables; /* 1 or 2 tables? */
    unsigned long stored = 0, maxsizemask;
    unsigned long maxsteps;
    /**对于字典大小小于规定大小的处理
     * 设置count为字典大小
    */
    if (dictSize(d) < count) 
    {
        count = dictSize(d);
    }
    maxsteps = count*10;

    /* Try to do a rehashing work proportional to 'count'. */
    /**对于字典可以进行散列的进程散列处理*/
    for (j = 0; j < count; j++) 
    {
        if (dictIsRehashing(d))
        {
             _dictRehashStep(d);
        }
        else
        {
            break;
        }
    }
    /**获取表的数量*/
    tables = dictIsRehashing(d) ? 2 : 1;
    maxsizemask = d->ht[0].sizemask;
    /**对于表的数量大于且最大掩码小于表1的掩码的处理
     * 设置最大掩码为表1的掩码
    */
    if (tables > 1 && maxsizemask < d->ht[1].sizemask)
    {
        maxsizemask = d->ht[1].sizemask;
    }

    /* Pick a random point inside the larger table. */
    unsigned long i = random() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far. */
    while(stored < count && maxsteps--) 
    {
        for (j = 0; j < tables; j++) 
        {
            /* Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, there are no populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1. */
            if (tables == 2 && j == 0 && i < (unsigned long) d->rehashidx) 
            {
                /* Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jump if possible.
                 * (this happens when going from big to small table). */
                if (i >= d->ht[1].size)
                {
                    i = d->rehashidx;
                }
                else
                {
                    continue;
                }
            }
            if (i >= d->ht[j].size) 
            {
                continue; /* Out of range for this table. */
            }
            dictEntry *he = d->ht[j].table[i];

            /* Count contiguous empty buckets, and jump to other
             * locations if they reach 'count' (with a minimum of 5). */
            if (he == NULL) 
            {
                emptylen++;
                if (emptylen >= 5 && emptylen > count) 
                {
                    i = random() & maxsizemask;
                    emptylen = 0;
                }
            } 
            else 
            {
                emptylen = 0;
                while (he) 
                {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) 
                    {
                        return stored;
                    }
                }
            }
        }
        i = (i+1) & maxsizemask;
    }
    return stored;
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
 /**根据传入的参数v返回此参数的二进制翻转只会对为1的进行翻转
  * 比如将0000 0001 翻转为1000 0000
 */
static unsigned long rev(unsigned long v) 
{
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    /***/
    while ((s >>= 1) > 0) 
    {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * Iterating works the following way:
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 3) When the returned cursor is 0, the iteration is complete.
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 *
 * HOW IT WORKS.
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 *
 * The disadvantages resulting from this design are:
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 */
/**扫描字典
 * d:为字典
 * v:为游标值
 * fn：为扫描函数指针
 * bucketfn：为扫描篮子函数指针
 * privdata：为私有数据指针
 * 返回游标值
*/
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       dictScanBucketFunction* bucketfn,
                       void *privdata)
{
    dictht *t0;                 //hash表0的头指针
    dictht *t1;                 //
    const dictEntry *de;        //字典实例
    const dictEntry *next;      //当前字典实例的下一个字典实例
    unsigned long m0;           //hash表0的空间掩码
    unsigned long m1;           //
    /**对于字典的大小为0的处理
     * 直接返回0
    */
    if (dictSize(d) == 0) 
    {
        return 0;
    }
    /**对于字典不可重散列的处理*/
    if (!dictIsRehashing(d)) 
    {
        t0 = &(d->ht[0]);
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        /**对于bucketfn函数不为空的处理调用此函数*/
        if (bucketfn) 
        {
            bucketfn(privdata, &t0->table[v & m0]);
        }
        de = t0->table[v & m0];
        /**找到下一个可用的位置*/
        while (de) 
        {
            next = de->next;
            /**调用扫描函数*/
            fn(privdata, de);
            de = next;
        }

        /* Set unmasked bits so incrementing the reversed cursor
         * operates on the masked bits */
        v |= ~m0;

        /* Increment the reverse cursor */
        v = rev(v);
        v++;
        v = rev(v);

    }
    /**对于字典可以重散列的处理*/ 
    else 
    {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        /**对于hash表0大于hash表1的处理
         * 设置t0指向表小的，t1指向表大的
        */
        if (t0->size > t1->size) 
        {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        if (bucketfn) 
        {
            bucketfn(privdata, &t0->table[v & m0]);
        }
        de = t0->table[v & m0];
        /**获取下一个可用的字典实例位置*/
        while (de) 
        {
            next = de->next;
            fn(privdata, de);
            de = next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            if (bucketfn) 
            {
                bucketfn(privdata, &t1->table[v & m1]);
            }
            de = t1->table[v & m1];
            while (de) 
            {
                next = de->next;
                fn(privdata, de);
                de = next;
            }

            /* Increment the reverse cursor not covered by the smaller mask.*/
            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
/**判断字典是否需要扩展
 * 对需要扩展的进行扩展
*/
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return. */
    /**字典可散列直接返回成功*/
    if (dictIsRehashing(d)) 
    {
        return DICT_OK;
    }

    /* If the hash table is empty expand it to the initial size. */
    /**对于hash表的尺寸为0的处理
     * 扩展hash表为初始大小
    */
    if (d->ht[0].size == 0) 
    {
        return dictExpand(d, DICT_HT_INITIAL_SIZE);
    }

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    /**对于Hash的使用值大于hash表，或者对于可扩展的或者使用值比上hash表的大小大于预订比例的处理
     * 字典扩展为已使用的2倍
    */
    if (d->ht[0].used >= d->ht[0].size &&(dict_can_resize ||d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two */
/**返回最小的大于size的二的次方数*/
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;
    /**对传入的size的处理
     * 返回LONG的最大值加1
    */
    if (size >= LONG_MAX) 
    {
        return LONG_MAX + 1LU;
    }
    /**根据传入的size值返回比其值大的最小的2的次方数*/
    while(1) 
    {
        if (i >= size)
        {
            return i;
        }
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned
 * and the optional output parameter may be filled.
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table. */
/**根据传入的hash值
 * d:为字典
 * key：为键值指针
 * hash：为此键值的hash值
 * existing：为字典实例的二级指针
 * 返回有效的hash值或者返回-1
*/
static long _dictKeyIndex(dict *d, const void *key, uint64_t hash, dictEntry **existing)
{
    unsigned long idx, table;
    dictEntry *he;
    /**对于实例的*/
    if (existing) 
    {
        *existing = NULL;
    }

    /* Expand the hash table if needed */
    /**对于字典扩展失败的处理
     * 返回-1
    */
    if (_dictExpandIfNeeded(d) == DICT_ERR)
    {
        return -1;
    }
    /**在hash表中寻找*/
    for (table = 0; table <= 1; table++) 
    {
        /**获取有效的hash值*/
        idx = hash & d->ht[table].sizemask;
        /* Search if this slot does not already contain the given key */
        /**获取实例*/
        he = d->ht[table].table[idx];
        while(he) 
        {
            /**对于传入key的地址与字典的地址一致的处理
             * 对于existing的值不为空设置*existing为he的地址
             * 并且返回-1
            */
            if (key==he->key || dictCompareKeys(d, key, he->key)) 
            {
                if (existing) 
                {
                    *existing = he;
                }
                return -1;
            }
            /**对于地址不一致的处理寻找从此处开始为空的实例*/
            he = he->next;
        }
        /**对于字典不支持重散列退出循环*/
        if (!dictIsRehashing(d)) 
        {
            break;
        }
    }
    /**返回索引值*/
    return idx;
}
/**设置字典为空*/
void dictEmpty(dict *d, void(callback)(void*)) 
{
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}
/**设置字典可以设置大小*/
void dictEnableResize(void) 
{
    dict_can_resize = 1;
}
/**设置字典不可调整大小*/
void dictDisableResize(void) 
{
    dict_can_resize = 0;
}
/**获取此键值的hash值*/
uint64_t dictGetHash(dict *d, const void *key) 
{
    return dictHashKey(d, key);
}

/* Finds the dictEntry reference by using pointer and pre-calculated hash.
 * oldkey is a dead pointer and should not be accessed.
 * the hash value should be provided using dictGetHash.
 * no string / key comparison is performed.
 * return value is the reference to the dictEntry if found, or NULL if not found. */
/**查找字典实例使用指针和hash值
 * 返回字典实例的指针
*/
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash) 
{
    dictEntry *he, **heref;
    unsigned long idx, table;
    /**对于Hash表为空的处理*/
    if (d->ht[0].used + d->ht[1].used == 0) 
    {
        return NULL; /* dict is empty */
    }
    /**循环hash表项*/
    for (table = 0; table <= 1; table++) 
    {
        /**获取有效hash值*/
        idx = hash & d->ht[table].sizemask;
        /**获取在对应hash表的hash值的表项地址*/
        heref = &d->ht[table].table[idx];
        /**获取字典实例值*/
        he = *heref;
        while(he) 
        {
            /**寻找指针对应的字典实例的指针一致的项返回此处的指针*/
            if (oldptr==he->key)
            {
                 return heref;
            }
            heref = &he->next;
            he = *heref;
        }
        /**对于不可重散列的hash表的处理*/
        if (!dictIsRehashing(d)) 
        {
            return NULL;
        }
    }
    return NULL;
}

/* ------------------------------- Debugging ---------------------------------*/

#define DICT_STATS_VECTLEN 50
size_t _dictGetStatsHt(char *buf, size_t bufsize, dictht *ht, int tableid) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];
    size_t l = 0;

    if (ht->used == 0) {
        return snprintf(buf,bufsize,
            "No stats available for empty dictionaries\n");
    }

    /* Compute stats. */
    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }

    /* Generate human readable stats. */
    l += snprintf(buf+l,bufsize-l,
        "Hash table %d stats (%s):\n"
        " table size: %ld\n"
        " number of elements: %ld\n"
        " different slots: %ld\n"
        " max chain length: %ld\n"
        " avg chain length (counted): %.02f\n"
        " avg chain length (computed): %.02f\n"
        " Chain length distribution:\n",
        tableid, (tableid == 0) ? "main hash table" : "rehashing target",
        ht->size, ht->used, slots, maxchainlen,
        (float)totchainlen/slots, (float)ht->used/slots);

    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        if (l >= bufsize) break;
        l += snprintf(buf+l,bufsize-l,
            "   %s%ld: %ld (%.02f%%)\n",
            (i == DICT_STATS_VECTLEN-1)?">= ":"",
            i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }

    /* Unlike snprintf(), teturn the number of characters actually written. */
    if (bufsize) buf[bufsize-1] = '\0';
    return strlen(buf);
}

void dictGetStats(char *buf, size_t bufsize, dict *d) {
    size_t l;
    char *orig_buf = buf;
    size_t orig_bufsize = bufsize;

    l = _dictGetStatsHt(buf,bufsize,&d->ht[0],0);
    buf += l;
    bufsize -= l;
    if (dictIsRehashing(d) && bufsize > 0) {
        _dictGetStatsHt(buf,bufsize,&d->ht[1],1);
    }
    /* Make sure there is a NULL term at the end. */
    if (orig_bufsize) orig_buf[orig_bufsize-1] = '\0';
}

/* ------------------------------- Benchmark ---------------------------------*/

#ifdef DICT_BENCHMARK_MAIN

#include "sds.h"

uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

int compareCallback(void *privdata, const void *key1, const void *key2) {
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void freeCallback(void *privdata, void *val) {
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL
};

#define start_benchmark() start = timeInMilliseconds()
#define end_benchmark(msg) do { \
    elapsed = timeInMilliseconds()-start; \
    printf(msg ": %ld items in %lld ms\n", count, elapsed); \
} while(0);

/* dict-benchmark [count] */
int main(int argc, char **argv) {
    long j;
    long long start, elapsed;
    dict *dict = dictCreate(&BenchmarkDictType,NULL);
    long count = 0;

    if (argc == 2) {
        count = strtol(argv[1],NULL,10);
    } else {
        count = 5000000;
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        int retval = dictAdd(dict,sdsfromlonglong(j),(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Inserting");
    assert((long)dictSize(dict) == count);

    /* Wait for rehashing. */
    while (dictIsRehashing(dict)) {
        dictRehashMilliseconds(dict,100);
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Linear access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Linear access of existing elements (2nd round)");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(rand() % count);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        sdsfree(key);
    }
    end_benchmark("Random access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(rand() % count);
        key[0] = 'X';
        dictEntry *de = dictFind(dict,key);
        assert(de == NULL);
        sdsfree(key);
    }
    end_benchmark("Accessing missing");

    start_benchmark();
    for (j = 0; j < count; j++) {
        sds key = sdsfromlonglong(j);
        int retval = dictDelete(dict,key);
        assert(retval == DICT_OK);
        key[0] += 17; /* Change first number to letter. */
        retval = dictAdd(dict,key,(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Removing and adding");
}
#endif
