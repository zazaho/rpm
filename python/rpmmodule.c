#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <glob.h>	/* XXX rpmio.h */
#include <dirent.h>	/* XXX rpmio.h */
#include <locale.h>

#include "Python.h"
#include "rpmlib.h"
#include "misc.h"
#include "rpmmacro.h"
#include "upgrade.h"

/* from lib/misc.c */
int rpmvercmp(const char * one, const char * two);

/* Forward types */

typedef struct rpmdbObject_s rpmdbObject;
typedef struct rpmdbMIObject_s rpmdbMIObject;
typedef struct rpmtransObject_s rpmtransObject;
typedef struct hdrObject_s hdrObject;

/* Prototypes */

/* rpmdb functions */
static void rpmdbDealloc(rpmdbObject * s);
static PyObject * rpmdbFirst(rpmdbObject * s, PyObject * args);
static PyObject * rpmdbNext(rpmdbObject * s, PyObject * args);
static PyObject * rpmdbGetAttr(rpmdbObject * s, char * name);
static PyObject * rpmdbByName(rpmdbObject * s, PyObject * args);
static PyObject * rpmdbByProvides(rpmdbObject * s, PyObject * args);
static PyObject * rpmdbByFile(rpmdbObject * s, PyObject * args);
static int rpmdbLength(rpmdbObject * s);
static hdrObject * rpmdbSubscript(rpmdbObject * s, PyObject * key);
static rpmdbMIObject * py_rpmdbInitIterator (rpmdbObject * s, PyObject * args);

/* rpmdbMatchIterator functions */
static void rpmdbMIDealloc(rpmdbMIObject * s);
static PyObject * rpmdbMIGetAttr (rpmdbObject *s, char *name);

static PyObject * rpmdbMINext(rpmdbMIObject * s, PyObject * args);

/* header functions */
static void hdrDealloc(hdrObject * s);
static PyObject * hdrGetAttr(hdrObject * s, char * name);
static PyObject * hdrSubscript(hdrObject * s, PyObject * item);
static PyObject * hdrKeyList(hdrObject * s, PyObject * args);
static PyObject * hdrUnload(hdrObject * s, PyObject * args);
static PyObject * hdrVerifyFile(hdrObject * s, PyObject * args);
static PyObject * hdrCompressFilelist(hdrObject * s, PyObject * args);
static PyObject * hdrExpandFilelist(hdrObject * s, PyObject * args);
static PyObject * hdrFullFilelist(hdrObject * s, PyObject * args);

void initrpm(void);

/* global module functions */
static PyObject * doAddMacro(PyObject * self, PyObject * args);
static PyObject * doDelMacro(PyObject * self, PyObject * args);
static rpmdbObject * rpmOpenDB(PyObject * self, PyObject * args);
static PyObject * hdrLoad(PyObject * self, PyObject * args);
static PyObject * rpmHeaderFromPackage(PyObject * self, PyObject * args);
static PyObject * rpmHeaderFromFile(PyObject * self, PyObject * args);
static PyObject * archScore(PyObject * self, PyObject * args);
static PyObject * rpmHeaderFromFD(PyObject * self, PyObject * args);
static PyObject * findUpgradeSet(PyObject * self, PyObject * args);
static PyObject * errorSetCallback (PyObject * self, PyObject * args);
static PyObject * errorString (PyObject * self, PyObject * args);
static PyObject * versionCompare (PyObject * self, PyObject * args);
static PyObject * labelCompare (PyObject * self, PyObject * args);
static PyObject * rebuildDB (PyObject * self, PyObject * args);
/*  static PyObject * doFopen(PyObject * self, PyObject * args); */

/* transaction set functions */
static PyObject * rpmtransCreate(PyObject * self, PyObject * args);
static PyObject * rpmtransAdd(rpmtransObject * s, PyObject * args);
static PyObject * rpmtransRemove(rpmtransObject * s, PyObject * args);
static PyObject * rpmtransDepCheck(rpmtransObject * s, PyObject * args);
static PyObject * rpmtransRun(rpmtransObject * s, PyObject * args);
static PyObject * rpmtransOrder(rpmtransObject * s, PyObject * args);
static void rpmtransDealloc(PyObject * o);
static PyObject * rpmtransGetAttr(rpmtransObject * o, char * name);
static int rpmtransSetAttr(rpmtransObject * o, char * name,
			   PyObject * val);

/* signature verification */
static PyObject * checkSig (PyObject * self, PyObject * args);

/* hack to get the current header that's in the transaction set */
static PyObject * getTsHeader (PyObject * self, PyObject * args);

/* internal functions */
static long tagNumFromPyObject (PyObject *item);
static void mungeFilelist(Header h);

/* Types */
struct rpmdbObject_s {
    PyObject_HEAD;
    rpmdb db;
    int offx;
    int noffs;
    int *offsets;
} ;

struct rpmdbMIObject_s {
    PyObject_HEAD;
    rpmdbObject *db;
    rpmdbMatchIterator mi;
} ;

struct rpmtransObject_s {
    PyObject_HEAD;
    rpmdbObject * dbo;
    rpmTransactionSet ts;
    PyObject * keyList;			/* keeps reference counts correct */
    FD_t scriptFd;
} ;

struct hdrObject_s {
    PyObject_HEAD;
    Header h;
    Header sigs;
    char ** md5list;
    char ** fileList;
    char ** linkList;
    int_32 * fileSizes;
    int_32 * mtimes;
    int_32 * uids, * gids;
    unsigned short * rdevs;
    unsigned short * modes;
} ;

/* Data */

static PyObject * pyrpmError;
static Header transactionSetHeader = NULL;

static PyMethodDef rpmModuleMethods[] = {
    { "TransactionSet", (PyCFunction) rpmtransCreate, METH_VARARGS, NULL },
    { "addMacro", (PyCFunction) doAddMacro, METH_VARARGS, NULL },
    { "delMacro", (PyCFunction) doDelMacro, METH_VARARGS, NULL },
    { "archscore", (PyCFunction) archScore, METH_VARARGS, NULL },
    { "findUpgradeSet", (PyCFunction) findUpgradeSet, METH_VARARGS, NULL },
    { "headerFromPackage", (PyCFunction) rpmHeaderFromPackage, METH_VARARGS, NULL },
    { "headerLoad", (PyCFunction) hdrLoad, METH_VARARGS, NULL },
    { "opendb", (PyCFunction) rpmOpenDB, METH_VARARGS, NULL },
    { "rebuilddb", (PyCFunction) rebuildDB, METH_VARARGS, NULL },
    { "readHeaderListFromFD", (PyCFunction) rpmHeaderFromFD, METH_VARARGS, NULL },
    { "readHeaderListFromFile", (PyCFunction) rpmHeaderFromFile, METH_VARARGS, NULL },
    { "errorSetCallback", (PyCFunction) errorSetCallback, METH_VARARGS, NULL },
    { "errorString", (PyCFunction) errorString, METH_VARARGS, NULL },
    { "versionCompare", (PyCFunction) versionCompare, METH_VARARGS, NULL },
    { "labelCompare", (PyCFunction) labelCompare, METH_VARARGS, NULL },
    { "checksig", (PyCFunction) checkSig, METH_VARARGS, NULL },
    { "getTransactionCallbackHeader", (PyCFunction) getTsHeader, METH_VARARGS, NULL },
/*      { "Fopen", (PyCFunction) doFopen, METH_VARARGS, NULL }, */
    { NULL }
} ;

static PyMappingMethods hdrAsMapping = {
	(inquiry) 0,			/* mp_length */
	(binaryfunc) hdrSubscript,	/* mp_subscript */
	(objobjargproc)0,		/* mp_ass_subscript */
};

