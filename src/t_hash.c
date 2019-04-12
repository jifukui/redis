/*
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

#include "server.h"
#include <math.h>

/*-----------------------------------------------------------------------------
 * Hash type API
 *----------------------------------------------------------------------------*/

/* Check the length of a number of objects to see if we need to convert a
 * ziplist to a real hash. Note that we only check string encoded objects
 * as their string length can be queried in constant time. */
/***/
void hashTypeTryConversion(robj *o, robj **argv, int start, int end) 
{
    int i;
    /**对于数据库对象的类型不是压缩列表的处理
     * 返回*/
    if (o->encoding != OBJ_ENCODING_ZIPLIST) 
    {
        return;
    }
    /**对于数据库对象是压缩列表的处理*/
    for (i = start; i <= end; i++) 
    {
        /**对于数据库对象argv的为原始编码或是是嵌入编码的处理
         * 进行转换
        */
        if (sdsEncodedObject(argv[i]) &&sdslen(argv[i]->ptr) > server.hash_max_ziplist_value)
        {
            hashTypeConvert(o, OBJ_ENCODING_HT);
            break;
        }
    }
}

/* Get the value from a ziplist encoded hash, identified by field.
 * Returns -1 when the field cannot be found. */
/**从压缩列表获取hash类型
 * o:数据库对象指针
 * field：域名
 * vstr：对应域名的字符串数据
 * vlen：对应域名的字符串长度
 * vll：对应域名的数据值
 * 这个函数的功能是从数据库中提取对用域的数据
*/
int hashTypeGetFromZiplist(robj *o, 
                            sds field,
                           unsigned char **vstr,
                           unsigned int *vlen,
                           long long *vll)
{
    unsigned char *zl;          //  
    unsigned char *fptr = NULL;
    unsigned char *vptr = NULL;
    int ret;
    /**断言处理数据库对象的编码类型是否是压缩列表*/
    serverAssert(o->encoding == OBJ_ENCODING_ZIPLIST);
    /**zl指向数据部分*/
    zl = o->ptr;
    /**指向压缩列表的第一个实例*/
    fptr = ziplistIndex(zl, ZIPLIST_HEAD);
    /**找到第一个实例的处理*/
    if (fptr != NULL) 
    {
        /**从压缩列表中获取指定域*/
        fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
        /**成功找到指定域*/
        if (fptr != NULL) 
        {
            /* Grab pointer to the value (fptr points to the field) */
            /**指向值域，获取值域的位置*/
            vptr = ziplistNext(zl, fptr);
            serverAssert(vptr != NULL);
        }
    }

    if (vptr != NULL) 
    {
        /**获取内容*/
        ret = ziplistGet(vptr, vstr, vlen, vll);
        serverAssert(ret);
        return 0;
    }

    return -1;
}

/* Get the value from a hash table encoded hash, identified by field.
 * Returns NULL when the field cannot be found, otherwise the SDS value
 * is returned. */
/**从数据库中获取对应域的字典实例*/
sds hashTypeGetFromHashTable(robj *o, sds field) 
{
    dictEntry *de;

    serverAssert(o->encoding == OBJ_ENCODING_HT);
    /**从字典获取对应域*/
    de = dictFind(o->ptr, field);
    if (de == NULL) 
    {
        return NULL;
    }
    return dictGetVal(de);
}

/* Higher level function of hashTypeGet*() that returns the hash value
 * associated with the specified field. If the field is found C_OK
 * is returned, otherwise C_ERR. The returned object is returned by
 * reference in either *vstr and *vlen if it's returned in string form,
 * or stored in *vll if it's returned as a number.
 *
 * If *vll is populated *vstr is set to NULL, so the caller
 * can always check the function return by checking the return value
 * for C_OK and checking if vll (or vstr) is NULL. */
