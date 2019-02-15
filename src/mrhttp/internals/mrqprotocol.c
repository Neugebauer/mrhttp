
#include "mrqprotocol.h"
#include "mrqclient.h"
#include "Python.h"
#include "common.h"
#include <errno.h>
#include <string.h>

static void print_buffer( char* b, int len ) {
  for ( int z = 0; z < len; z++ ) {
    printf( "%02x ",(int)b[z]);
  }
  printf("\n");
}

PyObject * MrqProtocol_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  MrqProtocol* self = NULL;
  DBG_MRQ printf("Mrq protocol new\n");

  self = (MrqProtocol*)type->tp_alloc(type, 0);
  if(!self) goto finally;

  self->transport = NULL;
  self->write = NULL;
  self->client = NULL;

  finally:
  return (PyObject*)self;
}

void MrqProtocol_dealloc(MrqProtocol* self)
{
  free(self->b);
  free(self->gb);
  free(self->rbuf);
  Py_XDECREF(self->transport);
  Py_XDECREF(self->write);
  Py_DECREF(self->q);
  Py_DECREF(self->client);
  Py_TYPE(self)->tp_free((PyObject*)self);
}

int MrqProtocol_init(MrqProtocol* self, PyObject *args, PyObject *kw)
{
  DBG_MRQ printf("Mrq protocol init\n");
  self->closed = true;

  if(!PyArg_ParseTuple(args, "OiO", &self->client, &self->server_num, &self->q)) return -1;
  Py_INCREF(self->client);
  Py_INCREF(self->q);

  if(!(self->pfunc = PyObject_GetAttrString(self->q, "put_nowait"))) return -1;

  self->b = malloc(1024);
  self->bsz = 1024;
  self->b[0] = 0;
  self->b[1] = 1;
  self->b[2] = 0;
  self->b[3] = 0;
  self->bb = self->b+8;
  self->bp4 = (int*)(self->b+4);

  self->gb = malloc(1024);
  self->gbsz = 1024;
  self->gb[0] = 0;
  self->gb[1] = 0xB;
  self->gb[2] = 0;
  self->gb[3] = 0;

  self->rbufsz = 4*1024;
  self->rbuf = malloc(self->rbufsz);
  self->rbufp = NULL;

  DBG_MRQ printf("Mrq protocol init end\n");
  return 0;
}

PyObject* MrqProtocol_connection_made(MrqProtocol* self, PyObject* transport)
{
  DBG_MRQ printf("MrqProtocol conn made\n");
  self->transport = transport;
  Py_INCREF(self->transport);

  self->closed = false;

  if(!(self->write      = PyObject_GetAttrString(transport, "write"))) return NULL;

  MrqClient_addConnection( (MrqClient*)(self->client), self, self->server_num );


  //printf("Mrq protocol made\n");
  //PyObject* connections = NULL;
  //PyObject* setconn = NULL;
  //if(!(setconn = PyObject_GetAttrString(self->client, "setConnection"))) return NULL;
  //printf("Mrq protocol made\n");
  //PyObject* tmp = PyObject_CallFunctionObjArgs(setconn, (PyObject*)self, NULL);
  //if(!tmp) return NULL;
  //printf("Mrq protocol made\n");
  //Py_DECREF(tmp);
  //if(!(connections = PyObject_GetAttrString(self->client, "_connections"))) return NULL;
  //if(PyList_Add(connections, (PyObject*)self) == -1) return NULL;
  //Py_XDECREF(connections);

  //return (PyObject*)self;
  Py_RETURN_NONE;
}

void* MrqProtocol_close(MrqProtocol* self)
{
  void* result = self;

  PyObject* close = PyObject_GetAttrString(self->transport, "close");
  if(!close) return NULL;
  PyObject* tmp = PyObject_CallFunctionObjArgs(close, NULL);
  Py_XDECREF(close);
  if(!tmp) return NULL;
  Py_DECREF(tmp);
  self->closed = true;

  return result;

}

PyObject* MrqProtocol_eof_received(MrqProtocol* self) {
  DBG_MRQ printf("MrqProtocol eof received\n");
  Py_RETURN_NONE; // Closes the connection and conn lost will be called next
}

PyObject* MrqProtocol_connection_lost(MrqProtocol* self, PyObject* args)
{
  DBG_MRQ printf("MrqProtocol conn lost\n");
  self->closed = true;
  MrqClient_connection_lost((MrqClient*)self->client, self );

  Py_RETURN_NONE;
}