static PyTypeObject hdrType = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,				/* ob_size */
	"header",			/* tp_name */
	sizeof(hdrObject),		/* tp_size */
	0,				/* tp_itemsize */
	(destructor) hdrDealloc, 	/* tp_dealloc */
	0,				/* tp_print */
	(getattrfunc) hdrGetAttr, 	/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,	 			/* tp_as_sequence */
	&hdrAsMapping,			/* tp_as_mapping */
};

#ifndef DYINGSOON
static PyMappingMethods rpmdbAsMapping = {
	(inquiry) rpmdbLength,		/* mp_length */
	(binaryfunc) rpmdbSubscript,	/* mp_subscript */
	(objobjargproc)0,		/* mp_ass_subscript */
};
#endif

static PyTypeObject rpmdbType = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,				/* ob_size */
	"rpmdb",			/* tp_name */
	sizeof(rpmdbObject),		/* tp_size */
	0,				/* tp_itemsize */
	(destructor) rpmdbDealloc, 	/* tp_dealloc */
	0,				/* tp_print */
	(getattrfunc) rpmdbGetAttr, 	/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
#ifndef DYINGSOON
	&rpmdbAsMapping,		/* tp_as_mapping */
#else
	0,
#endif
};

/* Python rpmdbMatchIterator object */
static PyTypeObject rpmdbMIType = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,				/* ob_size */
	"rpmdbMatchIterator",		/* tp_name */
	sizeof(rpmdbMIObject),	/* tp_size */
	0,				/* tp_itemsize */
	(destructor) rpmdbMIDealloc, 	/* tp_dealloc */
	0,				/* tp_print */
	(getattrfunc) rpmdbMIGetAttr, 	/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
};

static PyTypeObject rpmtransType = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,				/* ob_size */
	"rpmtrans",			/* tp_name */
	sizeof(rpmtransObject),		/* tp_size */
	0,				/* tp_itemsize */
	(destructor) rpmtransDealloc, 	/* tp_dealloc */
	0,				/* tp_print */
	(getattrfunc) rpmtransGetAttr, 	/* tp_getattr */
	(setattrfunc) rpmtransSetAttr,	/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
};

static struct PyMethodDef rpmdbMethods[] = {
	{"firstkey",	    (PyCFunction) rpmdbFirst,	1 },
	{"nextkey",	    (PyCFunction) rpmdbNext,	1 },
	{"findbyfile",	    (PyCFunction) rpmdbByFile, 1 },
	{"findbyname",	    (PyCFunction) rpmdbByName, 1 },
	{"findbyprovides",  (PyCFunction) rpmdbByProvides, 1 },
	{"match",	    (PyCFunction) py_rpmdbInitIterator, 1 },
	{NULL,		NULL}		/* sentinel */
};

static struct PyMethodDef rpmdbMIMethods[] = {
	{"next",	    (PyCFunction) rpmdbMINext,	1 },
	{NULL,		NULL}		/* sentinel */
};

static struct PyMethodDef rpmtransMethods[] = {
	{"add",		(PyCFunction) rpmtransAdd,	1 },
	{"remove",	(PyCFunction) rpmtransRemove,	1 },
	{"depcheck",	(PyCFunction) rpmtransDepCheck,	1 },
	{"order",	(PyCFunction) rpmtransOrder,	1 },
	{"run",		(PyCFunction) rpmtransRun, 1 },
	{NULL,		NULL}		/* sentinel */
};

static struct PyMethodDef hdrMethods[] = {
	{"keys",	(PyCFunction) hdrKeyList,	1 },
	{"unload",	(PyCFunction) hdrUnload,	1 },
	{"verifyFile",	(PyCFunction) hdrVerifyFile,	1 },
	{"expandFilelist",	(PyCFunction) hdrExpandFilelist,	1 },
	{"compressFilelist",	(PyCFunction) hdrCompressFilelist,	1 },
	{"fullFilelist",	(PyCFunction) hdrFullFilelist,	1 },
	{NULL,		NULL}		/* sentinel */
};

/* External functions */
int mdfile(const char *fn, unsigned char *digest);

/* Code */

extern int _rpmio_debug;