/**从数据库中获取对应域的数据针对于压缩列表和hash表*/
int hashTypeGetValue(robj *o, sds field, unsigned char **vstr, unsigned int *vlen, long long *vll) 
{
    /**对于数据库的编码类型是压缩列表的处理*/
    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        *vstr = NULL;
        /**从压缩列表中提取对于的数据*/
        if (hashTypeGetFromZiplist(o, field, vstr, vlen, vll) == 0)
        {
            return C_OK;
        }
    } 
    /**对于数据库的编码类型是hash表的处理*/
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        sds value;
        /**获取对应域名的字典实例*/
        if ((value = hashTypeGetFromHashTable(o, field)) != NULL) 
        {
            *vstr = (unsigned char*) value;
            *vlen = sdslen(value);
            return C_OK;
        }
    } 
    /**对于数据类型是其他类型的处理*/
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return C_ERR;
}

/* Like hashTypeGetValue() but returns a Redis object, which is useful for
 * interaction with the hash type outside t_hash.c.
 * The function returns NULL if the field is not found in the hash. Otherwise
 * a newly allocated string object with the value is returned. */
/**获取对应域的值，但是返回的是redis定义的对象*/
robj *hashTypeGetValueObject(robj *o, sds field) 
{
    unsigned char *vstr;
    unsigned int vlen;
    long long vll;
    /**获取对应域的值*/
    if (hashTypeGetValue(o,field,&vstr,&vlen,&vll) == C_ERR) 
    {
        return NULL;
    }
    /**字符串的处理
     * 将字符串创建为字符串对象并返回此对象
    */
    if (vstr) 
    {
        return createStringObject((char*)vstr,vlen);
    }
    /**长整型数据的处理
     * 创建long long类型的字符串对象
    */
    else 
    {
        return createStringObjectFromLongLong(vll);
    }
}

/* Higher level function using hashTypeGet*() to return the length of the
 * object associated with the requested field, or 0 if the field does not
 * exist. */
/**返回对应域的值的长度字节数*/
size_t hashTypeGetValueLength(robj *o, sds field) 
{
    size_t len = 0;
    /**对于压缩列表的处理*/
    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0)
        {
            len = vstr ? vlen : sdigits10(vll);
        }
    } 
    /**对于hash表的处理*/
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        sds aux;

        if ((aux = hashTypeGetFromHashTable(o, field)) != NULL)
        {
            len = sdslen(aux);
        }
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return len;
}

/* Test if the specified field exists in the given hash. Returns 1 if the field
 * exists, and 0 when it doesn't. */
/**判断此域是否存在
 * 返回1表示存在
 * 返回0表示不存在
*/
int hashTypeExists(robj *o, sds field) 
{
    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0) 
        {
            return 1;
        }
    } 
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        if (hashTypeGetFromHashTable(o, field) != NULL) 
        {
            return 1;
        }
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return 0;
}

/* Add a new field, overwrite the old with the new value if it already exists.
 * Return 0 on insert and 1 on update.
 *
 * By default, the key and value SDS strings are copied if needed, so the
 * caller retains ownership of the strings passed. However this behavior
 * can be effected by passing appropriate flags (possibly bitwise OR-ed):
 *
 * HASH_SET_TAKE_FIELD -- The SDS field ownership passes to the function.
 * HASH_SET_TAKE_VALUE -- The SDS value ownership passes to the function.
 *
 * When the flags are used the caller does not need to release the passed
 * SDS string(s). It's up to the function to use the string to create a new
 * entry or to free the SDS string before returning to the caller.
 *
 * HASH_SET_COPY corresponds to no flags passed, and means the default
 * semantics of copying the values if needed.
 *
 */
