/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"

/* Note that these encodings are ordered, so:
 * INTSET_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64. */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/* Return the required encoding for the provided value. */
/**根据传入的参数返回编码类型*/
static uint8_t _intsetValueEncoding(int64_t v) 
{
    if (v < INT32_MIN || v > INT32_MAX)
    {
        return INTSET_ENC_INT64;
    }
    else if (v < INT16_MIN || v > INT16_MAX)
    {
        return INTSET_ENC_INT32;
    }
    else
    {
        return INTSET_ENC_INT16;
    }
}

/* Return the value at pos, given an encoding. */
/**根据传入的编码类型和位置值以及整形集合返回对应位置的值*/
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) 
{
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) 
    {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        /**获取其*/
        memrev64ifbe(&v64);
        return v64;
    } 
    else if (enc == INTSET_ENC_INT32) 
    {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } 
    else 
    {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

/* Return the value at pos, using the configured encoding. */
/**根据位置值获取此位置的数据的编码类型*/
static int64_t _intsetGet(intset *is, int pos) 
{
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

/* Set the value at pos, using the configured encoding. */
/**设置对应位置的值
 * is:为整形集合
 * pos：为位置
 * value：为参数值
*/
static void _intsetSet(intset *is, int pos, int64_t value) 
{
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) 
    {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } 
    else if (encoding == INTSET_ENC_INT32) 
    {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } 
    else 
    {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/* Create an empty intset. */
/**创建一个新的整形集合*/
intset *intsetNew(void) 
{
    intset *is = zmalloc(sizeof(intset));
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;
    return is;
}

/* Resize the intset */
/**调整整形集合的大小*/
static intset *intsetResize(intset *is, uint32_t len) 
{
    uint32_t size = len*intrev32ifbe(is->encoding);
    is = zrealloc(is,sizeof(intset)+size);
    return is;
}

/* Search for the position of "value". Return 1 when the value was found and
 * sets "pos" to the position of the value within the intset. Return 0 when
 * the value is not present in the intset and sets "pos" to the position
 * where "value" can be inserted. */
/**
 * 在整形集合中搜索
 * is:为整形集合指针
 * value:为查找的值
 * pos：为在整形集合中的位置指针
 * 返回0表示当前数值为插入，返回1表示当前数值已经插入
*/
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) 
{
    int min = 0;
    /**最大值*/
    int max = intrev32ifbe(is->length)-1;
    int mid = -1;
    int64_t cur = -1;

    /* The value can never be found when the set is empty */
    /**对于整形集合的大小为0的处理*/
    if (intrev32ifbe(is->length) == 0) 
    {
        if (pos) 
        {
            *pos = 0;
        }
        return 0;
    } 
    /**对于整形集合不为空的处理*/
    else 
    {
        /* Check for the case where we know we cannot find the value,
         * but do know the insert position. */
        /**对于值大于此集合最大值的处理
         * 这只pos为此集合的当前使用的长度
        */
        if (value > _intsetGet(is,max)) 
        {
            if (pos) 
            {
                *pos = intrev32ifbe(is->length);
            }
            return 0;
        } 
        /**对于value值小于此集合位置0的值
         * 设置pos为0 返回0
        */
        else if (value < _intsetGet(is,0)) 
        {
            if (pos) 
            {
                *pos = 0;
            }
            return 0;
        }
    }
    /**对于不符合上述问题的处理
     * 进行二分查找
    */
    while(max >= min) 
    {
        mid = ((unsigned int)min + (unsigned int)max) >> 1;
        cur = _intsetGet(is,mid);
        if (value > cur) 
        {
            min = mid+1;
        } 
        else if (value < cur) 
        {
            max = mid-1;
        } 
        else 
        {
            break;
        }
    }
    /***对于传入参数等于中间位置的值
     * 设置位置为上述得到的中间位置的位置
     * 返回1
    */
    if (value == cur) 
    {
        if (pos) 
        {
            *pos = mid;
        }
        return 1;
    } 
    /**对于传入的值不为中间位置的值的处理
     * 设置位置为当前min，返回0
    */
    else 
    {
        if (pos) 
        {
            *pos = min;
        }
        return 0;
    }
}

/* Upgrades the intset to a larger encoding and inserts the given integer. */
/**更新整形集合*/
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) 
{
    /**编码类型*/
    uint8_t curenc = intrev32ifbe(is->encoding);
    /**新的编码类型*/
    uint8_t newenc = _intsetValueEncoding(value);
    /**集合长度*/
    int length = intrev32ifbe(is->length);
    /**设置添加位置*/
    int prepend = value < 0 ? 1 : 0;

    /* First set new encoding and resize */
    is->encoding = intrev32ifbe(newenc);
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    /* Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset. */
    /**根据当前的集合的长度更新传入数据的值*/
    while(length--)
    {
         _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));
    }

    /* Set the value at the beginning or the end. */
    /**添加此值在集合前面*/
    if (prepend)
    {
        _intsetSet(is,0,value);
    }
    /**更新此值在集合的后面*/
    else
    {
        _intsetSet(is,intrev32ifbe(is->length),value);
    }
    /**更新集合长度*/
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}
/**根据传入的整形集合和源目的地址进行集合数据的从from到结尾的数据搬移*/
static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) 
{
    void *src, *dst;
    uint32_t bytes = intrev32ifbe(is->length)-from;
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) 
    {
        src = (int64_t*)is->contents+from;
        dst = (int64_t*)is->contents+to;
        bytes *= sizeof(int64_t);
    } 
    else if (encoding == INTSET_ENC_INT32) 
    {
        src = (int32_t*)is->contents+from;
        dst = (int32_t*)is->contents+to;
        bytes *= sizeof(int32_t);
    } 
    else 
    {
        src = (int16_t*)is->contents+from;
        dst = (int16_t*)is->contents+to;
        bytes *= sizeof(int16_t);
    }
    memmove(dst,src,bytes);
}