void initrpm(void) {
    PyObject * m, * d, *o, * tag = NULL, * dict;
    int i;
    const struct headerSprintfExtension * extensions = rpmHeaderFormats;
    struct headerSprintfExtension * ext;

/*      _rpmio_debug = -1; */
    rpmReadConfigFiles(NULL, NULL);

    m = Py_InitModule("rpm", rpmModuleMethods);
    d = PyModule_GetDict(m);

    pyrpmError = PyString_FromString("rpm.error");
    PyDict_SetItemString(d, "error", pyrpmError);
    Py_DECREF(pyrpmError);

    dict = PyDict_New();

    for (i = 0; i < rpmTagTableSize; i++) {
	tag = PyInt_FromLong(rpmTagTable[i].val);
	PyDict_SetItemString(d, (char *) rpmTagTable[i].name, tag);
	Py_DECREF(tag);
        PyDict_SetItem(dict, tag, o=PyString_FromString(rpmTagTable[i].name + 7));
	Py_DECREF(o);
    }

    while (extensions->name) {
	if (extensions->type == HEADER_EXT_TAG) {
            (const struct headerSprintfExtension *) ext = extensions;
            PyDict_SetItemString(d, extensions->name, o=PyCObject_FromVoidPtr(ext, NULL));
	    Py_DECREF(o);
            PyDict_SetItem(dict, tag, o=PyString_FromString(ext->name + 7));
	    Py_DECREF(o);    
        }
        extensions++;
    }

    PyDict_SetItemString(d, "tagnames", dict);
    Py_DECREF(dict);


#define REGISTER_ENUM(val) \
    PyDict_SetItemString(d, #val, o=PyInt_FromLong(## val)); \
    Py_DECREF(o);
    
    REGISTER_ENUM(RPMFILE_STATE_NORMAL);
    REGISTER_ENUM(RPMFILE_STATE_REPLACED);
    REGISTER_ENUM(RPMFILE_STATE_NOTINSTALLED);
    REGISTER_ENUM(RPMFILE_CONFIG);
    REGISTER_ENUM(RPMFILE_MISSINGOK);
    REGISTER_ENUM(RPMFILE_DOC);

    REGISTER_ENUM(RPMDEP_SENSE_REQUIRES);
    REGISTER_ENUM(RPMDEP_SENSE_CONFLICTS);

    REGISTER_ENUM(RPMSENSE_SERIAL);
    REGISTER_ENUM(RPMSENSE_LESS);
    REGISTER_ENUM(RPMSENSE_GREATER);
    REGISTER_ENUM(RPMSENSE_EQUAL);
    REGISTER_ENUM(RPMSENSE_PREREQ);

    REGISTER_ENUM(RPMTRANS_FLAG_TEST);
    REGISTER_ENUM(RPMTRANS_FLAG_BUILD_PROBS);
    REGISTER_ENUM(RPMTRANS_FLAG_NOSCRIPTS);
    REGISTER_ENUM(RPMTRANS_FLAG_JUSTDB);
    REGISTER_ENUM(RPMTRANS_FLAG_NOTRIGGERS);
    REGISTER_ENUM(RPMTRANS_FLAG_NODOCS);
    REGISTER_ENUM(RPMTRANS_FLAG_ALLFILES);
    REGISTER_ENUM(RPMTRANS_FLAG_KEEPOBSOLETE);

    REGISTER_ENUM(RPMPROB_FILTER_IGNOREOS);
    REGISTER_ENUM(RPMPROB_FILTER_IGNOREARCH);
    REGISTER_ENUM(RPMPROB_FILTER_REPLACEPKG);
    REGISTER_ENUM(RPMPROB_FILTER_FORCERELOCATE);
    REGISTER_ENUM(RPMPROB_FILTER_REPLACENEWFILES);
    REGISTER_ENUM(RPMPROB_FILTER_REPLACEOLDFILES);
    REGISTER_ENUM(RPMPROB_FILTER_OLDPACKAGE);
    REGISTER_ENUM(RPMPROB_FILTER_DISKSPACE);

    REGISTER_ENUM(RPMCALLBACK_INST_PROGRESS);
    REGISTER_ENUM(RPMCALLBACK_INST_START);
    REGISTER_ENUM(RPMCALLBACK_INST_OPEN_FILE);
    REGISTER_ENUM(RPMCALLBACK_INST_CLOSE_FILE);
    REGISTER_ENUM(RPMCALLBACK_TRANS_PROGRESS);
    REGISTER_ENUM(RPMCALLBACK_TRANS_START);
    REGISTER_ENUM(RPMCALLBACK_TRANS_STOP);
    REGISTER_ENUM(RPMCALLBACK_UNINST_PROGRESS);
    REGISTER_ENUM(RPMCALLBACK_UNINST_START);
    REGISTER_ENUM(RPMCALLBACK_UNINST_STOP);

    REGISTER_ENUM(RPMPROB_BADARCH);
    REGISTER_ENUM(RPMPROB_BADOS);
    REGISTER_ENUM(RPMPROB_PKG_INSTALLED);
    REGISTER_ENUM(RPMPROB_BADRELOCATE);
    REGISTER_ENUM(RPMPROB_REQUIRES);
    REGISTER_ENUM(RPMPROB_CONFLICT);
    REGISTER_ENUM(RPMPROB_NEW_FILE_CONFLICT);
    REGISTER_ENUM(RPMPROB_FILE_CONFLICT);
    REGISTER_ENUM(RPMPROB_OLDPACKAGE);
    REGISTER_ENUM(RPMPROB_DISKSPACE);

    REGISTER_ENUM(CHECKSIG_PGP);
    REGISTER_ENUM(CHECKSIG_GPG);
    REGISTER_ENUM(CHECKSIG_MD5);
}

/* make a header with _all_ the tags we need */
static void mungeFilelist(Header h)
{
    const char ** fileNames = NULL;
    int count = 0;

    if (!headerIsEntry (h, RPMTAG_BASENAMES)
	|| !headerIsEntry (h, RPMTAG_DIRNAMES)
	|| !headerIsEntry (h, RPMTAG_DIRINDEXES))
	compressFilelist(h);
    
    rpmBuildFileList(h, &fileNames, &count);

    if (fileNames == NULL || count <= 0)
	return;

    headerAddEntry(h, RPMTAG_OLDFILENAMES, RPM_STRING_ARRAY_TYPE,
			fileNames, count);

    xfree(fileNames);
}

static int psGetArchScore(Header h) {
    void * pkgArch;
    int type, count;

    if (!headerGetEntry(h, RPMTAG_ARCH, &type, (void **) &pkgArch, &count) ||
        type == RPM_INT8_TYPE)
       return 150;
    else
        return rpmMachineScore(RPM_MACHTABLE_INSTARCH, pkgArch);
}

static int pkgCompareVer(void * first, void * second) {
    struct packageInfo ** a = first;
    struct packageInfo ** b = second;
    int ret, score1, score2;

    /* put packages w/o names at the end */
    if (!(*a)->name) return 1;
    if (!(*b)->name) return -1;

    ret = strcasecmp((*a)->name, (*b)->name);
    if (ret) return ret;
    score1 = psGetArchScore((*a)->h);
    if (!score1) return 1;
    score2 = psGetArchScore((*b)->h);
    if (!score2) return -1;
    if (score1 < score2) return -1;
    if (score1 > score2) return 1;
    return rpmVersionCompare((*b)->h, (*a)->h);
}

static void pkgSort(struct pkgSet * psp) {
    int i;
    char *name;

    qsort(psp->packages, psp->numPackages, sizeof(*psp->packages),
	 (void *) pkgCompareVer);

    name = psp->packages[0]->name;
    if (!name) {
       psp->numPackages = 0;
       return;
    }
    for (i = 1; i < psp->numPackages; i++) {
       if (!psp->packages[i]->name) break;
       if (!strcmp(psp->packages[i]->name, name))
	   psp->packages[i]->name = NULL;
       else
	   name = psp->packages[i]->name;
    }

    qsort(psp->packages, psp->numPackages, sizeof(*psp->packages),
	 (void *) pkgCompareVer);

    for (i = 0; i < psp->numPackages; i++)
       if (!psp->packages[i]->name) break;
    psp->numPackages = i;
}

static PyObject * findUpgradeSet(PyObject * self, PyObject * args) {
    PyObject * hdrList, * result;
    char * root = "/";
    int i;
    struct pkgSet list;
    hdrObject * hdr;

    if (!PyArg_ParseTuple(args, "O|s", &hdrList, &root)) return NULL;

    if (!PyList_Check(hdrList)) {
	PyErr_SetString(PyExc_TypeError, "list of headers expected");
	return NULL;
    }

    list.numPackages = PyList_Size(hdrList);
    list.packages = alloca(sizeof(list.packages) * list.numPackages);
    for (i = 0; i < list.numPackages; i++) {
	hdr = (hdrObject *) PyList_GetItem(hdrList, i);
	if (hdr->ob_type != &hdrType) {
	    PyErr_SetString(PyExc_TypeError, "list of headers expected");
	    return NULL;
	}
	list.packages[i] = alloca(sizeof(struct packageInfo));
	list.packages[i]->h = hdr->h;
	list.packages[i]->selected = 0;
	list.packages[i]->data = hdr;

	headerGetEntry(hdr->h, RPMTAG_NAME, NULL,
		      (void **) &list.packages[i]->name, NULL);
    }

    pkgSort (&list);

    if (ugFindUpgradePackages(&list, root)) {
	PyErr_SetString(pyrpmError, "error during upgrade check");
	return NULL;
    }

    result = PyList_New(0);
    for (i = 0; i < list.numPackages; i++) {
	if (list.packages[i]->selected) {
	    PyList_Append(result, list.packages[i]->data);
/*  	    Py_DECREF(list.packages[i]->data); */
	}
    }

    return result;
}



static rpmdbObject * rpmOpenDB(PyObject * self, PyObject * args) {
    rpmdbObject * o;
    char * root = "";
    int forWrite = 0;

    if (!PyArg_ParseTuple(args, "|is", &forWrite, &root)) return NULL;

    o = PyObject_NEW(rpmdbObject, &rpmdbType);
    o->db = NULL;
    o->offx = 0;
    o->noffs = 0;
    o->offsets = NULL;

    if (rpmdbOpen(root, &o->db, forWrite ? O_RDWR | O_CREAT: O_RDONLY, 0644)) {
	char * errmsg = "cannot open database in %s";
	char * errstr = NULL;
	int errsize;

	Py_DECREF(o);
	/* PyErr_SetString should take varargs... */
	errsize = strlen(errmsg) + *root == '\0' ? 15 /* "/var/lib/rpm" */ : strlen(root);
	errstr = alloca(errsize);
	snprintf(errstr, errsize, errmsg, *root == '\0' ? "/var/lib/rpm" : root);
	PyErr_SetString(pyrpmError, errstr);
	return NULL;
    }

    return o;
}

static PyObject * rebuildDB (PyObject * self, PyObject * args) {
    char * root = "";

    if (!PyArg_ParseTuple(args, "s", &root)) return NULL;

    return Py_BuildValue("i", rpmdbRebuild(root));
}

static PyObject * rpmReadHeaders (FD_t fd) {
    PyObject * list;
    Header header;
    hdrObject * h;

    if (!fd) {
	PyErr_SetFromErrno(pyrpmError);
	return NULL;
    }

    list = PyList_New(0);
    Py_BEGIN_ALLOW_THREADS
    header = headerRead(fd, HEADER_MAGIC_YES);

    Py_END_ALLOW_THREADS
    while (header) {
	compressFilelist (header);
	providePackageNVR (header);
	h = (hdrObject *) PyObject_NEW(PyObject, &hdrType);
	h->h = header;
	h->sigs = NULL;
	h->fileList = h->linkList = h->md5list = NULL;
	h->uids = h->gids = h->mtimes = h->fileSizes = NULL;
	h->modes = h->rdevs = NULL;
	if (PyList_Append(list, (PyObject *) h)) {
	    Py_DECREF(list);
	    Py_DECREF(h);
	    return NULL;
	}

	Py_DECREF(h);

	Py_BEGIN_ALLOW_THREADS
	header = headerRead(fd, HEADER_MAGIC_YES);
	Py_END_ALLOW_THREADS
    }

    return list;
}

static PyObject * rpmHeaderFromFD(PyObject * self, PyObject * args) {
    FD_t fd;
    int fileno;
    PyObject * list;

    if (!PyArg_ParseTuple(args, "i", &fileno)) return NULL;
    fd = fdDup(fileno);

    list = rpmReadHeaders (fd);
    Fclose(fd);

    return list;
}


static PyObject * hdrLoad(PyObject * self, PyObject * args) {
    char * obj;
    Header hdr;
    hdrObject * h;
    int len;

    if (!PyArg_ParseTuple(args, "s#", &obj, &len)) return NULL;

    hdr = headerLoad(obj);
    if (!hdr) {
	PyErr_SetString(pyrpmError, "bad header");
	return NULL;
    }
    compressFilelist (hdr);
    providePackageNVR (hdr);

    h = (hdrObject *) PyObject_NEW(PyObject, &hdrType);
    h->h = hdr;
    h->sigs = NULL;
    h->fileList = h->linkList = h->md5list = NULL;
    h->uids = h->gids = h->mtimes = h->fileSizes = NULL;
    h->modes = h->rdevs = NULL;

    return (PyObject *) h;
}

static PyObject * rpmHeaderFromFile(PyObject * self, PyObject * args) {
    char * filespec;
    FD_t fd;
    PyObject * list;

    if (!PyArg_ParseTuple(args, "s", &filespec)) return NULL;
    fd = Fopen(filespec, "r.fdio");

    if (!fd) {
	PyErr_SetFromErrno(pyrpmError);
	return NULL;
    }

    list = rpmReadHeaders (fd);
    Fclose(fd);

    return list;
}

static PyObject * errorCB = NULL, * errorData = NULL;

static void errorcb (void)
{
    PyObject * result, * args = NULL;

    if (errorData)
	args = Py_BuildValue("(O)", errorData);

    result = PyEval_CallObject(errorCB, args);
    Py_XDECREF(args);

    if (result == NULL) {
	PyErr_Print();
	PyErr_Clear();
    }
    Py_DECREF (result);
}

static PyObject * errorSetCallback (PyObject * self, PyObject * args) {
    if (errorCB != NULL) {
	Py_DECREF (errorCB);
	errorCB = NULL;
    }

    if (errorData != NULL) {
	Py_DECREF (errorData);
	errorData = NULL;
    }

    if (!PyArg_ParseTuple(args, "O|O", &errorCB, &errorData)) return NULL;

    /* if we're getting a void*, set the error callback to this. */
    /* also, we can possibly decref any python callbacks we had  */
    /* and set them to NULL.                                     */
    if (PyCObject_Check (errorCB)) {
	rpmErrorSetCallback (PyCObject_AsVoidPtr(errorCB));

	Py_XDECREF (errorCB);
	Py_XDECREF (errorData);

	errorCB   = NULL;
	errorData = NULL;
	
	Py_INCREF(Py_None);
	return Py_None;
    }
    
    if (!PyCallable_Check (errorCB)) {
	PyErr_SetString(PyExc_TypeError, "parameter must be callable");
	return NULL;
    }

    Py_INCREF (errorCB);
    Py_XINCREF (errorData);

    return PyCObject_FromVoidPtr(rpmErrorSetCallback (errorcb), NULL);
}

static PyObject * errorString (PyObject * self, PyObject * args) {
    return PyString_FromString(rpmErrorString ());
}

static PyObject * versionCompare (PyObject * self, PyObject * args) {
    hdrObject * h1, * h2;

    if (!PyArg_ParseTuple(args, "O!O!", &hdrType, &h1, &hdrType, &h2)) return NULL;

    return Py_BuildValue("i", rpmVersionCompare(h1->h, h2->h));
}

static PyObject * labelCompare (PyObject * self, PyObject * args) {
    char *v1, *r1, *e1, *v2, *r2, *e2;
    int rc;

    if (!PyArg_ParseTuple(args, "(zzz)(zzz)",
			  &e1, &v1, &r1,
			  &e2, &v2, &r2)) return NULL;

    if (e1 && !e2)
	return Py_BuildValue("i", 1);
    else if (!e1 && e2)
	return Py_BuildValue("i", -1);
    else if (e1 && e2) {
	int ep1, ep2;
	ep1 = atoi (e1);
	ep2 = atoi (e2);
	if (ep1 < ep2)
	    return Py_BuildValue("i", -1);
	else if (ep1 > ep2)
	    return Py_BuildValue("i", 1);
    }

    rc = rpmvercmp(v1, v2);
    if (rc)
	return Py_BuildValue("i", rc);

    return Py_BuildValue("i", rpmvercmp(r1, r2));
}

static PyObject * rpmHeaderFromPackage(PyObject * self, PyObject * args) {
    hdrObject * h;
    Header header;
    Header sigs;
    int rc;
    FD_t fd;
    int rawFd;
    int isSource = 0;

    if (!PyArg_ParseTuple(args, "i", &rawFd)) return NULL;
    fd = fdDup(rawFd);

    rc = rpmReadPackageInfo(fd, &sigs, &header);
    Fclose(fd);

    switch (rc) {
      case 0:
	h = (hdrObject *) PyObject_NEW(PyObject, &hdrType);
	h->h = header;
	h->sigs = sigs;
	h->fileList = h->linkList = h->md5list = NULL;
	h->uids = h->gids = h->mtimes = h->fileSizes = NULL;
	h->modes = h->rdevs = NULL;
	if (headerIsEntry(header, RPMTAG_SOURCEPACKAGE))
	    isSource = 1;
	break;

      case 1:
	Py_INCREF(Py_None);
	h = (hdrObject *) Py_None;
	break;

      default:
	PyErr_SetString(pyrpmError, "error reading package");
	return NULL;
    }

    return Py_BuildValue("(Ni)", h, isSource);
}

/* methods for rpmdb object */

static PyObject * rpmdbGetAttr(rpmdbObject * s, char * name) {
    return Py_FindMethod(rpmdbMethods, (PyObject * ) s, name);
}

static void rpmdbDealloc(rpmdbObject * s) {
    if (s->offsets) {
	free(s->offsets);
    }
    if (s->db) {
	rpmdbClose(s->db);
    }
    PyMem_DEL(s);
}

static PyObject * rpmdbFirst(rpmdbObject * s, PyObject * args) {
    int first;

    if (!PyArg_ParseTuple (args, "")) return NULL;

    /* Acquire all offsets in one fell swoop. */
    if (s->offsets == NULL || s->noffs <= 0) {
	rpmdbMatchIterator mi;
	Header h;

	if (s->offsets)
	    free(s->offsets);
	s->offsets = NULL;
	s->noffs = 0;
	mi = rpmdbInitIterator(s->db, RPMDBI_PACKAGES, NULL, 0);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
	    s->noffs++;
	    s->offsets = realloc(s->offsets, s->noffs * sizeof(s->offsets[0]));
	    s->offsets[s->noffs-1] = rpmdbGetIteratorOffset(mi);
	}
	rpmdbFreeIterator(mi);
    }

    s->offx = 0;
    if (s->offsets != NULL && s->offx < s->noffs)
	first = s->offsets[s->offx++];
    else
	first = 0;

    if (!first) {
	PyErr_SetString(pyrpmError, "cannot find first entry in database\n");
	return NULL;
    }

    return Py_BuildValue("i", first);
}