#define HASH_SET_TAKE_FIELD (1<<0)
#define HASH_SET_TAKE_VALUE (1<<1)
#define HASH_SET_COPY 0
/**设置对应域的值
 * 如果存在此域的值进行修改
 * 如果不存在则添加此域和此域的值
*/
int hashTypeSet(robj *o, sds field, sds value, int flags) 
{
    int update = 0;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *zl, *fptr, *vptr;

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) 
        {
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            /**找到的处理*/
            if (fptr != NULL) 
            {
                /* Grab pointer to the value (fptr points to the field) */
                /**获取下一个实例*/
                vptr = ziplistNext(zl, fptr);
                serverAssert(vptr != NULL);
                update = 1;

                /* Delete value */
                /**删除此值*/
                zl = ziplistDelete(zl, &vptr);

                /* Insert new value */
                /**此处插入新值*/
                zl = ziplistInsert(zl, vptr, (unsigned char*)value,sdslen(value));
            }
        }
        /**如果下一个实例不存在在压缩列表的尾处添加此域和此域的值*/
        if (!update) 
        {
            /* Push new field/value pair onto the tail of the ziplist */
            zl = ziplistPush(zl, (unsigned char*)field, sdslen(field),ZIPLIST_TAIL);
            zl = ziplistPush(zl, (unsigned char*)value, sdslen(value),ZIPLIST_TAIL);
        }
        /**设置对象指针指向此指针*/
        o->ptr = zl;

        /* Check if the ziplist needs to be converted to a hash table */
        /**对于长度大于设置的压缩列表的最大长条目的处理设置转换为hash表*/
        if (hashTypeLength(o) > server.hash_max_ziplist_entries)
        {
            hashTypeConvert(o, OBJ_ENCODING_HT);
        }
    } 
    /**hash表的处理*/
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        dictEntry *de = dictFind(o->ptr,field);
        /**对于此域存在的处理*/
        if (de) 
        {
            /**释放此域的值*/
            sdsfree(dictGetVal(de));
            if (flags & HASH_SET_TAKE_VALUE) 
            {
                /*设置此域的值*/
                dictGetVal(de) = value;
                value = NULL;
            } 
            else 
            {
                dictGetVal(de) = sdsdup(value);
            }
            update = 1;
        } 
        else 
        {
            sds f,v;
            if (flags & HASH_SET_TAKE_FIELD) 
            {
                f = field;
                field = NULL;
            } 
            else 
            {
                f = sdsdup(field);
            }
            if (flags & HASH_SET_TAKE_VALUE) 
            {
                v = value;
                value = NULL;
            } 
            else 
            {
                v = sdsdup(value);
            }
            dictAdd(o->ptr,f,v);
        }
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }

    /* Free SDS strings we did not referenced elsewhere if the flags
     * want this function to be responsible. */
    if (flags & HASH_SET_TAKE_FIELD && field) 
    {
        sdsfree(field);
    }
    if (flags & HASH_SET_TAKE_VALUE && value) 
    {
        sdsfree(value);
    }
    return update;
}

/* Delete an element from a hash.
 * Return 1 on deleted and 0 on not found. */
/**删除对应域的值*/
int hashTypeDelete(robj *o, sds field) 
{
    int deleted = 0;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *zl, *fptr;

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) 
        {
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) 
            {
                zl = ziplistDelete(zl,&fptr); /* Delete the key. */
                zl = ziplistDelete(zl,&fptr); /* Delete the value. */
                o->ptr = zl;
                deleted = 1;
            }
        }
    } 
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        if (dictDelete((dict*)o->ptr, field) == C_OK) 
        {
            deleted = 1;

            /* Always check if the dictionary needs a resize after a delete. */
            if (htNeedsResize(o->ptr)) 
            {
                dictResize(o->ptr);
            }
        }

    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return deleted;
}

/* Return the number of elements in a hash. */
/**返回此对象的长度*/
unsigned long hashTypeLength(const robj *o) 
{
    unsigned long length = ULONG_MAX;
    /**编码类型为压缩列表的处理
     * 设置长度为此指针指向的字符串的一半
    */
    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        length = ziplistLen(o->ptr) / 2;
    } 
    /**对于是hash表*/
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        length = dictSize((const dict*)o->ptr);
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return length;
}
/***hash类型初始化迭代器*/
hashTypeIterator *hashTypeInitIterator(robj *subject) 
{
    /**为hash迭代器分配内存空间*/
    hashTypeIterator *hi = zmalloc(sizeof(hashTypeIterator));
    hi->subject = subject;
    hi->encoding = subject->encoding;
    /**对于迭代器的编码类型为压缩列表的处理
     * 
    */
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        hi->fptr = NULL;
        hi->vptr = NULL;
    } 
    /**对于迭代器的编码类型为HT的处理*/
    else if (hi->encoding == OBJ_ENCODING_HT) 
    {
        hi->di = dictGetIterator(subject->ptr);
    }
    /***对于其他处理*/
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    /**返回迭代器对象*/
    return hi;
}
/**释放hash迭代器对象*/
void hashTypeReleaseIterator(hashTypeIterator *hi) 
{
    if (hi->encoding == OBJ_ENCODING_HT)
    {
        dictReleaseIterator(hi->di);
    }
    zfree(hi);
}

