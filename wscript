# vim: ft=javascript
import Options
from os import unlink, symlink, system
from os.path import exists, abspath

srcdir = "."
blddir = "build"
VERSION = "0.0.1"
SQLITE = "sqlite-amalgamation-3070500"

def set_options(opt):
  opt.tool_options("compiler_cxx")
  opt.tool_options("compiler_cc")

def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("compiler_cc")
  conf.check_tool("node_addon")

  conf.env.append_value('LIBPATH_SQLITE', abspath('build/default/deps/'+SQLITE))
  conf.env.append_value('STATICLIB_SQLITE', 'sqlite3-bundled')
  conf.env.append_value('CPATH_SQLITE', abspath('./deps/'+SQLITE))

def build(bld):
  mpool = bld.new_task_gen('cc', 'staticlib')
  mpool.ccflags = ["-g", "-fPIC", "-Wall"]
  mpool.source = 'src/mpool.c'
  mpool.target = 'mpool_bindings'
  mpool.name = "mpool"

  sqlite = bld.new_task_gen('cc', 'staticlib')
  sqlite.ccflags = ["-g", "-fPIC", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE", "-Wall", "-DSQLITE_ENABLE_FTS3", "-DSQLITE_ENABLE_FTS3_PARENTHESIS"]
  sqlite.source = '/'.join(['deps', SQLITE, 'sqlite3.c'])
  sqlite.target = '/'.join(['deps', SQLITE, 'sqlite3-bundled'])
  sqlite.name = "sqlite3"

  obj = bld.new_task_gen("cxx", "shlib", "node_addon")
  obj.cxxflags = ["-g", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE", "-Wall", "-I../deps/"+SQLITE]
  obj.target = "sqlite3_bindings"
  obj.source = "src/sqlite3_bindings.cc src/database.cc src/statement.cc src/events.cc"
  obj.uselib_local = ["sqlite3", "mpool"]

t = 'sqlite3_bindings.node'
def shutdown():
  # HACK to get binding.node out of build directory.
  # better way to do this?
  if Options.commands['clean']:
    if exists(t): unlink(t)
  else:
    if exists('build/default/' + t) and not exists(t):
      symlink('build/default/' + t, t)