static PyObject * rpmdbNext(rpmdbObject * s, PyObject * args) {
    int where;

    if (!PyArg_ParseTuple (args, "i", &where)) return NULL;

    if (s->offsets == NULL || s->offx >= s->noffs) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    where = s->offsets[s->offx++];

    if (!where) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    return Py_BuildValue("i", where);
}

static PyObject * handleDbResult(rpmdbMatchIterator mi) {
    PyObject * list, *o;

    if (mi == NULL) {
	PyErr_SetString(pyrpmError, "error reading from database");
	return NULL;
    }

    list = PyList_New(0);

    /* XXX FIXME: unnecessary header mallocs are side effect here */
    while (rpmdbNextIterator(mi)) {
	PyList_Append(list, o=PyInt_FromLong(rpmdbGetIteratorOffset(mi)));
	Py_DECREF(o);
    }
    rpmdbFreeIterator(mi);

    return list;
}

static PyObject * rpmdbByName(rpmdbObject * s, PyObject * args) {
    char * str;

    if (!PyArg_ParseTuple(args, "s", &str)) return NULL;

    return handleDbResult(rpmdbInitIterator(s->db, RPMTAG_NAME, str, 0));
}

static PyObject * rpmdbByFile(rpmdbObject * s, PyObject * args) {
    char * str;

    if (!PyArg_ParseTuple(args, "s", &str)) return NULL;

    return handleDbResult(rpmdbInitIterator(s->db, RPMTAG_BASENAMES, str, 0));
}