/* Move to the next entry in the hash. Return C_OK when the next entry
 * could be found and C_ERR when the iterator reaches the end. */
/**根据迭代器设置下一个操作对象
 * 返回-1表示失败
 * 返回0表示成功
*/
int hashTypeNext(hashTypeIterator *hi) 
{
    /**对于压缩列表的处理*/
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *zl;
        unsigned char *fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;
        /***/
        if (fptr == NULL) 
        {
            /* Initialize cursor */
            serverAssert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);
        } 
        else 
        {
            /* Advance cursor */
            serverAssert(vptr != NULL);
            fptr = ziplistNext(zl, vptr);
        }
        if (fptr == NULL) 
        {
            return C_ERR;
        }

        /* Grab pointer to the value (fptr points to the field) */
        vptr = ziplistNext(zl, fptr);
        serverAssert(vptr != NULL);

        /* fptr, vptr now point to the first or next pair */
        hi->fptr = fptr;
        hi->vptr = vptr;
    } 
    else if (hi->encoding == OBJ_ENCODING_HT) 
    {
        if ((hi->de = dictNext(hi->di)) == NULL) 
        {
            return C_ERR;
        }
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
    return C_OK;
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a ziplist. Prototype is similar to `hashTypeGetFromZiplist`. */
/**获取当前指向的的值*/
void hashTypeCurrentFromZiplist(hashTypeIterator *hi, 
                                int what,
                                unsigned char **vstr,
                                unsigned int *vlen,
                                long long *vll)
{
    int ret;

    serverAssert(hi->encoding == OBJ_ENCODING_ZIPLIST);

    if (what & OBJ_HASH_KEY) 
    {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        serverAssert(ret);
    } 
    else 
    {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        serverAssert(ret);
    }
}

/* Get the field or value at iterator cursor, for an iterator on a hash value
 * encoded as a hash table. Prototype is similar to
 * `hashTypeGetFromHashTable`. */
/**获取当前指针指向的值*/
sds hashTypeCurrentFromHashTable(hashTypeIterator *hi, int what) 
{
    serverAssert(hi->encoding == OBJ_ENCODING_HT);

    if (what & OBJ_HASH_KEY) 
    {
        return dictGetKey(hi->de);
    } 
    else 
    {
        return dictGetVal(hi->de);
    }
}

/* Higher level function of hashTypeCurrent*() that returns the hash value
 * at current iterator position.
 *
 * The returned element is returned by reference in either *vstr and *vlen if
 * it's returned in string form, or stored in *vll if it's returned as
 * a number.
 *
 * If *vll is populated *vstr is set to NULL, so the caller
 * can always check the function return by checking the return value
 * type checking if vstr == NULL. */
/**获取当前指针指向的值*/
void hashTypeCurrentObject(hashTypeIterator *hi, int what, unsigned char **vstr, unsigned int *vlen, long long *vll) 
{
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        *vstr = NULL;
        hashTypeCurrentFromZiplist(hi, what, vstr, vlen, vll);
    } 
    else if (hi->encoding == OBJ_ENCODING_HT) 
    {
        sds ele = hashTypeCurrentFromHashTable(hi, what);
        *vstr = (unsigned char*) ele;
        *vlen = sdslen(ele);
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
}

/* Return the key or value at the current iterator position as a new
 * SDS string. */
/**获取当前指针指向的值
 * 对于是字符串返回字符串长度
 * 对于数值返回数值占用的长度
*/
sds hashTypeCurrentObjectNewSds(hashTypeIterator *hi, int what) 
{
    unsigned char *vstr;
    unsigned int vlen;
    long long vll;

    hashTypeCurrentObject(hi,what,&vstr,&vlen,&vll);
    if (vstr) 
    {
        return sdsnewlen(vstr,vlen);
    }
    return sdsfromlonglong(vll);
}
/***/
robj *hashTypeLookupWriteOrCreate(client *c, robj *key) 
{
    robj *o = lookupKeyWrite(c->db,key);
    if (o == NULL) 
    {
        o = createHashObject();
        dbAdd(c->db,key,o);
    } 
    else 
    {
        if (o->type != OBJ_HASH) 
        {
            addReply(c,shared.wrongtypeerr);
            return NULL;
        }
    }
    return o;
}

/**将数据库对象转换为指定类型*/
void hashTypeConvertZiplist(robj *o, int enc) 
{
    /**对于数据库编码类型是不是压缩列的处理
     * 不是退出
     * 是输出断言
    */
    serverAssert(o->encoding == OBJ_ENCODING_ZIPLIST);
    /**对于编码类型是压缩编码的处理
     * 什么也不做
    */
    if (enc == OBJ_ENCODING_ZIPLIST) 
    {
        /* Nothing to do... */

    } 
    /**对于编码类型是HT的处理*/
    else if (enc == OBJ_ENCODING_HT) 
    {
        hashTypeIterator *hi;
        dict *dict;
        int ret;

        hi = hashTypeInitIterator(o);
        dict = dictCreate(&hashDictType, NULL);

        while (hashTypeNext(hi) != C_ERR) 
        {
            sds key, value;

            key = hashTypeCurrentObjectNewSds(hi,OBJ_HASH_KEY);
            value = hashTypeCurrentObjectNewSds(hi,OBJ_HASH_VALUE);
            ret = dictAdd(dict, key, value);
            if (ret != DICT_OK) 
            {
                serverLogHexDump(LL_WARNING,"ziplist with dup elements dump",
                    o->ptr,ziplistBlobLen(o->ptr));
                serverPanic("Ziplist corruption detected");
            }
        }
        hashTypeReleaseIterator(hi);
        zfree(o->ptr);
        o->encoding = OBJ_ENCODING_HT;
        o->ptr = dict;
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
}

/**hash的类型转换*/
void hashTypeConvert(robj *o, int enc) 
{
    /**对于数据库类型是压缩列表的处理
     * 转换为压缩列表
    */
    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        hashTypeConvertZiplist(o, enc);
    } 
    /**对于其他的处理报错*/
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        serverPanic("Not implemented");
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
}

/*-----------------------------------------------------------------------------
 * Hash type commands
 *----------------------------------------------------------------------------*/

void hsetnxCommand(client *c) 
{
    robj *o;
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) 
    {
        return;
    }
    hashTypeTryConversion(o,c->argv,2,3);

    if (hashTypeExists(o, c->argv[2]->ptr)) 
    {
        addReply(c, shared.czero);
    } 
    else 
    {
        hashTypeSet(o,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_COPY);
        addReply(c, shared.cone);
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH,"hset",c->argv[1],c->db->id);
        server.dirty++;
    }
}