/* Insert an integer in the intset */
/**向整形集合中插入数据*/
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) 
{
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    if (success) 
    {
        *success = 1;
    }

    /* Upgrade encoding if necessary. If we need to upgrade, we know that
     * this value should be either appended (if > 0) or prepended (if < 0),
     * because it lies outside the range of existing values. */
    /**对于传入的数据编码大于当前的编码进行集合更新*/
    if (valenc > intrev32ifbe(is->encoding)) 
    {
        /* This always succeeds, so we don't need to curry *success. */
        return intsetUpgradeAndAdd(is,value);
    } 
    /**对于当前编码不小于传入的参数的编码方式的处理*/
    else 
    {
        /* Abort if the value is already present in the set.
         * This call will populate "pos" with the right position to insert
         * the value when it cannot be found. */
        /**找到此值*/
        if (intsetSearch(is,value,&pos)) 
        {
            if (success) 
            {
                *success = 0;
            }
            return is;
        }
        /**更新集合大小*/
        is = intsetResize(is,intrev32ifbe(is->length)+1);
        /**对于得到的位置值大于当前集合长度的处理
         * 将此位置的值向后推移一个
        */
        if (pos < intrev32ifbe(is->length)) 
        {
            intsetMoveTail(is,pos,pos+1);
        }
    }
    /**填充当前值，更新集合长度*/
    _intsetSet(is,pos,value);
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}

/* Delete integer from intset */
/**从集合删除数据*/
intset *intsetRemove(intset *is, int64_t value, int *success) 
{
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    if (success) 
    {
        *success = 0;
    }
    /**对于值满足当前的类型范围且查找到此值的处理
     * 将此位置后面的值向前移动一位更新长度返回此集合
    */
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,&pos)) 
    {
        uint32_t len = intrev32ifbe(is->length);

        /* We know we can delete */
        if (success) 
        {
            *success = 1;
        }

        /* Overwrite value with tail and update length */
        if (pos < (len-1)) 
        {
            intsetMoveTail(is,pos+1,pos);
        }
        is = intsetResize(is,len-1);
        is->length = intrev32ifbe(len-1);
    }
    return is;
}