PyObject* MrqProtocol_data_received(MrqProtocol* self, PyObject* data)
{
  // TODO Handle the pause/resume msg
   DBG_MRQ printf("mrq protocol - data recvd\n");
   //PyObject_Print( data, stdout, 0 ); 
   //printf("\n");

  //PyObject_CallFunctionObjArgs(self->pfunc, Py_True, NULL);
  //Py_RETURN_NONE;

  unsigned char* p;
  Py_ssize_t psz;

  if(PyBytes_AsStringAndSize(data, &p, &psz) == -1) {
    printf("WARNING py bytes as string failed\n");
    return NULL; //TODO set error
  }
  DBG_MRQ printf(" psz %ld\n",psz);
  int data_left = psz;
  if ( self->rbufp ) {
    DBG_MRQ printf(" rbufp! --------------------------------------------------------\n");
    //print_buffer( self->buf, self->bufp-self->buf);

    int rblen = self->rbufp-self->rbuf;
    if ( (rblen + psz) > self->rbufsz ) {
      while ( (rblen + psz) > self->rbufsz ) self->rbufsz <<= 1;
      self->rbuf = realloc( self->rbuf, self->rbufsz ); 
      self->rbufp = self->rbuf + rblen;
    }
    memcpy(self->rbufp, p, psz);
    data_left = data_left + self->rbufp - self->rbuf;
    p = self->rbuf;
    self->rbufp = NULL;
  }

  while ( data_left > 0 ) {
    DBG_MRQ printf(" top  while dl %d\n",data_left);

    if ( data_left < 4 ) {
      DBG_MRQ printf("Received partial data need 4 ------------------------------------\n");
      if ( self->rbufp == NULL ) self->rbufp = self->rbuf;

      int rblen = self->rbufp-self->rbuf;
      if ( (rblen + psz) > self->rbufsz ) {
        while ( (rblen + psz) > self->rbufsz ) self->rbufsz <<= 1;
        self->rbuf = realloc( self->rbuf, self->rbufsz ); 
        self->rbufp = self->rbuf + rblen;
      }

      memcpy(self->rbufp, p, data_left);
      self->rbufp += data_left;
      Py_RETURN_NONE;
    }

    if ( p[0] == 2 ) { 

      //print_buffer(p, 5);
      //int len   = *((int*)(p)+1);
      int len   = p[1] | (p[2]<<8) | (p[3]<<16) | (p[4]<<24);
      DBG_MRQ printf("cmd dl %d len %d\n",data_left,len);

      if ( data_left < len ) {
        DBG_MRQ printf("Received partial data dl %d need %d\n",data_left,len);
        if ( self->rbufp == NULL ) self->rbufp = self->rbuf;

        int rblen = self->rbufp-self->rbuf;
        if ( (rblen + psz) > self->rbufsz ) {
          while ( (rblen + psz) > self->rbufsz ) self->rbufsz <<= 1;
          self->rbuf = realloc( self->rbuf, self->rbufsz ); 
          self->rbufp = self->rbuf + rblen;
        }

        memcpy(self->rbufp, p, data_left);
        self->rbufp += data_left;
        Py_RETURN_NONE;
      }
      p += 5;
      data_left -= len + 5;

      PyObject *b = PyBytes_FromStringAndSize(p,len);
      p += len;
      //PyObject_Print( b, stdout, 0 ); 
      //printf("\n");

      DBG_MRQ printf("putting on q %p\n",self->q);
      DBG_MRQ printf("pfunc        %p\n",self->pfunc);
      if(!PyObject_CallFunctionObjArgs(self->pfunc, b, NULL)) { printf("WTF\n"); Py_XDECREF(b); return NULL; }
      DBG_MRQ printf("after putting on q\n");
      Py_DECREF(b);

    } else {
      printf("Unrecognized cmd %d\n", p[0]);
      return NULL;
    }
  }

  //tMrqCallback cb = self->queue[0].cb;
  //cb(self->queue[0].connection);
  Py_RETURN_NONE;
}

int MrqProtocol_get(MrqProtocol* self, int slot, char *d, int dsz) {

  self->gb[2] = (char)((dsz&0xFF00)>>8);
  self->gb[3] = (char)(dsz&0xFF);

  memcpy(self->gb+4, d, dsz);
  PyObject *bytes = PyBytes_FromStringAndSize(self->gb, dsz + 4);
  if ( bytes ) {
    if(!PyObject_CallFunctionObjArgs(self->write, bytes, NULL)) { Py_XDECREF(bytes); return 1; }
    Py_DECREF(bytes);
  } else {
    printf("DELME WHAT?\n");
  }
  return 0;
}

int MrqProtocol_push(MrqProtocol* self, int topic, int slot, char *d, int dsz) {

  if ( dsz > 10*1024 ) return -1;

  if ( dsz > self->bsz ) {
    self->bsz = 12*1024;
    self->b = realloc( self->b, self->bsz ); 
    self->bb = self->b+8;
    self->bp4 = (int*)(self->b+4);
  }

  self->b[2] = topic;  
  self->b[3] = topic;  

  //int *p_len = (int*)(self->bp4);
  *self->bp4 = dsz;
  memcpy(self->bb, d, dsz);

  PyObject *bytes = PyBytes_FromStringAndSize(self->b, dsz + 8);
  //DBG_MRQ PyObject_Print(bytes, stdout,0); 
  //DBG_MRQ printf("\n");
  if(!PyObject_CallFunctionObjArgs(self->write, bytes, NULL)) { Py_XDECREF(bytes); return 1; }
  Py_DECREF(bytes);

  return 0;
}