void hsetCommand(client *c) {
    int i, created = 0;
    robj *o;

    if ((c->argc % 2) == 1) 
    {
        addReplyError(c,"wrong number of arguments for HMSET");
        return;
    }

    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) 
    {
        return;
    }
    hashTypeTryConversion(o,c->argv,2,c->argc-1);

    for (i = 2; i < c->argc; i += 2)
    {
        created += !hashTypeSet(o,c->argv[i]->ptr,c->argv[i+1]->ptr,HASH_SET_COPY);
    }

    /* HMSET (deprecated) and HSET return value is different. */
    char *cmdname = c->argv[0]->ptr;
    if (cmdname[1] == 's' || cmdname[1] == 'S') 
    {
        /* HSET */
        addReplyLongLong(c, created);
    } 
    else 
    {
        /* HMSET */
        addReply(c, shared.ok);
    }
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"hset",c->argv[1],c->db->id);
    server.dirty++;
}

void hincrbyCommand(client *c) 
{
    long long value, incr, oldvalue;
    robj *o;
    sds new;
    unsigned char *vstr;
    unsigned int vlen;

    if (getLongLongFromObjectOrReply(c,c->argv[3],&incr,NULL) != C_OK) 
    {
        return;
    }
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) 
    {
        return;
    }
    if (hashTypeGetValue(o,c->argv[2]->ptr,&vstr,&vlen,&value) == C_OK) 
    {
        if (vstr) 
        {
            if (string2ll((char*)vstr,vlen,&value) == 0) 
            {
                addReplyError(c,"hash value is not an integer");
                return;
            }
        } /* Else hashTypeGetValue() already stored it into &value */
    } 
    else 
    {
        value = 0;
    }

    oldvalue = value;
    if ((incr < 0 && oldvalue < 0 && incr < (LLONG_MIN-oldvalue)) ||
        (incr > 0 && oldvalue > 0 && incr > (LLONG_MAX-oldvalue))) 
    {
        addReplyError(c,"increment or decrement would overflow");
        return;
    }
    value += incr;
    new = sdsfromlonglong(value);
    hashTypeSet(o,c->argv[2]->ptr,new,HASH_SET_TAKE_VALUE);
    addReplyLongLong(c,value);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"hincrby",c->argv[1],c->db->id);
    server.dirty++;
}