/* Determine whether a value belongs to this set */
/**从集合中查找值*/
uint8_t intsetFind(intset *is, int64_t value) 
{
    uint8_t valenc = _intsetValueEncoding(value);
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,NULL);
}

/* Return random member */
/**从集合的随机位置获取一个值*/
int64_t intsetRandom(intset *is) 
{
    return _intsetGet(is,rand()%intrev32ifbe(is->length));
}

/* Get the value at the given position. When this position is
 * out of range the function returns 0, when in range it returns 1. */
/**获取集合指定位置的值*/
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) 
{
    if (pos < intrev32ifbe(is->length)) 
    {
        *value = _intsetGet(is,pos);
        return 1;
    }
    return 0;
}

/* Return intset length */
/**返回集合的当前长度*/
uint32_t intsetLen(const intset *is) 
{
    return intrev32ifbe(is->length);
}

/* Return intset blob size in bytes. */
size_t intsetBlobLen(intset *is) 
{
    return sizeof(intset)+intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

#ifdef REDIS_TEST
#include <sys/time.h>
#include <time.h>

#if 0
static void intsetRepr(intset *is) {
    for (uint32_t i = 0; i < intrev32ifbe(is->length); i++) {
        printf("%lld\n", (uint64_t)_intsetGet(is,i));
    }
    printf("\n");
}

static void error(char *err) {
    printf("%s\n", err);
    exit(1);
}
#endif

static void ok(void) {
    printf("OK\n");
}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

#define assert(_e) ((_e)?(void)0:(_assert(#_e,__FILE__,__LINE__),exit(1)))
static void _assert(char *estr, char *file, int line) {
    printf("\n\n=== ASSERTION FAILED ===\n");
    printf("==> %s:%d '%s' is not true\n",file,line,estr);
}

static intset *createSet(int bits, int size) {
    uint64_t mask = (1<<bits)-1;
    uint64_t value;
    intset *is = intsetNew();

    for (int i = 0; i < size; i++) {
        if (bits > 32) {
            value = (rand()*rand()) & mask;
        } else {
            value = rand() & mask;
        }
        is = intsetAdd(is,value,NULL);
    }
    return is;
}

static void checkConsistency(intset *is) {
    for (uint32_t i = 0; i < (intrev32ifbe(is->length)-1); i++) {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16) {
            int16_t *i16 = (int16_t*)is->contents;
            assert(i16[i] < i16[i+1]);
        } else if (encoding == INTSET_ENC_INT32) {
            int32_t *i32 = (int32_t*)is->contents;
            assert(i32[i] < i32[i+1]);
        } else {
            int64_t *i64 = (int64_t*)is->contents;
            assert(i64[i] < i64[i+1]);
        }
    }
}

#define UNUSED(x) (void)(x)
int intsetTest(int argc, char **argv) {
    uint8_t success;
    int i;
    intset *is;
    srand(time(NULL));

    UNUSED(argc);
    UNUSED(argv);

    printf("Value encodings: "); {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) ==
                    INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) ==
                    INTSET_ENC_INT64);
        ok();
    }

    printf("Basic adding: "); {
        is = intsetNew();
        is = intsetAdd(is,5,&success); assert(success);
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
    }

    printf("Large number of random adds: "); {
        uint32_t inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++) {
            is = intsetAdd(is,rand()%0x800,&success);
            if (success) inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int32: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,65535));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-65535));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int16 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Upgrade from int32 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);

        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
    }

    printf("Stress lookups: "); {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits,size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
        printf("%ld lookups, %ld element set, %lldusec\n",
               num,size,usec()-start);
    }

    printf("Stress add+delete: "); {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++) {
            v1 = rand() % 0xfff;
            is = intsetAdd(is,v1,NULL);
            assert(intsetFind(is,v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is,v2,NULL);
            assert(!intsetFind(is,v2));
        }
        checkConsistency(is);
        ok();
    }

    return 0;
}
#endif