static PyObject * rpmdbByProvides(rpmdbObject * s, PyObject * args) {
    char * str;

    if (!PyArg_ParseTuple(args, "s", &str)) return NULL;

    return handleDbResult(rpmdbInitIterator(s->db, RPMTAG_PROVIDENAME, str, 0));
}

static int
rpmdbLength(rpmdbObject * s) {
    int count = 0;

    {	rpmdbMatchIterator mi;

	/* RPMDBI_PACKAGES */
	mi = rpmdbInitIterator(s->db, RPMDBI_PACKAGES, NULL, 0);
	/* XXX FIXME: unnecessary header mallocs are side effect here */
	while (rpmdbNextIterator(mi) != NULL)
	    count++;
	rpmdbFreeIterator(mi);
    }

    return count;
}

static hdrObject *
rpmdbSubscript(rpmdbObject * s, PyObject * key) {
    int offset;
    hdrObject * h;

    if (!PyInt_Check(key)) {
	PyErr_SetString(PyExc_TypeError, "integer expected");
	return NULL;
    }

    offset = (int) PyInt_AsLong(key);

    h = PyObject_NEW(hdrObject, &hdrType);
    h->h = NULL;
    h->sigs = NULL;
    {	rpmdbMatchIterator mi;
	mi = rpmdbInitIterator(s->db, RPMDBI_PACKAGES, &offset, sizeof(offset));
	if ((h->h = rpmdbNextIterator(mi)) != NULL)
	    h->h = headerLink(h->h);
	rpmdbFreeIterator(mi);
    }
    h->fileList = h->linkList = h->md5list = NULL;
    h->uids = h->gids = h->mtimes = h->fileSizes = NULL;
    h->modes = h->rdevs = NULL;
    if (!h->h) {
	Py_DECREF(h);
	PyErr_SetString(pyrpmError, "cannot read rpmdb entry");
	return NULL;
    }

    return h;
}

static rpmdbMIObject *
py_rpmdbInitIterator (rpmdbObject * s, PyObject * args) {
    PyObject *index = NULL;
    char *key = NULL;
    int len = 0, tag = -1;
    rpmdbMIObject * mio;
    
    if (!PyArg_ParseTuple(args, "|Ozi", &index, &key, &len))
	return NULL;

    if (index == NULL)
	tag = 0;
    else if ((tag = tagNumFromPyObject (index)) == -1) {
	PyErr_SetString(PyExc_TypeError, "unknown tag type");
	return NULL;
    }
    
    mio = (rpmdbMIObject *) PyObject_NEW(rpmdbMIObject, &rpmdbMIType);
    if (mio == NULL) {
	PyErr_SetString(pyrpmError, "out of memory creating rpmdbMIObject");
	return NULL;
    }
    
    mio->mi = rpmdbInitIterator(s->db, tag, key, len);
    mio->db = s;
    Py_INCREF (mio->db);
    
    return mio;
}

static PyObject *
rpmdbMINext(rpmdbMIObject * s, PyObject * args) {
    /* XXX assume header? */
    Header h;
    hdrObject * ho;
    

    h = rpmdbNextIterator(s->mi);
    if (!h) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    ho = PyObject_NEW(hdrObject, &hdrType);
    ho->h = headerLink(h);
    ho->sigs = NULL;
    ho->fileList = ho->linkList = ho->md5list = NULL;
    ho->uids = ho->gids = ho->mtimes = ho->fileSizes = NULL;
    ho->modes = ho->rdevs = NULL;
    
    return (PyObject *) ho;
}

/* methods for rpmdbMatchIterator object */
static void rpmdbMIDealloc(rpmdbMIObject * s) {
    if (s && s->mi) {
	rpmdbFreeIterator(s->mi);
    }
    Py_DECREF (s->db);
    PyMem_DEL(s);
}

static PyObject * rpmdbMIGetAttr (rpmdbObject *s, char *name) {
    return Py_FindMethod (rpmdbMIMethods, (PyObject *) s, name);
}

/* methods for header object */

static void hdrDealloc(hdrObject * s) {
    if (s->h) headerFree(s->h);
    if (s->sigs) headerFree(s->sigs);
    if (s->md5list) free(s->md5list);
    if (s->fileList) free(s->fileList);
    if (s->linkList) free(s->linkList);
    PyMem_DEL(s);
}

static PyObject * hdrGetAttr(hdrObject * s, char * name) {
    return Py_FindMethod(hdrMethods, (PyObject * ) s, name);
}

static long tagNumFromPyObject (PyObject *item)
{
    char * str;
    int i;

    if (PyInt_Check(item)) {
	return PyInt_AsLong(item);
    } else if (PyString_Check(item)) {
	str = PyString_AsString(item);
	for (i = 0; i < rpmTagTableSize; i++)
	    if (!strcasecmp(rpmTagTable[i].name + 7, str)) break;
	if (i < rpmTagTableSize) return rpmTagTable[i].val;
    }
    return -1;
}

