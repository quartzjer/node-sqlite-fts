/*
Copyright (c) 2010, Orlando Vazquez <ovazquez@gmail.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <string.h>
#include <v8.h>
#include <node.h>

#include "sqlite3_bindings.h"
#include "database.h"
#include "statement.h"

using namespace v8;
using namespace node;

void Database::Init(v8::Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);

  constructor_template = Persistent<FunctionTemplate>::New(t);
//  constructor_template->Inherit(EventEmitter::constructor_template);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("Database"));

  NODE_SET_PROTOTYPE_METHOD(constructor_template, "open", Open);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "close", Close);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "prepare", Prepare);
  NODE_SET_PROTOTYPE_METHOD(constructor_template, "prepareAndStep", PrepareAndStep);

  target->Set(v8::String::NewSymbol("Database"),
          constructor_template->GetFunction());

  // insert/update execution result mask
  NODE_DEFINE_CONSTANT (target, EXEC_EMPTY);
  NODE_DEFINE_CONSTANT (target, EXEC_LAST_INSERT_ID);
  NODE_DEFINE_CONSTANT (target, EXEC_AFFECTED_ROWS);

  Statement::Init(target);
}

Handle<Value> Database::New(const Arguments& args) {
  HandleScope scope;
  Database* dbo = new Database();
  dbo->Wrap(args.This());
  return args.This();
}


void Database::UVWORK_AfterOpen(uv_work_t *req) {
  HandleScope scope;
  struct open_request *open_req = (struct open_request *)(req->data);

  Local<Value> argv[1];
  bool err = false;
  if (open_req->result) {
    err = true;
    argv[0] = Exception::Error(String::New("Error opening database"));
  }

  TryCatch try_catch;

  open_req->dbo->Unref();
  open_req->cb->Call(Context::GetCurrent()->Global(), err ? 1 : 0, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  //v8::Local<v8::String> event(String::New("ready"));
  open_req->dbo->Emit("ready", 0, NULL);
  open_req->cb.Dispose();

  free(open_req);
  delete req;
}

void Database::UVWORK_Open(uv_work_t *req) {
  struct open_request *open_req = (struct open_request *)(req->data);

  sqlite3 **dbptr = open_req->dbo->GetDBPtr();
  int rc = sqlite3_open_v2( open_req->filename
                          , dbptr
                          , SQLITE_OPEN_READWRITE
                            | SQLITE_OPEN_CREATE
                            | SQLITE_OPEN_FULLMUTEX
                          , NULL);

  open_req->result = rc;

  // Set the a 10s timeout valuei for retries on BUSY errors.
  sqlite3_busy_timeout(*dbptr, 10000);


//   sqlite3 *db = *dbptr;
//   sqlite3_commit_hook(db, CommitHook, open_req->dbo);
//   sqlite3_rollback_hook(db, RollbackHook, open_req->dbo);
//   sqlite3_update_hook(db, UpdateHook, open_req->dbo);

  free(open_req->filename);
  open_req->filename = NULL;
}

Handle<Value> Database::Open(const Arguments& args) {
  HandleScope scope;

  REQ_STR_ARG(0, filename);
  REQ_FUN_ARG(1, cb);

  Database* dbo = ObjectWrap::Unwrap<Database>(args.This());

  struct open_request *open_req = (struct open_request *)
      calloc(1, sizeof(struct open_request) + filename.length());

  if (!open_req) {
    V8::LowMemoryNotification();
    return ThrowException(Exception::Error(
      String::New("Could not allocate enough memory")));
  }

  open_req->filename = (char *)malloc(strlen(*filename) + 1);
  strcpy(open_req->filename, *filename);
  open_req->cb = Persistent<Function>::New(cb);
  open_req->dbo = dbo;

  uv_work_t *req = new uv_work_t;
  req->data = open_req;

  uv_queue_work(uv_default_loop(), req, UVWORK_Open, UVWORK_AfterOpen);

  dbo->Ref();

  return Undefined();
}

void Database::UVWORK_AfterClose(uv_work_t *req) {
  HandleScope scope;

  struct close_request *close_req = (struct close_request *)(req->data);

  Local<Value> argv[1];
  bool err = false;
  if (close_req->result) {
    err = true;
    argv[0] = Exception::Error(String::New("Error closing database"));
  }

  TryCatch try_catch;

  close_req->dbo->Unref();
  close_req->cb->Call(Context::GetCurrent()->Global(), err ? 1 : 0, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  close_req->cb.Dispose();

  free(close_req);
  delete req;
}

void Database::UVWORK_Close(uv_work_t *req) {
  struct close_request *close_req = (struct close_request *)(req->data);
  Database* dbo = close_req->dbo;
  close_req->result = sqlite3_close(dbo->db_);
  dbo->db_ = NULL;
}

Handle<Value> Database::Close(const Arguments& args) {
  HandleScope scope;

  REQ_FUN_ARG(0, cb);

  Database* dbo = ObjectWrap::Unwrap<Database>(args.This());

  struct close_request *close_req = (struct close_request *)
      calloc(1, sizeof(struct close_request));

  if (!close_req) {
    V8::LowMemoryNotification();
    return ThrowException(Exception::Error(
      String::New("Could not allocate enough memory")));
  }

  close_req->cb = Persistent<Function>::New(cb);
  close_req->dbo = dbo;

  uv_work_t *req = new uv_work_t;
  req->data = close_req;

  uv_queue_work(uv_default_loop(), req, UVWORK_Close, UVWORK_AfterClose);

  dbo->Ref();

  return Undefined();
}

// // TODO: libeio'fy

// Hooks
//   static int CommitHook(void* v_this) {
//     HandleScope scope;
//     Database* db = static_cast<Database*>(v_this);
//     db->Emit(String::New("commit"), 0, NULL);
//     // TODO: allow change in return value to convert to rollback...somehow
//     return 0;
//   }
//
//   static void RollbackHook(void* v_this) {
//     HandleScope scope;
//     Database* db = static_cast<Database*>(v_this);
//     db->Emit(String::New("rollback"), 0, NULL);
//   }
//
//   static void UpdateHook(void* v_this, int operation, const char* database,
//                          const char* table, sqlite_int64 rowid) {
//     HandleScope scope;
//     Database* db = static_cast<Database*>(v_this);
//     Local<Value> args[] = { Int32::New(operation), String::New(database),
//                             String::New(table), Number::New(rowid) };
//     db->Emit(String::New("update"), 4, args);
//   }

void Database::UVWORK_AfterPrepareAndStep(uv_work_t *req) {
  struct prepare_request *prep_req = (struct prepare_request *)(req->data);
  HandleScope scope;

  Local<Value> argv[2];
  int argc = 0;

  // if the prepare failed
  if (prep_req->result != SQLITE_OK) {
    argv[0] = Exception::Error(
        String::New(sqlite3_errmsg(prep_req->dbo->db_)));
    argc = 1;

  }
  else {
    if (prep_req->int1 == SQLITE_DONE) {

      if (prep_req->mode != EXEC_EMPTY) {
        argv[0] = Local<Value>::New(Undefined());   // no error

        Local<Object> info = Object::New();

        if (prep_req->mode & EXEC_LAST_INSERT_ID) {
          info->Set(String::NewSymbol("last_inserted_id"),
              Number::New (prep_req->lastInsertId));
        }
        if (prep_req->mode & EXEC_AFFECTED_ROWS) {
          info->Set(String::NewSymbol("affected_rows"),
              Integer::New (prep_req->affectedRows));
        }
        argv[1] = info;
        argc = 2;

      } else {
        argc = 0;
      }

    }
    else {
      argv[0] = External::New(prep_req->stmt);
      argv[1] = Integer::New(prep_req->int1);
      Persistent<Object> statement(
          Statement::constructor_template->GetFunction()->NewInstance(2, argv));

      if (prep_req->tail) {
        statement->Set(String::New("tail"), String::New(prep_req->tail));
      }

      argv[0] = Local<Value>::New(Undefined());
      argv[1] = Local<Value>::New(statement);
      argc = 2;
    }
  }

  TryCatch try_catch;

  prep_req->dbo->Unref();
  prep_req->cb->Call(Context::GetCurrent()->Global(), argc, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  prep_req->cb.Dispose();
  free(prep_req);
  delete req;
}

void Database::UVWORK_PrepareAndStep(uv_work_t *req) {
  struct prepare_request *prep_req = (struct prepare_request *)(req->data);

  prep_req->stmt = NULL;
  prep_req->tail = NULL;
  sqlite3* db = prep_req->dbo->db_;

  int rc = sqlite3_prepare_v2(db, prep_req->sql, -1,
              &(prep_req->stmt), &(prep_req->tail));

  prep_req->result = rc;
  prep_req->int1 = -1;

  // This might be a INSERT statement. Let's try to get the first row.
  // This is to optimize out further calls to the thread pool. This is only
  // possible in the case where there are no variable placeholders/bindings
  // in the SQL.
  if (rc == SQLITE_OK && !sqlite3_bind_parameter_count(prep_req->stmt)) {
    rc = sqlite3_step(prep_req->stmt);
    prep_req->int1 = rc;

    // no more rows to return, clean up statement
    if (rc == SQLITE_DONE) {
      rc = sqlite3_finalize(prep_req->stmt);
      prep_req->stmt = NULL;
      assert(rc == SQLITE_OK);
    }
  }

  prep_req->lastInsertId = 0;
  prep_req->affectedRows = 0;

  // load custom properties
  if (prep_req->mode & EXEC_LAST_INSERT_ID)
      prep_req->lastInsertId = sqlite3_last_insert_rowid(db);
  if (prep_req->mode & EXEC_AFFECTED_ROWS)
      prep_req->affectedRows = sqlite3_changes(db);
}

Handle<Value> Database::PrepareAndStep(const Arguments& args) {
  HandleScope scope;

  REQ_STR_ARG(0, sql);
  REQ_FUN_ARG(1, cb);
  OPT_INT_ARG(2, mode, EXEC_EMPTY);

  Database* dbo = ObjectWrap::Unwrap<Database>(args.This());

  struct prepare_request *prep_req = (struct prepare_request *)
      calloc(1, sizeof(struct prepare_request) + sql.length());

  if (!prep_req) {
    V8::LowMemoryNotification();
    return ThrowException(Exception::Error(
      String::New("Could not allocate enough memory")));
  }

  strcpy(prep_req->sql, *sql);
  prep_req->cb = Persistent<Function>::New(cb);
  prep_req->dbo = dbo;
  prep_req->mode = mode;

  uv_work_t *req = new uv_work_t;
  req->data = prep_req;

  uv_queue_work(uv_default_loop(), req, UVWORK_PrepareAndStep, UVWORK_AfterPrepareAndStep);

  dbo->Ref();

  return Undefined();
}

void Database::UVWORK_AfterPrepare(uv_work_t *req) {
  struct prepare_request *prep_req = (struct prepare_request *)(req->data);
  HandleScope scope;

  Local<Value> argv[3];
  int argc = 0;

  // if the prepare failed
  if (prep_req->result != SQLITE_OK) {
    argv[0] = Exception::Error(
                String::New(sqlite3_errmsg(prep_req->dbo->db_)));
    argc = 1;
  }
  else {
    argv[0] = External::New(prep_req->stmt);
    argv[1] = Integer::New(-1);
    argv[2] = Integer::New(prep_req->mode);
    Persistent<Object> statement(
      Statement::constructor_template->GetFunction()->NewInstance(3, argv));

    if (prep_req->tail) {
      statement->Set(String::New("tail"), String::New(prep_req->tail));
    }

    argc = 2;
    argv[0] = Local<Value>::New(Undefined());
    argv[1] = Local<Value>::New(statement);
  }

  TryCatch try_catch;

  prep_req->dbo->Unref();
  prep_req->cb->Call(Context::GetCurrent()->Global(), argc, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  prep_req->cb.Dispose();
  free(prep_req);
  delete req;
}

void Database::UVWORK_Prepare(uv_work_t *req) {
  struct prepare_request *prep_req = (struct prepare_request *)(req->data);

  prep_req->stmt = NULL;
  prep_req->tail = NULL;
  sqlite3* db = prep_req->dbo->db_;

  int rc = sqlite3_prepare_v2(db, prep_req->sql, -1,
              &(prep_req->stmt), &(prep_req->tail));

  prep_req->result = rc;

  prep_req->lastInsertId = 0;
  prep_req->affectedRows = 0;

  // load custom properties
  if (prep_req->mode & EXEC_LAST_INSERT_ID)
      prep_req->lastInsertId = sqlite3_last_insert_rowid(db);
  if (prep_req->mode & EXEC_AFFECTED_ROWS)
      prep_req->affectedRows = sqlite3_changes(db);

  free(prep_req->sql);
  prep_req = NULL;
}

// Statement#prepare(sql, [ options ,] callback);
Handle<Value> Database::Prepare(const Arguments& args) {
  HandleScope scope;
  Local<Object> options;
  Local<Function> cb;
  int mode;

  REQ_STR_ARG(0, sql);

  // middle argument could be options or
  switch (args.Length()) {
    case 2:
      if (!args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
                  String::New("Argument 1 must be a function")));
      }
      cb = Local<Function>::Cast(args[1]);
      options = Object::New();
      break;

    case 3:
      if (!args[1]->IsObject()) {
        return ThrowException(Exception::TypeError(
                  String::New("Argument 1 must be an object")));
      }
      options = Local<Function>::Cast(args[1]);

      if (!args[2]->IsFunction()) {
        return ThrowException(Exception::TypeError(
                  String::New("Argument 2 must be a function")));
      }
      cb = Local<Function>::Cast(args[2]);
      break;
  }

  mode = EXEC_EMPTY;

  if (options->Get(String::New("lastInsertRowID"))->IsTrue()) {
    mode |= EXEC_LAST_INSERT_ID;
  }
  if (options->Get(String::New("affectedRows"))->IsTrue())  {
    mode |= EXEC_AFFECTED_ROWS;
  }

  Database* dbo = ObjectWrap::Unwrap<Database>(args.This());

  struct prepare_request *prep_req = (struct prepare_request *)
      calloc(1, sizeof(struct prepare_request) + sql.length());

  if (!prep_req) {
    V8::LowMemoryNotification();
    return ThrowException(Exception::Error(
      String::New("Could not allocate enough memory")));
  }

  prep_req->sql = (char *)malloc(strlen(*sql) + 1);
  strcpy(prep_req->sql, *sql);
  prep_req->cb = Persistent<Function>::New(cb);
  prep_req->dbo = dbo;
  prep_req->mode = mode;

  uv_work_t *req = new uv_work_t;
  req->data = prep_req;

  uv_queue_work(uv_default_loop(), req, UVWORK_Prepare, UVWORK_AfterPrepare);

  dbo->Ref();

  return Undefined();
}

Persistent<FunctionTemplate> Database::constructor_template;
