
/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is ArsDigita code and related documentation
 * distributed by ArsDigita.
 * 
 * The Initial Developer of the Original Code is ArsDigita.,
 * Portions created by ArsDigita are Copyright (C) 1999 ArsDigita.
 * All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */

/*
 *   Added ns_cache incr command, Vlad Seryakov vlad@crystalballinc.com
 *
 *   2003-03-13: Don Baccus dhogaza@pacifier.com
 *               Added virtual server support and cleaned up warnings
 *
 *   2003-03-16: Zoran Vasiljevic zoran@archiware.com
 *               Added support for process-wide caches
 */

/* 
 * tclcache.c --
 *
 *	Tcl API for cache.c.  Only works in nsd8x and AOLserver 4x and better.
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#ifndef USE_TCL8X
#define USE_TCL8X
#endif

#include "ns.h"

typedef int TclCacheCmdProc(Ns_Cache *cache, int needsLocking,
    Tcl_Interp *interp, int objc, Tcl_Obj * CONST objv[]);

typedef struct {
    Ns_Cache *nscache;

    TclCacheCmdProc *evalPtr;
    TclCacheCmdProc *flushPtr;
    TclCacheCmdProc *getPtr;
    TclCacheCmdProc *namesPtr;
    TclCacheCmdProc *setPtr;
    TclCacheCmdProc *incrPtr;
} TclCache;

typedef struct {

    /* If it's a global cache... */
    TclCache *globalCache;

    /* If globalCache == NULL */
    Ns_Tls tls;       /* the tls value is a TclCache */
    int    maxSize;   /* use when creating a new thread-private cache */

} TclCacheInfo;

typedef struct {

    int length;
    char *value;

    /*
     * If value == NULL, then some thread is computing the value.
     * This flag tells that thread that the entry has been flushed,
     * so it should not store the value in the cache.
     */
    int flushed;

} GlobalValue;

/* Each virtual server gets its own cache and lock */

typedef struct {
    Tcl_HashTable tclCaches;
    Ns_Mutex lock;
} Server;

/* Global symbols */

int Ns_ModuleVersion = 1;

/* Common cache, used for all virtual servers */
static Server *commonCache;

int Ns_ModuleInit(char *server, char *module);

/* Private symbols */

static int CacheInterpInit(Tcl_Interp *interp, void *context);
static Tcl_ObjCmdProc NsTclCacheCmd;

static TclCache *TclCacheFind(Server *servPtr, char *name, int *needsLockingPtr);
static TclCache *CacheFind(Server *servPtr, char *name, int *needsLockingPtr);
static TclCache *GetThreadCache(char *name, TclCacheInfo *info);

static int CreateCmd(Tcl_Interp *interp, Server *servPtr, int objc, Tcl_Obj * CONST objv[]);
static int CreateThreadCache(Tcl_Interp *interp, Server *servPtr, char *name, int maxSize);
static int CreateGlobalCache(Tcl_Interp *interp, Server *servPtr, char *name,
    int maxSize, int timeout);

static TclCacheCmdProc ThreadCacheEvalCmd;
static TclCacheCmdProc ThreadCacheGetCmd;
static TclCacheCmdProc ThreadCacheSetCmd;
static TclCacheCmdProc ThreadCacheIncrCmd;

static TclCacheCmdProc GlobalCacheEvalCmd;
static TclCacheCmdProc GlobalCacheGetCmd;
static TclCacheCmdProc GlobalCacheSetCmd;
static TclCacheCmdProc GlobalCacheIncrCmd;

static TclCacheCmdProc FlushCmd;
static TclCacheCmdProc NamesCmd;

static Ns_Callback ThreadValueFree;
static Ns_Callback GlobalValueFree;

static Ns_TlsCleanup CleanupThreadCache;

static Ns_Entry *GetGlobalEntry(Ns_Cache *cache, char *key, int create);
static int CompleteEntryP(Ns_Entry *entry);


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Initialize this module.
 *
 * Results:
 *	Standard AOLserver return code..
 *
 * Side effects:
 *	On very first invocation, create and initialize process-wide
 *  descriptor handling common caches.
 *
 *----------------------------------------------------------------------
 */