static PyObject * hdrSubscript(hdrObject * s, PyObject * item) {
    int type, count, i, tag = -1;
    void * data;
    PyObject * o, * metao;
    char ** stringArray;
    int forceArray = 0;
    int freeData = 0;
    char * str;
    struct headerSprintfExtension * ext = NULL;
    const struct headerSprintfExtension * extensions = rpmHeaderFormats;

    if (PyCObject_Check (item))
        ext = PyCObject_AsVoidPtr(item);
    else
	tag = tagNumFromPyObject (item);
    if (tag == -1 && PyString_Check(item)) {
	/* if we still don't have the tag, go looking for the header
	   extensions */
	str = PyString_AsString(item);
	while (extensions->name) {
	    if (extensions->type == HEADER_EXT_TAG
		&& !strcasecmp(extensions->name + 7, str)) {
		(const struct headerSprintfExtension *) ext = extensions;
	    }
	    extensions++;
	}
    }

    if (ext) {
        ext->u.tagFunction(s->h, &type, (const void **) &data, &count, &freeData);
    } else {
        if (tag == -1) {
            PyErr_SetString(PyExc_KeyError, "unknown header tag");
            return NULL;
        }
        
        if (!rpmPackageGetEntry(NULL, s->sigs, s->h, tag, &type, &data, &count))
	{
            Py_INCREF(Py_None);
            return Py_None;
        }
    }

    switch (tag) {
      case RPMTAG_OLDFILENAMES:
      case RPMTAG_FILESIZES:
      case RPMTAG_FILESTATES:
      case RPMTAG_FILEMODES:
      case RPMTAG_FILEUIDS:
      case RPMTAG_FILEGIDS:
      case RPMTAG_FILERDEVS:
      case RPMTAG_FILEMTIMES:
      case RPMTAG_FILEMD5S:
      case RPMTAG_FILELINKTOS:
      case RPMTAG_FILEFLAGS:
      case RPMTAG_ROOT:
      case RPMTAG_FILEUSERNAME:
      case RPMTAG_FILEGROUPNAME:
	forceArray = 1;
	break;
      case RPMTAG_SUMMARY:
      case RPMTAG_GROUP:
      case RPMTAG_DESCRIPTION:
	freeData = 1;
	break;
      default:
        break;
    }

    switch (type) {
      case RPM_BIN_TYPE:
	o = PyString_FromStringAndSize(data, count);
	break;

      case RPM_INT32_TYPE:
	if (count != 1 || forceArray) {
	    metao = PyList_New(0);
	    for (i = 0; i < count; i++) {
		o = PyInt_FromLong(((int *) data)[i]);
		PyList_Append(metao, o);
		Py_DECREF(o);
	    }
	    o = metao;
	} else {
	    o = PyInt_FromLong(*((int *) data));
	}
	break;

      case RPM_CHAR_TYPE:
      case RPM_INT8_TYPE:
	if (count != 1 || forceArray) {
	    metao = PyList_New(0);
	    for (i = 0; i < count; i++) {
		o = PyInt_FromLong(((char *) data)[i]);
		PyList_Append(metao, o);
		Py_DECREF(o);
	    }
	    o = metao;
	} else {
	    o = PyInt_FromLong(*((char *) data));
	}
	break;

      case RPM_INT16_TYPE:
	if (count != 1 || forceArray) {
	    metao = PyList_New(0);
	    for (i = 0; i < count; i++) {
		o = PyInt_FromLong(((short *) data)[i]);
		PyList_Append(metao, o);
		Py_DECREF(o);
	    }
	    o = metao;
	} else {
	    o = PyInt_FromLong(*((short *) data));
	}
	break;

      case RPM_STRING_ARRAY_TYPE:
	stringArray = data;

	metao = PyList_New(0);
	for (i = 0; i < count; i++) {
	    o = PyString_FromString(stringArray[i]);
	    PyList_Append(metao, o);
	    Py_DECREF(o);
	}
	free (stringArray);
	o = metao;
	break;

      case RPM_STRING_TYPE:
	if (count != 1 || forceArray) {
	    stringArray = data;

	    metao = PyList_New(0);
	    for (i=0; i < count; i++) {
		o = PyString_FromString(stringArray[i]);
		PyList_Append(metao, o);
		Py_DECREF(o);
	    }
	    o = metao;
	} else {
	    o = PyString_FromString(data);
	    if (freeData)
		free (data);
	}
	break;

      default:
	PyErr_SetString(PyExc_TypeError, "unsupported type in header");
	return NULL;
    }

    return o;
}

static PyObject * hdrKeyList(hdrObject * s, PyObject * args) {
    PyObject * list, *o;
    HeaderIterator iter;
    int tag, type;

    if (!PyArg_ParseTuple(args, "")) return NULL;

    list = PyList_New(0);

    iter = headerInitIterator(s->h);
    while (headerNextIterator(iter, &tag, &type, NULL, NULL)) {
        if (tag == HEADER_I18NTABLE) continue;

	switch (type) {
	  case RPM_BIN_TYPE:
	  case RPM_INT32_TYPE:
	  case RPM_CHAR_TYPE:
	  case RPM_INT8_TYPE:
	  case RPM_INT16_TYPE:
	  case RPM_STRING_ARRAY_TYPE:
	  case RPM_STRING_TYPE:
	    PyList_Append(list, o=PyInt_FromLong(tag));
	    Py_DECREF(o);
	}
    }

    headerFreeIterator(iter);

    return list;
}

static PyObject * hdrUnload(hdrObject * s, PyObject * args) {
    char * buf;
    int len;
    PyObject * rc;

    len = headerSizeof(s->h, 0);
    buf = headerUnload(s->h);

    rc = PyString_FromStringAndSize(buf, len);
    free(buf);

    return rc;
}