void hincrbyfloatCommand(client *c) 
{
    long double value, incr;
    long long ll;
    robj *o;
    sds new;
    unsigned char *vstr;
    unsigned int vlen;

    if (getLongDoubleFromObjectOrReply(c,c->argv[3],&incr,NULL) != C_OK) 
    {
        return;
    }
    if ((o = hashTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) 
    {
        return;
    }
    if (hashTypeGetValue(o,c->argv[2]->ptr,&vstr,&vlen,&ll) == C_OK) 
    {
        if (vstr) 
        {
            if (string2ld((char*)vstr,vlen,&value) == 0) 
            {
                addReplyError(c,"hash value is not a float");
                return;
            }
        } 
        else 
        {
            value = (long double)ll;
        }
    } 
    else 
    {
        value = 0;
    }

    value += incr;

    char buf[MAX_LONG_DOUBLE_CHARS];
    int len = ld2string(buf,sizeof(buf),value,1);
    new = sdsnewlen(buf,len);
    hashTypeSet(o,c->argv[2]->ptr,new,HASH_SET_TAKE_VALUE);
    addReplyBulkCBuffer(c,buf,len);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"hincrbyfloat",c->argv[1],c->db->id);
    server.dirty++;

    /* Always replicate HINCRBYFLOAT as an HSET command with the final value
     * in order to make sure that differences in float pricision or formatting
     * will not create differences in replicas or after an AOF restart. */
    robj *aux, *newobj;
    aux = createStringObject("HSET",4);
    newobj = createRawStringObject(buf,len);
    rewriteClientCommandArgument(c,0,aux);
    decrRefCount(aux);
    rewriteClientCommandArgument(c,3,newobj);
    decrRefCount(newobj);
}

static void addHashFieldToReply(client *c, robj *o, sds field) 
{
    int ret;

    if (o == NULL) 
    {
        addReply(c, shared.nullbulk);
        return;
    }

    if (o->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        ret = hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll);
        if (ret < 0) 
        {
            addReply(c, shared.nullbulk);
        } 
        else 
        {
            if (vstr) 
            {
                addReplyBulkCBuffer(c, vstr, vlen);
            } 
            else 
            {
                addReplyBulkLongLong(c, vll);
            }
        }

    } 
    else if (o->encoding == OBJ_ENCODING_HT) 
    {
        sds value = hashTypeGetFromHashTable(o, field);
        if (value == NULL)
        {
            addReply(c, shared.nullbulk);
        }
        else
        {
            addReplyBulkCBuffer(c, value, sdslen(value));
        }
    } 
    else
    {
        serverPanic("Unknown hash encoding");
    }
}