int
Ns_ModuleInit(char *server, char *module)
{
    static int initCommon = 0;
    Server *servPtr;

    Ns_Log(Notice, "nscache module version @VER@ server: %s", server);

    /*
     * Initialize cache descriptor for process-wide caches
     */

    if (initCommon == 0) {
        Ns_MasterLock();
        if (initCommon == 0) {
            commonCache = ns_malloc(sizeof(Server));
            Tcl_InitHashTable(&commonCache->tclCaches, TCL_STRING_KEYS);
            Ns_MutexInit(&commonCache->lock);
            Ns_MutexSetName(&commonCache->lock, "nscache:commonTclCaches");
            initCommon = 1;
        }
        Ns_MasterUnlock();
    }

    /*
     * Initialize per-virtual-server cache descriptor
     */

    servPtr = ns_malloc(sizeof(Server));
    Tcl_InitHashTable(&servPtr->tclCaches, TCL_STRING_KEYS);
    Ns_MutexInit(&servPtr->lock);
    Ns_MutexSetName2(&servPtr->lock, "nscache:tclCaches", server);
    Ns_TclInitInterps(server, (Ns_TclInterpInitProc *) CacheInterpInit,
            (void *) servPtr);

    return NS_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CacheInterpInit --
 *
 *	Add ns_cache commands to the given interp.
 *
 * Results:
 *	Standard AOLserver return code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
CacheInterpInit(Tcl_Interp *interp, void *context)
{
    Tcl_Command cacheCmd;

    cacheCmd = Tcl_CreateObjCommand(interp, "ns_cache", NsTclCacheCmd,
            (ClientData) context, NULL);

    return (cacheCmd != NULL) ? NS_OK : NS_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetThreadCache --
 *
 *  Locate the per-thread cache.
 *
 * Results:
 *	A cache private to the current thread and virtual server.
 *
 * Side effects:
 *	A new cache may be allocated.
 *
 *----------------------------------------------------------------------
 */

static TclCache *
GetThreadCache(char *name, TclCacheInfo *info)
{
    TclCache *cache;
    Ns_DString ds;
    char tid[20];

    cache = (TclCache *) Ns_TlsGet(&info->tls);

    if (cache == NULL) {
	cache = ns_calloc(1, sizeof *cache);

	Ns_DStringInit(&ds);
	Ns_DStringAppend(&ds, name);
	Ns_DStringAppend(&ds, ".");
	sprintf(tid, "%d", Ns_ThreadId());
	Ns_DStringAppend(&ds, tid);

	cache->nscache = Ns_CacheCreateSz(Ns_DStringValue(&ds),
	    TCL_STRING_KEYS, (size_t) info->maxSize, ThreadValueFree);

	cache->evalPtr = ThreadCacheEvalCmd;
	cache->flushPtr = FlushCmd;
	cache->getPtr = ThreadCacheGetCmd;
	cache->namesPtr = NamesCmd;
	cache->setPtr = ThreadCacheSetCmd;
        cache->incrPtr = ThreadCacheIncrCmd;

	Ns_TlsSet(&info->tls, cache);
    }

    return cache;
}

/*
 *----------------------------------------------------------------------
 *
 * TclCacheFind --
 *
 *  Locates the named cache for the given virtual server.
 *
 * Results:
 *	The appropriate cache (global or thread-private) for the
 *  specified name.  If the cache is global, sets *globalPtr to
 *  NS_TRUE, else sets it to NS_FALSE.
 *
 * Side effects:
 *	A new thread-private cache may be created.
 *
 *----------------------------------------------------------------------
 */

static TclCache *
TclCacheFind(Server *servPtr, char *name, int *needsLockingPtr)
{
    TclCache *cache = NULL;
    
    cache = CacheFind(servPtr, name, needsLockingPtr);
    if (cache == NULL) {
        cache = CacheFind(commonCache, name, needsLockingPtr);
    }

    return cache;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateThreadCache --
 *
 *	Create a thread cache.  Actually, just create the TclCacheInfo
 *  for it.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	Adds a TclCacheInfo to tclCaches.
 *
 *----------------------------------------------------------------------
 */

static int
CreateThreadCache(Tcl_Interp *interp, Server *servPtr, char *name, int maxSize)
{
    TclCacheInfo  *iPtr;
    Tcl_HashEntry *hPtr;
    int            new;

    Ns_MutexLock(&servPtr->lock);

    hPtr = Tcl_CreateHashEntry(&servPtr->tclCaches, name, &new);
    if (!new) {
	Tcl_AppendResult(interp, "a cache named ", name,
	    " already exists", NULL);

	Ns_MutexUnlock(&servPtr->lock);
	return TCL_ERROR;
    }

    iPtr = ns_malloc(sizeof *iPtr);
    iPtr->globalCache = NULL;
    Ns_TlsAlloc(&iPtr->tls, CleanupThreadCache);
    iPtr->maxSize = maxSize;

    Tcl_SetHashValue(hPtr, iPtr);

    Ns_MutexUnlock(&servPtr->lock);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateGlobalCache --
 *
 *	Create a global cache.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	Adds a TclCacheInfo to tclCaches.
 *
 *----------------------------------------------------------------------
 */

static int
CreateGlobalCache(Tcl_Interp *interp, Server *servPtr, char *name, int maxSize,
    int timeout)
{
    TclCacheInfo  *iPtr;
    Tcl_HashEntry *hPtr;
    int            new;

    Ns_MutexLock(&servPtr->lock);

    hPtr = Tcl_CreateHashEntry(&servPtr->tclCaches, name, &new);
    if (!new) {
	Tcl_AppendResult(interp, "a cache named ", name,
	    " already exists", NULL);

	Ns_MutexUnlock(&servPtr->lock);
	return TCL_ERROR;
    }

    iPtr = ns_malloc(sizeof *iPtr);
    iPtr->globalCache = ns_malloc(sizeof *iPtr->globalCache);
    if (maxSize > 0) {
	iPtr->globalCache->nscache = Ns_CacheCreateSz(name,
	    TCL_STRING_KEYS, (size_t) maxSize, GlobalValueFree);
    } else {
	iPtr->globalCache->nscache = Ns_CacheCreate(name,
	    TCL_STRING_KEYS, timeout, GlobalValueFree);
    }
    iPtr->globalCache->evalPtr = GlobalCacheEvalCmd;
    iPtr->globalCache->flushPtr = FlushCmd;
    iPtr->globalCache->getPtr = GlobalCacheGetCmd;
    iPtr->globalCache->namesPtr = NamesCmd;
    iPtr->globalCache->setPtr = GlobalCacheSetCmd;
    iPtr->globalCache->incrPtr = GlobalCacheIncrCmd;

    Tcl_SetHashValue(hPtr, iPtr);

    Ns_MutexUnlock(&servPtr->lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateCmd --
 *
 *	Create a cache.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	Creates a new cache.
 *
 *----------------------------------------------------------------------
 */

static int
CreateCmd(Tcl_Interp *interp, Server *servPtr, int objc, Tcl_Obj * CONST objv[])
{
    int        obji, n;
    char      *arg;
    int        maxSize = -1;
    int        timeout = -1;
    int        thread  = -1;
    int        common  = -1;
    char      *name;

    if (objc < 3) {
	Tcl_AppendResult(interp, "missing cache-name argument", NULL);
	return TCL_ERROR;
    }

    for (obji = 3; obji < objc; obji++) {
	arg = Tcl_GetString(objv[obji]);

	if (STREQ(arg, "-size") || STREQ(arg, "-timeout")) {

	    if (obji + 1 >= objc) {
		Tcl_AppendResult(interp, arg,
		    " requires an argument", NULL);
		return TCL_ERROR;
	    }

	    if (timeout != -1 || maxSize != -1) {
		Tcl_AppendResult(interp,
		    "only one of -size and -timeout is allowed", NULL);
		return TCL_ERROR;
	    }

	    if (Tcl_GetIntFromObj(interp, objv[obji+1], &n) != TCL_OK) {
		return TCL_ERROR;
	    }

	    if (n < 0) {
		Tcl_AppendResult(interp, arg + 1,
		    " must be at least 0", NULL);
		return TCL_ERROR;
	    }

	    if (arg[1] == 's') {
		maxSize =  n;
	    } else {
		timeout =  n;
	    }

	    obji++;
	}

	else if (STREQ(arg, "-thread")) {
	    if (obji + 1 >= objc) {
		Tcl_AppendResult(interp, arg,
		    " requires an argument", NULL);
		return TCL_ERROR;
	    }

	    if (thread != -1) {
		Tcl_AppendResult(interp, arg,
		    " may only be given once", NULL);
		return TCL_ERROR;
	    }

	    if (common != -1) {
		Tcl_AppendResult(interp, arg,
		    " may not be used together with -common option", NULL);
		return TCL_ERROR;
	    }

	    if (Tcl_GetBooleanFromObj(interp, objv[obji+1], &thread)
		!= TCL_OK)
	    {
		return TCL_ERROR;
	    }

	    obji++;
	}

	else if (STREQ(arg, "-common")) {
	    if (obji + 1 >= objc) {
		Tcl_AppendResult(interp, arg,
		    " requires an argument", NULL);
		return TCL_ERROR;
	    }

	    if (common != -1) {
		Tcl_AppendResult(interp, arg,
		    " may only be given once", NULL);
		return TCL_ERROR;
	    }

	    if (thread != -1) {
		Tcl_AppendResult(interp, arg,
		    " may not be used together with -thread option", NULL);
		return TCL_ERROR;
	    }

	    if (Tcl_GetBooleanFromObj(interp, objv[obji+1], &common)
		!= TCL_OK)
	    {
		return TCL_ERROR;
	    }

	    obji++;
	}

	else {
	    Tcl_AppendResult(interp, "unknown flag ", arg, "; should be \"",
		Tcl_GetString(objv[0]), " ", Tcl_GetString(objv[1]),
		" cache-name ?-size size? ?-timeout timeout?"
        " ?-thread boolean | -common boolean?", NULL);
	    return TCL_ERROR;
	}
    }

    name = Tcl_GetString(objv[2]);

    if (timeout < 0 && maxSize < 0) {
	timeout = 0;
	maxSize = 0;
    }

    if (thread < 0) {
	thread = 0;
    }

    if (common < 0) {
	common = 0;
    }

    if (thread && timeout > 0) {
	Tcl_AppendResult(interp,
	    "thread-private cache cannot have a timeout", NULL);
	return TCL_ERROR;
    }

    if (common) {
	return CreateGlobalCache(interp, commonCache, name, maxSize, timeout);
    }
    
    else if (thread) {
	return CreateThreadCache(interp, servPtr, name, maxSize);
    }

    else {
	return CreateGlobalCache(interp, servPtr, name, maxSize, timeout);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * NamesCmd --
 *
 *	Get a list of all the entry names in a cache.
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
NamesCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp, int objc,
    Tcl_Obj * CONST objv[])
{
    Ns_CacheSearch  search;
    Ns_Entry       *ePtr;
    Tcl_Obj        *namePtr;
    Tcl_Obj        *resultPtr;
    int             status;

    if (objc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    Tcl_GetString(objv[0]), " ",
	    Tcl_GetString(objv[1]), " cache\"", NULL);
	return TCL_ERROR;
    }

    if (needsLocking)
	Ns_CacheLock(cache);

    status = TCL_OK;
    resultPtr = Tcl_GetObjResult(interp);
    ePtr = Ns_CacheFirstEntry(cache, &search);

    while (ePtr != NULL) {
	namePtr = Tcl_NewStringObj(Ns_CacheKey(ePtr), -1);
	if (Tcl_ListObjAppendElement(interp, resultPtr, namePtr) != TCL_OK) {
	    Tcl_DecrRefCount(namePtr);
	    status = TCL_ERROR;
	    break;
	}
        ePtr = Ns_CacheNextEntry(&search);
    }

    if (needsLocking)
	Ns_CacheUnlock(cache);

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * FlushCmd --
 *
 *	Remove an entry from the cache by key.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
FlushCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp, int objc,
    Tcl_Obj * CONST objv[])
{
    Ns_Entry *ePtr;

    if (objc != 4) {
	Tcl_AppendResult(interp,"wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ", Tcl_GetString(objv[1]),
		    " cache key\"", NULL);
        return TCL_ERROR;
    }

    if (needsLocking)
	Ns_CacheLock(cache);

    ePtr = Ns_CacheFindEntry(cache, Tcl_GetString(objv[3]));

    if (ePtr != NULL) {
	Ns_CacheFlushEntry(ePtr);
	if (needsLocking)
	    Ns_CacheBroadcast(cache);
    }

    if (needsLocking)
	Ns_CacheUnlock(cache);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * IncompleteEntryP --
 *
 * Results:
 *	NS_TRUE if the cache value is complete, else NS_FALSE.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CompleteEntryP(Ns_Entry *ePtr)
{
    return ((GlobalValue *)Ns_CacheGetValue(ePtr))->value != NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetGlobalEntry --
 *
 *	Given a locked global cache and a key, get the cache entry.
 *  If the key is not found and create is set, then create a new,
 *  incomplete entry and return it.  If the key is not found and
 *  create is not set, then return NULL.  If the key is found but
 *  the entry is incomplete, wait until the entry is complete or
 *  deleted.
 *  If the key is found and the entry is complete, return it.
 *
 * Results:
 *	If create is set, a new or complete cache entry.
 *  If create is not set, NULL or a complete cache entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Ns_Entry *
GetGlobalEntry(Ns_Cache *cache, char *key, int create)
{
    Ns_Entry    *ePtr;
    GlobalValue *vPtr;
    int          new = 0;

    while (1) {
	if (create) {
	    ePtr = Ns_CacheCreateEntry(cache, key, &new);

	    if (new) {
		vPtr = ns_malloc(sizeof *vPtr);
		vPtr->value = NULL;
		vPtr->length = 0;
		vPtr->flushed = NS_FALSE;
		Ns_CacheSetValueSz(ePtr, vPtr, 0);

		break;
	    }
	}

	else {
	    ePtr = Ns_CacheFindEntry(cache, key);

	    if (ePtr == NULL) {
		break;
	    }
	}

	if (CompleteEntryP(ePtr)) {
	    break;
	}

	Ns_CacheWait(cache);
    }

    return ePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * GlobalCacheGetCmd --
 *
 *  Get a value from the cache given a key. If some other thread is
 *  currently in an eval for the same cache and key, block until
 *  that thread finishes, then use the value it computed.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GlobalCacheGetCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    char        *key;
    char        *varname;
    Tcl_Obj     *resultPtr;
    Tcl_Obj     *newValuePtr;
    Ns_Entry    *ePtr;
    GlobalValue *vPtr;
    int          status = TCL_OK;

    if (objc == 4) {
	varname = NULL;
    }

    else if (objc == 5) {
	varname = Tcl_GetString(objv[4]);
    }

    else {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    Tcl_GetString(objv[0]), " ",
	    Tcl_GetString(objv[1]), " cache key ?varname?\"", NULL);
	return TCL_ERROR;
    }

    resultPtr = Tcl_GetObjResult(interp);
    key = Tcl_GetString(objv[3]);

    Ns_CacheLock(cache);

    ePtr = GetGlobalEntry(cache, key, 0);

    if (ePtr == NULL) {

	if (varname == NULL) {
	    Tcl_AppendStringsToObj(resultPtr, "no such key: ", key, NULL);
	    status = TCL_ERROR;
	} else {
	    Tcl_SetBooleanObj(resultPtr, 0);
	}

    } else {

	vPtr = (GlobalValue *) Ns_CacheGetValue(ePtr);
	if (varname == NULL) {
	    Tcl_SetStringObj(resultPtr, vPtr->value, vPtr->length);
	} else {
	    Tcl_SetBooleanObj(resultPtr, 1);

	    newValuePtr = Tcl_NewStringObj(vPtr->value, vPtr->length);
	    if (Tcl_SetVar2Ex(interp, varname, NULL, newValuePtr,
		TCL_LEAVE_ERR_MSG) == NULL)
	    {
		status = TCL_ERROR;
		Tcl_DecrRefCount(newValuePtr);
	    }
	}

    }

    Ns_CacheUnlock(cache);

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * GlobalCacheEvalCmd --
 *
 *	Get a value from the cache given a key.  If the key is not in
 *  the cache, evaluate the code block and store its result as the
 *  value for the key.  If some other thread is currently in an
 *  eval for the same cache and key, block until that thread
 *  finishes, then use the value it computed.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GlobalCacheEvalCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    Ns_Entry    *ePtr;
    GlobalValue *vPtr;
    int          status;
    char        *string;
    int          length;

    if (objc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key code\"", NULL);
        return TCL_ERROR;
    }

    Ns_CacheLock(cache);

    ePtr = GetGlobalEntry(cache, Tcl_GetString(objv[3]), 1);
    vPtr = (GlobalValue *)Ns_CacheGetValue(ePtr);

    if (vPtr->value != NULL) {
	Tcl_SetStringObj(Tcl_GetObjResult(interp),
	    vPtr->value, vPtr->length);
	status = TCL_OK;
    }

    else {
	Ns_CacheUnlock(cache);
	status = Tcl_EvalObjEx(interp, objv[4], 0);
	Ns_CacheLock(cache);

	if (status == TCL_OK || status == TCL_RETURN) {
	    if (vPtr->flushed) {
		/* Cache entry was flushed while we were in Tcl_Eval. */
		ns_free(vPtr);
	    } else {
		string = Tcl_GetStringFromObj(Tcl_GetObjResult(interp),
		    &length);
		Ns_CacheSetValueSz(ePtr, vPtr, (size_t) length);
		vPtr->value = ns_malloc((size_t) length);
		memcpy(vPtr->value, string, (size_t) length);
		vPtr->length = length;
		vPtr->flushed = 0;
	    }
	    status = TCL_OK;
	}

	else {
	    if (!vPtr->flushed) {
		Ns_CacheDeleteEntry(ePtr);
	    }
	    ns_free(vPtr);
	}

	Ns_CacheBroadcast(cache);
    }

    Ns_CacheUnlock(cache);

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * GlobalCacheSetCmd --
 *
 *	Store the key/value pair in the cache, flushing any previous
 *  value for the key.
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GlobalCacheSetCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    char        *key;
    char        *value;
    Ns_Entry    *ePtr;
    GlobalValue *vPtr;
    int          new;

    if (objc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key value\"", NULL);
        return TCL_ERROR;
    }

    key = Tcl_GetString(objv[3]);
    vPtr = ns_malloc(sizeof *vPtr);
    value = Tcl_GetStringFromObj(objv[4], &vPtr->length);
    vPtr->value = ns_malloc((size_t) vPtr->length);
    memcpy(vPtr->value, value, (size_t) vPtr->length);

    Ns_CacheLock(cache);

    ePtr = Ns_CacheCreateEntry(cache, key, &new);
    Ns_CacheSetValueSz(ePtr, vPtr, (size_t) vPtr->length);

    Ns_CacheUnlock(cache);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GlobalCacheIncrCmd --
 *
 *	Increases the value by specified key in the cache, creates new entry
 *  if does not exist
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GlobalCacheIncrCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    int         incr = 1;
    char        buf[20];
    Ns_Entry    *ePtr;
    GlobalValue *vPtr;

    if (objc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key ?value?\"", NULL);
        return TCL_ERROR;
    }

    if (objc > 4 && Tcl_GetIntFromObj(interp,objv[4],&incr) != TCL_OK) {
       return TCL_ERROR;
    }

    Ns_CacheLock(cache);

    ePtr = GetGlobalEntry(cache, Tcl_GetString(objv[3]), 1);
    vPtr = (GlobalValue *)Ns_CacheGetValue(ePtr);
    sprintf(buf,"%d",(vPtr->value ? atoi(vPtr->value) : 0) + incr);
    ns_free(vPtr->value);
    vPtr->value = ns_strdup(buf);
    vPtr->length = strlen(buf);

    Ns_CacheUnlock(cache);

    Tcl_SetStringObj(Tcl_GetObjResult(interp), vPtr->value, vPtr->length);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ThreadCacheGetCmd --
 *
 *  Get a value from the cache given a key.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ThreadCacheGetCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    char        *key;
    char        *varname;
    Tcl_Obj     *resultPtr;
    Ns_Entry    *ePtr;
    Tcl_Obj     *vPtr;
    int          status = TCL_OK;

    if (objc == 4) {
	varname = NULL;
    }

    else if (objc == 5) {
	varname = Tcl_GetString(objv[4]);
    }

    else {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    Tcl_GetString(objv[0]), " ",
	    Tcl_GetString(objv[1]), " cache key ?varname?\"", NULL);
	return TCL_ERROR;
    }

    resultPtr = Tcl_GetObjResult(interp);
    key = Tcl_GetString(objv[3]);

    ePtr = Ns_CacheFindEntry(cache, key);

    if (ePtr == NULL) {

	if (varname == NULL) {
	    Tcl_AppendStringsToObj(resultPtr, "no such key: ", key, NULL);
	    status = TCL_ERROR;
	} else {
	    Tcl_SetBooleanObj(resultPtr, 0);
	}

    } else {

	vPtr = (Tcl_Obj *) Ns_CacheGetValue(ePtr);
	if (varname == NULL) {
	    Tcl_SetObjResult(interp, vPtr);
	} else {
	    Tcl_SetBooleanObj(resultPtr, 1);

	    if (Tcl_SetVar2Ex(interp, varname, NULL, vPtr,
		TCL_LEAVE_ERR_MSG) == NULL)
	    {
		status = TCL_ERROR;
	    }
	}

    }

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * ThreadCacheEvalCmd --
 *
 *	Get a value from the cache given a key.  If the key is not in
 *  the cache, evaluate the code block and store its result as the
 *  value for the key.
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ThreadCacheEvalCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    Ns_Entry    *ePtr;
    Tcl_Obj     *resultPtr;
    int          status;
    int          new;
    int          length;

    if (objc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key code\"", NULL);
        return TCL_ERROR;
    }

    ePtr = Ns_CacheCreateEntry(cache, Tcl_GetString(objv[3]), &new);

    if (new) {

	status = Tcl_EvalObjEx(interp, objv[4], 0);

	if (status == TCL_OK || status == TCL_RETURN) {
	    resultPtr = Tcl_GetObjResult(interp);
	    Tcl_GetStringFromObj(resultPtr, &length);
	    Tcl_IncrRefCount(resultPtr);
	    Ns_CacheSetValueSz(ePtr, resultPtr, (size_t) length);
	    status = TCL_OK;
	}

	else {
	    Ns_CacheDeleteEntry(ePtr);
	}

    } else {

	status = TCL_OK;
	Tcl_SetObjResult(interp, (Tcl_Obj *) Ns_CacheGetValue(ePtr));

    }

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * ThreadCacheSetCmd --
 *
 *	Store the key/value pair in the cache, flushing any previous
 *  value for the key.
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ThreadCacheSetCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    Tcl_Obj  *value;
    int       length;
    Ns_Entry *ePtr;
    int       new;

    if (objc != 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key value\"", NULL);
        return TCL_ERROR;
    }

    value = objv[4];

    ePtr = Ns_CacheCreateEntry(cache, Tcl_GetString(objv[3]), &new);
    Tcl_GetStringFromObj(value, &length);
    Tcl_IncrRefCount(value);
    Ns_CacheSetValueSz(ePtr, value, (size_t) length);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ThreadCacheIncrCmd --
 *
 *	Increases the value by specified key in the cache, creates new entry
 *  if does not exist
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ThreadCacheIncrCmd(Ns_Cache *cache, int needsLocking, Tcl_Interp *interp,
    int objc, Tcl_Obj * CONST objv[])
{
    int       incr = 1;
    int       length;
    int       new;
    int       iVal;
    Tcl_Obj  *oPtr;
    Ns_Entry *ePtr;

    if (objc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		    Tcl_GetString(objv[0]), " ",
		    Tcl_GetString(objv[1]), " cache key ?value?\"", NULL);
        return TCL_ERROR;
    }

    if (objc > 4 && Tcl_GetIntFromObj(interp,objv[4],&incr) != TCL_OK) {
       return TCL_ERROR;
    }

    ePtr = Ns_CacheCreateEntry(cache,Tcl_GetString(objv[3]),&new);
    if (new) {
      oPtr = Tcl_NewLongObj(1);
      Tcl_IncrRefCount(oPtr);
      Tcl_GetStringFromObj(oPtr, &length);
      Ns_CacheSetValueSz(ePtr, oPtr, (size_t) length);
    } else {
      oPtr = (Tcl_Obj *)Ns_CacheGetValue(ePtr);
      if (Tcl_GetIntFromObj(interp,oPtr,&iVal) != TCL_OK) return TCL_ERROR;
      Tcl_SetLongObj(oPtr, (iVal + incr));
    }

    Tcl_SetObjResult(interp, oPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheCmd --
 *
 *	Execute a Tcl ns_cache command.
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */

static int
NsTclCacheCmd(ClientData arg, Tcl_Interp *interp, int objc,
    Tcl_Obj * CONST objv[])
{
    char            *cmd;
    TclCache        *cache;
    int              needsLocking;
    TclCacheCmdProc *procPtr = NULL;
    Server *servPtr = arg;

    if (objc < 2) {
	Tcl_AppendResult(interp, "usage: ", Tcl_GetString(objv[0]),
	    " subcommand args\n  where subcommand is one of: ",
	    "create eval flush get names set", NULL);
	return TCL_ERROR;
    }

    cmd = Tcl_GetString(objv[1]);

    if (STREQ(cmd, "create")) {
	return CreateCmd(interp, servPtr, objc, objv);
    }

    else if (
	STREQ(cmd, "eval")
	|| STREQ(cmd, "flush")
	|| STREQ(cmd, "get")
	|| STREQ(cmd, "names")
	|| STREQ(cmd, "set")
        || STREQ(cmd, "incr")
    ) {
	if (objc < 3) {
	    Tcl_AppendResult(interp, "missing cache name for ",
		Tcl_GetString(objv[0]), " ", Tcl_GetString(objv[1]),
		" command", NULL);
	    return TCL_ERROR;
	}

	cache = TclCacheFind(servPtr, Tcl_GetString(objv[2]), &needsLocking);
	if (cache == NULL) {
	    Tcl_AppendResult(interp, "no such cache: ",
		Tcl_GetString(objv[2]), NULL);
	    return TCL_ERROR;
	}

	switch (*cmd) {
	case 'e': procPtr = cache->evalPtr;  break;
	case 'f': procPtr = cache->flushPtr; break;
	case 'g': procPtr = cache->getPtr;   break;
	case 'n': procPtr = cache->namesPtr; break;
	case 's': procPtr = cache->setPtr;   break;
        case 'i': procPtr = cache->incrPtr;   break;
	}

	return (*procPtr)(cache->nscache, needsLocking, interp, objc, objv);
    }

    Tcl_AppendResult(interp, "unknown ", Tcl_GetString(objv[0]),
	" subcommand \"", Tcl_GetString(objv[1]), "\"", NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GlobalValueFree --
 *
 *  Free a GlobalValue. If vPtr->value == NULL, then the value is
 *  incomplete and some thread is still computing it. Simply set
 *  the flushed flag, but don't free the GlobalValue, so that thread
 *  can discover that its entry has been flushed. If vPtr->value !=
 *  NULL, then the value is complete and we can free the GlobalValue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Either frees vPtr or sets vPtr->tid = 0.
 *
 *----------------------------------------------------------------------
 */

static void
GlobalValueFree(void *p)
{
    GlobalValue *vPtr = p;

    if (vPtr->value == NULL) {
	vPtr->flushed = 1;
    }

    else {
	ns_free(vPtr->value);
	ns_free(vPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ThreadValueFree --
 *
 *  Free a value in a thread-private cache.  Such values are always
 *  Tcl_Obj pointers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  Decrements a Tcl_Obj reference count and possibly frees the
 *  object.
 *
 *----------------------------------------------------------------------
 */

static void
ThreadValueFree(void *p)
{
    Tcl_Obj *oPtr = (Tcl_Obj *)p;

    Tcl_DecrRefCount(oPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupThreadCache --
 *
 *  Delete a thread cache entirely.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupThreadCache(void *p)
{
    TclCache *cache = (TclCache *) p;

    Ns_CacheDestroy(cache->nscache);
    ns_free(cache);
}

/*
 *----------------------------------------------------------------------
 *
 * CacheFind --
 *
 *  Locates the cache for a virtual-server.
 *
 * Results:
 *	The cache descriptor or NULL if no cache found.
 *
 * Side effects:
 *  Per-thread cache may be created.
 *
 *----------------------------------------------------------------------
 */

static TclCache *
CacheFind(Server *servPtr, char *name, int *needsLockingPtr)
{
    Tcl_HashEntry *hPtr;
    TclCacheInfo  *info;
    TclCache      *cache = NULL;

    Ns_MutexLock(&servPtr->lock);
    hPtr = Tcl_FindHashEntry(&servPtr->tclCaches, name);
    if (hPtr != NULL) {
	info = (TclCacheInfo *) Tcl_GetHashValue(hPtr);
	if (info->globalCache != NULL) {
	    cache = info->globalCache;
	    *needsLockingPtr = NS_TRUE;
	} else {
	    cache = GetThreadCache(name, info);
	    *needsLockingPtr = NS_FALSE;
	}
    }
    Ns_MutexUnlock(&servPtr->lock);

    return cache;
}