/* Returns a list of these tuple for each part which failed:

	(attr_name, correctValue, currentValue)

	It should be passwd the file number to verify.
*/
static PyObject * hdrVerifyFile(hdrObject * s, PyObject * args) {
    int fileNumber;
    int verifyResult;
    PyObject * list, * tuple, * attrName;
    int type, count;
    struct stat sb;
    char buf[2048];
    int i;
    time_t timeInt;
    struct tm * timeStruct;

    if (!PyInt_Check(args)) {
	PyErr_SetString(PyExc_TypeError, "integer expected");
	return NULL;
    }

    fileNumber = (int) PyInt_AsLong(args);

    if (rpmVerifyFile("", s->h, fileNumber, &verifyResult, 0)) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    list = PyList_New(0);

    if (!verifyResult) return list;

    if (!s->fileList) {
	headerGetEntry(s->h, RPMTAG_OLDFILENAMES, &type, (void **) &s->fileList,
		 &count);
    }

    lstat(s->fileList[fileNumber], &sb);

    if (verifyResult & RPMVERIFY_MD5) {
	if (!s->md5list) {
	    headerGetEntry(s->h, RPMTAG_FILEMD5S, &type, (void **) &s->md5list,
		     &count);
	}

	if (mdfile(s->fileList[fileNumber], buf)) {
	    strcpy(buf, "(unknown)");
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("checksum");
	PyTuple_SetItem(tuple, 0, attrName);
	PyTuple_SetItem(tuple, 1, PyString_FromString(s->md5list[fileNumber]));
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_FILESIZE) {
	if (!s->fileSizes) {
	    headerGetEntry(s->h, RPMTAG_FILESIZES, &type, (void **) &s->fileSizes,
		     &count);

	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("size");
	PyTuple_SetItem(tuple, 0, attrName);

	sprintf(buf, "%d", 100);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));
	sprintf(buf, "%ld", sb.st_size);
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_LINKTO) {
	if (!s->linkList) {
	    headerGetEntry(s->h, RPMTAG_FILELINKTOS, &type, (void **) &s->linkList,
		     &count);
	}

	i = readlink(s->fileList[fileNumber], buf, sizeof(buf));
	if (i <= 0)
	    strcpy(buf, "(unknown)");
	else
	    buf[i] = '\0';

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("link");
	PyTuple_SetItem(tuple, 0, attrName);
	PyTuple_SetItem(tuple, 1, PyString_FromString(s->linkList[fileNumber]));
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_MTIME) {
	if (!s->mtimes) {
	    headerGetEntry(s->h, RPMTAG_FILEMTIMES, &type, (void **) &s->mtimes,
		     &count);
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("time");
	PyTuple_SetItem(tuple, 0, attrName);

	timeInt = sb.st_mtime;
	timeStruct = localtime(&timeInt);
	strftime(buf, sizeof(buf) - 1, "%c", timeStruct);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));

	timeInt = s->mtimes[fileNumber];
	timeStruct = localtime(&timeInt);
	strftime(buf, sizeof(buf) - 1, "%c", timeStruct);

	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));

	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_RDEV) {
	if (!s->rdevs) {
	    headerGetEntry(s->h, RPMTAG_FILERDEVS, &type, (void **) &s->rdevs,
		     &count);
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("device");

	PyTuple_SetItem(tuple, 0, attrName);
	sprintf(buf, "0x%-4x", s->rdevs[fileNumber]);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));
	sprintf(buf, "0x%-4x", (unsigned int) sb.st_rdev);
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    /* RPMVERIFY_USER and RPM_VERIFY_GROUP are handled wrong here, but rpmlib.a
       doesn't do these correctly either. At least this is consisten */
    if (verifyResult & RPMVERIFY_USER) {
	if (!s->uids) {
	    headerGetEntry(s->h, RPMTAG_FILEUIDS, &type, (void **) &s->uids,
		     &count);
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("uid");
	PyTuple_SetItem(tuple, 0, attrName);
	sprintf(buf, "%d", s->uids[fileNumber]);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));
	sprintf(buf, "%d", sb.st_uid);
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_GROUP) {
	if (!s->gids) {
	    headerGetEntry(s->h, RPMTAG_FILEGIDS, &type, (void **) &s->gids,
		     &count);
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("gid");
	PyTuple_SetItem(tuple, 0, attrName);
	sprintf(buf, "%d", s->gids[fileNumber]);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));
	sprintf(buf, "%d", sb.st_gid);
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    if (verifyResult & RPMVERIFY_MODE) {
	if (!s->modes) {
	    headerGetEntry(s->h, RPMTAG_FILEMODES, &type, (void **) &s->modes,
		     &count);
	}

	tuple = PyTuple_New(3);
	attrName = PyString_FromString("permissions");
	PyTuple_SetItem(tuple, 0, attrName);
	sprintf(buf, "0%-4o", s->modes[fileNumber]);
	PyTuple_SetItem(tuple, 1, PyString_FromString(buf));
	sprintf(buf, "0%-4o", sb.st_mode);
	PyTuple_SetItem(tuple, 2, PyString_FromString(buf));
	PyList_Append(list, tuple);
	Py_DECREF(tuple);
    }

    return list;
}