void hgetCommand(client *c) 
{
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }

    addHashFieldToReply(c, o, c->argv[2]->ptr);
}

void hmgetCommand(client *c) 
{
    robj *o;
    int i;

    /* Don't abort when the key cannot be found. Non-existing keys are empty
     * hashes, where HMGET should respond with a series of null bulks. */
    o = lookupKeyRead(c->db, c->argv[1]);
    if (o != NULL && o->type != OBJ_HASH) 
    {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    addReplyMultiBulkLen(c, c->argc-2);
    for (i = 2; i < c->argc; i++) 
    {
        addHashFieldToReply(c, o, c->argv[i]->ptr);
    }
}

void hdelCommand(client *c) 
{
    robj *o;
    int j, deleted = 0, keyremoved = 0;

    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }

    for (j = 2; j < c->argc; j++) 
    {
        if (hashTypeDelete(o,c->argv[j]->ptr)) 
        {
            deleted++;
            if (hashTypeLength(o) == 0) 
            {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }
    if (deleted) 
    {
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH,"hdel",c->argv[1],c->db->id);
        if (keyremoved)
        {
            notifyKeyspaceEvent(NOTIFY_GENERIC,"del",c->argv[1],c->db->id);
        }
        server.dirty += deleted;
    }
    addReplyLongLong(c,deleted);
}

void hlenCommand(client *c) 
{
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }

    addReplyLongLong(c,hashTypeLength(o));
}

void hstrlenCommand(client *c) 
{
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }
    addReplyLongLong(c,hashTypeGetValueLength(o,c->argv[2]->ptr));
}

static void addHashIteratorCursorToReply(client *c, hashTypeIterator *hi, int what) 
{
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) 
    {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);
        if (vstr)
        {
            addReplyBulkCBuffer(c, vstr, vlen);
        }
        else
        {
            addReplyBulkLongLong(c, vll);
        }
    } 
    else if (hi->encoding == OBJ_ENCODING_HT) 
    {
        sds value = hashTypeCurrentFromHashTable(hi, what);
        addReplyBulkCBuffer(c, value, sdslen(value));
    } 
    else 
    {
        serverPanic("Unknown hash encoding");
    }
}

void genericHgetallCommand(client *c, int flags) 
{
    robj *o;
    hashTypeIterator *hi;
    int multiplier = 0;
    int length, count = 0;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL|| checkType(c,o,OBJ_HASH)) 
    {
        return;
    }

    if (flags & OBJ_HASH_KEY) 
    {
        multiplier++;
    }
    if (flags & OBJ_HASH_VALUE) 
    {
        multiplier++;
    }

    length = hashTypeLength(o) * multiplier;
    addReplyMultiBulkLen(c, length);

    hi = hashTypeInitIterator(o);
    while (hashTypeNext(hi) != C_ERR) 
    {
        if (flags & OBJ_HASH_KEY) 
        {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_KEY);
            count++;
        }
        if (flags & OBJ_HASH_VALUE) 
        {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_VALUE);
            count++;
        }
    }

    hashTypeReleaseIterator(hi);
    serverAssert(count == length);
}

void hkeysCommand(client *c) 
{
    genericHgetallCommand(c,OBJ_HASH_KEY);
}

void hvalsCommand(client *c) 
{
    genericHgetallCommand(c,OBJ_HASH_VALUE);
}

void hgetallCommand(client *c) 
{
    genericHgetallCommand(c,OBJ_HASH_KEY|OBJ_HASH_VALUE);
}

void hexistsCommand(client *c) 
{
    robj *o;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }

    addReply(c, hashTypeExists(o,c->argv[2]->ptr) ? shared.cone : shared.czero);
}

void hscanCommand(client *c) 
{
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == C_ERR) 
    {
        return;
    }
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||checkType(c,o,OBJ_HASH)) 
    {
        return;
    }
    scanGenericCommand(c,o,cursor);
}