static PyObject * hdrCompressFilelist(hdrObject * s, PyObject * args) {
    compressFilelist (s->h);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * hdrExpandFilelist(hdrObject * s, PyObject * args) {
    expandFilelist (s->h);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * hdrFullFilelist(hdrObject * s, PyObject * args) {
    mungeFilelist (s->h);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * rpmtransCreate(PyObject * self, PyObject * args) {
    rpmtransObject * o;
    rpmdbObject * db = NULL;
    char * rootPath = "/";

    if (!PyArg_ParseTuple(args, "|sO", &rootPath, &db)) return NULL;
    if (db && db->ob_type != &rpmdbType) {
	PyErr_SetString(PyExc_TypeError, "bad type for database argument");
	return NULL;
    }

    o = (void *) PyObject_NEW(rpmtransObject, &rpmtransType);

    Py_XINCREF(db);
    o->dbo = db;
    o->scriptFd = NULL;
    o->ts = rpmtransCreateSet(db ? db->db : NULL, rootPath);
    o->keyList = PyList_New(0);

    return (void *) o;
}

static void rpmtransDealloc(PyObject * o) {
    rpmtransObject * trans = (void *) o;

    rpmtransFree(trans->ts);
    if (trans->dbo) {
	Py_DECREF(trans->dbo);
    }
    if (trans->scriptFd) Fclose(trans->scriptFd);
    /* this will free the keyList, and decrement the ref count of all
       the items on the list as well :-) */
    Py_DECREF(trans->keyList);
    PyMem_DEL(o);
}

static PyObject * rpmtransGetAttr(rpmtransObject * o, char * name) {
    return Py_FindMethod(rpmtransMethods, (PyObject *) o, name);
}

static int rpmtransSetAttr(rpmtransObject * o, char * name,
			   PyObject * val) {
    int i;

    if (!strcmp(name, "scriptFd")) {
	if (!PyArg_Parse(val, "i", &i)) return 0;
	if (i < 0) {
	    PyErr_SetString(PyExc_TypeError, "bad file descriptor");
	    return -1;
	} else {
	    o->scriptFd = fdDup(i);
	    rpmtransSetScriptFd(o->ts, o->scriptFd);
	}
    } else {
	PyErr_SetString(PyExc_AttributeError, name);
	return -1;
    }

    return 0;
}

static PyObject * rpmtransAdd(rpmtransObject * s, PyObject * args) {
    hdrObject * h;
    PyObject * key;
    char * how = NULL;
    int isUpgrade = 0;

    if (!PyArg_ParseTuple(args, "OO|s", &h, &key, &how)) return NULL;
    if (h->ob_type != &hdrType) {
	PyErr_SetString(PyExc_TypeError, "bad type for header argument");
	return NULL;
    }

    if (how && strcmp(how, "a") && strcmp(how, "u") && strcmp(how, "i")) {
	PyErr_SetString(PyExc_TypeError, "how argument must be \"u\", \"a\", or \"i\"");
	return NULL;
    } else if (how && !strcmp(how, "u"))
    	isUpgrade = 1;

    if (how && !strcmp(how, "a"))
	rpmtransAvailablePackage(s->ts, h->h, key);
    else
	rpmtransAddPackage(s->ts, h->h, NULL, key, isUpgrade, NULL);

    /* This should increment the usage count for me */
    if (key) {
	PyList_Append(s->keyList, key);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * rpmtransRemove(rpmtransObject * s, PyObject * args) {
    char * name;
    int count;
    rpmdbMatchIterator mi;
    
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    /* XXX: Copied hack from ../lib/rpminstall.c, rpmErase() */
    mi = rpmdbInitIterator(s->dbo->db, RPMDBI_LABEL, name, 0);
    count = rpmdbGetIteratorCount(mi);
    if (count <= 0) {
        PyErr_SetString(pyrpmError, "package not installed");
        return NULL;
    } else { /* XXX: Note that we automatically choose to remove all matches */
        Header h;
        while ((h = rpmdbNextIterator(mi)) != NULL) {
	    unsigned int recOffset = rpmdbGetIteratorOffset(mi);
	    if (recOffset) {
	        rpmtransRemovePackage(s->ts, recOffset);
	    }
	}
    }
    rpmdbFreeIterator(mi);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * rpmtransOrder(rpmtransObject * s, PyObject * args) {
    if (!PyArg_ParseTuple(args, "")) return NULL;

    rpmdepOrder(s->ts);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * rpmtransDepCheck(rpmtransObject * s, PyObject * args) {
    struct rpmDependencyConflict * conflicts;
    int numConflicts;
    PyObject * list, * cf;
    int i;

    if (!PyArg_ParseTuple(args, "")) return NULL;

    rpmdepCheck(s->ts, &conflicts, &numConflicts);
    if (numConflicts) {
	list = PyList_New(0);

	for (i = 0; i < numConflicts; i++) {
	    cf = Py_BuildValue("((sss)(ss)iOi)", conflicts[i].byName,
			       conflicts[i].byVersion, conflicts[i].byRelease,

			       conflicts[i].needsName,
			       conflicts[i].needsVersion,

			       conflicts[i].needsFlags,
			       conflicts[i].suggestedPackage ?
				   conflicts[i].suggestedPackage : Py_None,
			       conflicts[i].sense);
	    PyList_Append(list, (PyObject *) cf);
	    Py_DECREF(cf);
	}

	rpmdepFreeConflicts(conflicts, numConflicts);

	return list;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

struct tsCallbackType {
    PyObject * cb;
    PyObject * data;
    int pythonError;
};



static void * tsCallback(const Header h, const rpmCallbackType what,
		         const unsigned long amount, const unsigned long total,
	                 const void * pkgKey, void * data) {
    struct tsCallbackType * cbInfo = data;
    PyObject * args, * result;
    int fd;
    static FD_t fdt;

    if (cbInfo->pythonError) return NULL;

    if (!pkgKey) pkgKey = Py_None;
    transactionSetHeader = h;    

    args = Py_BuildValue("(illOO)", what, amount, total, pkgKey, cbInfo->data);
    result = PyEval_CallObject(cbInfo->cb, args);
    Py_DECREF(args);

    if (!result) {
	cbInfo->pythonError = 1;
	return NULL;
    }

    if (what == RPMCALLBACK_INST_OPEN_FILE) {
        if (!PyArg_Parse(result, "i", &fd)) {
	    cbInfo->pythonError = 1;
	    return NULL;
	}
	fdt = fdDup(fd);
	
	Py_DECREF(result);
	return fdt;
    }

    if (what == RPMCALLBACK_INST_CLOSE_FILE) {
	Fclose (fdt);
    }

    Py_DECREF(result);

    return NULL;
}

static PyObject * rpmtransRun(rpmtransObject * s, PyObject * args) {
    int flags, ignoreSet;
    int rc, i;
    PyObject * list, * prob;
    rpmProblemSet probs;
    struct tsCallbackType cbInfo;

    if (!PyArg_ParseTuple(args, "iiOO", &flags, &ignoreSet, &cbInfo.cb,
			  &cbInfo.data))
	return NULL;

    cbInfo.pythonError = 0;

    rc = rpmRunTransactions(s->ts, tsCallback, &cbInfo, NULL, &probs, flags,
			    ignoreSet);

    if (cbInfo.pythonError) {
	if (rc > 0)
	    rpmProblemSetFree(probs);
	return NULL;
    }

    if (rc < 0) {
	list = PyList_New(0);
	return list;
    } else if (!rc) {
	Py_INCREF(Py_None);
	return Py_None;
    }

    list = PyList_New(0);
    for (i = 0; i < probs->numProblems; i++) {
	rpmProblem myprob = probs->probs + i;
	prob = Py_BuildValue("s(isi)", rpmProblemString(myprob),
			     myprob->type,
			     myprob->str1,
			     myprob->ulong1);
	PyList_Append(list, prob);
	Py_DECREF(prob);
    }

    rpmProblemSetFree(probs);

    return list;
}

static PyObject * archScore(PyObject * self, PyObject * args) {
    char * arch;
    int score;

    if (!PyArg_ParseTuple(args, "s", &arch))
	return NULL;

    score = rpmMachineScore(RPM_MACHTABLE_INSTARCH, arch);

    return Py_BuildValue("i", score);
}

static PyObject * doAddMacro(PyObject * self, PyObject * args) {
    char * name, * val;

    if (!PyArg_ParseTuple(args, "ss", &name, &val))
	return NULL;

    addMacro(NULL, name, NULL, val, RMIL_DEFAULT);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * doDelMacro(PyObject * self, PyObject * args) {
    char * name;

    if (!PyArg_ParseTuple(args, "s", &name))
	return NULL;

    delMacro(NULL, name);

    Py_INCREF(Py_None);
    return Py_None;
}

typedef struct FDlist_t FDlist;

struct FDlist_t {
    FILE *f;
    FD_t fd;
    char *note;
    FDlist *next;
} ;

static FDlist *fdhead = NULL;
static FDlist *fdtail = NULL;

static int closeCallback(FILE * f) {
    FDlist *node, *last;

    printf ("close callback on %p\n", f);
    
    node = fdhead;
    last = NULL;
    while (node) {
        if (node->f == f)
            break;
        last = node;
        node = node->next;
    }
    if (node) {
        if (last)
            last->next = node->next;
        else
            fdhead = node->next;
        printf ("closing %s %p\n", node->note, node->fd);
	free (node->note);
        node->fd = fdLink(node->fd, "closeCallback");
        Fclose (node->fd);
        while (node->fd)
            node->fd = fdFree(node->fd, "closeCallback");
        free (node);
    }
    return 0; 
}

static PyObject * checkSig (PyObject * self, PyObject * args) {
    char * filename;
    int flags;
    int rc = 255;

    if (PyArg_ParseTuple(args, "si", &filename, &flags)) {
	const char *av[2];
	av[0] = filename;
	av[1] = NULL;
	rc = rpmCheckSig(flags, av);
    }
    return Py_BuildValue("i", rc);
}


static PyObject * getTsHeader (PyObject * self, PyObject * args) {
    hdrObject * h;
    
    if (transactionSetHeader) {
	h = (hdrObject *) PyObject_NEW(PyObject, &hdrType);
	h->h = headerLink(transactionSetHeader);
	h->sigs = NULL;
	h->fileList = h->linkList = h->md5list = NULL;
	h->uids = h->gids = h->mtimes = h->fileSizes = NULL;
	h->modes = h->rdevs = NULL;
	return (PyObject *) h;
    }
    Py_INCREF(Py_None);
    return (PyObject *) Py_None;
}

/* disable 
static PyObject * doFopen(PyObject * self, PyObject * args) {
    char * path, * mode;
    FDlist *node;
    
    if (!PyArg_ParseTuple(args, "ss", &path, &mode))
	return NULL;
    
    node = malloc (sizeof(FDlist));
    
    node->fd = Fopen(path, mode);
    node->fd = fdLink(node->fd, "doFopen");
    node->note = strdup (path);

    if (!node->fd) {
	PyErr_SetFromErrno(pyrpmError);
        free (node);
	return NULL;
    }
    
    if (Ferror(node->fd)) {
	const char *err = Fstrerror(node->fd);
        free(node);
	if (err) {
	    PyErr_SetString(pyrpmError, err);
	    return NULL;
	}
    }
    node->f = fdGetFp(node->fd);
    printf ("opening %s fd = %p f = %p\n", node->note, node->fd, node->f);
    if (!node->f) {
	PyErr_SetString(pyrpmError, "FD_t has no FILE*");
        free(node);
	return NULL;
    }

    node->next = NULL;
    if (!fdhead) {
	fdhead = fdtail = node;
    } else if (fdtail) {
        fdtail->next = node;
    } else {
        fdhead = node;
    }
    fdtail = node;
    
    return PyFile_FromFile (node->f, path, mode, closeCallback);
}
*/

